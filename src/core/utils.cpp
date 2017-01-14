#include "core/utils.h"

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/eval.h>
#include <libavutil/display.h>
#include <libswscale/swscale.h>
}

#include "core/audio_params.h"

/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

int check_stream_specifier(AVFormatContext* s, AVStream* st, const char* spec) {
  int ret = avformat_match_stream_specifier(s, st, spec);
  if (ret < 0)
    av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
  return ret;
}

AVDictionary* filter_codec_opts(AVDictionary* opts,
                                enum AVCodecID codec_id,
                                AVFormatContext* s,
                                AVStream* st,
                                AVCodec* codec) {
  AVDictionary* ret = NULL;
  AVDictionaryEntry* t = NULL;
  int flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM : AV_OPT_FLAG_DECODING_PARAM;
  char prefix = 0;
  const AVClass* cc = avcodec_get_class();

  if (!codec)
    codec = s->oformat ? avcodec_find_encoder(codec_id) : avcodec_find_decoder(codec_id);

  switch (st->codecpar->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      prefix = 'v';
      flags |= AV_OPT_FLAG_VIDEO_PARAM;
      break;
    case AVMEDIA_TYPE_AUDIO:
      prefix = 'a';
      flags |= AV_OPT_FLAG_AUDIO_PARAM;
      break;
    case AVMEDIA_TYPE_SUBTITLE:
      prefix = 's';
      flags |= AV_OPT_FLAG_SUBTITLE_PARAM;
      break;
  }

  while (t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX)) {
    char* p = strchr(t->key, ':');

    /* check stream specification in opt name */
    if (p)
      switch (check_stream_specifier(s, st, p + 1)) {
        case 1:
          *p = 0;
          break;
        case 0:
          continue;
        default:
          exit_program(1);
      }

    if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) || !codec ||
        (codec->priv_class &&
         av_opt_find(&codec->priv_class, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ)))
      av_dict_set(&ret, t->key, t->value, 0);
    else if (t->key[0] == prefix &&
             av_opt_find(&cc, t->key + 1, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ))
      av_dict_set(&ret, t->key + 1, t->value, 0);

    if (p)
      *p = ':';
  }
  return ret;
}

AVDictionary** setup_find_stream_info_opts(AVFormatContext* s, AVDictionary* codec_opts) {
  if (!s->nb_streams) {
    return NULL;
  }

  AVDictionary** opts = static_cast<AVDictionary**>(av_mallocz_array(s->nb_streams, sizeof(*opts)));
  if (!opts) {
    av_log(NULL, AV_LOG_ERROR, "Could not alloc memory for stream options.\n");
    return NULL;
  }

  for (unsigned int i = 0; i < s->nb_streams; i++) {
    opts[i] =
        filter_codec_opts(codec_opts, s->streams[i]->codecpar->codec_id, s, s->streams[i], NULL);
  }
  return opts;
}

double get_rotation(AVStream* st) {
  AVDictionaryEntry* rotate_tag = av_dict_get(st->metadata, "rotate", NULL, 0);
  uint8_t* displaymatrix = av_stream_get_side_data(st, AV_PKT_DATA_DISPLAYMATRIX, NULL);
  double theta = 0;

  if (rotate_tag && *rotate_tag->value && strcmp(rotate_tag->value, "0")) {
    char* tail;
    theta = av_strtod(rotate_tag->value, &tail);
    if (*tail)
      theta = 0;
  }
  if (displaymatrix && !theta) {
    theta = -av_display_rotation_get(reinterpret_cast<int32_t*>(displaymatrix));
  }

  theta -= 360 * floor(theta / 360 + 0.9 / 360);

  if (fabs(theta - 90 * round(theta / 90)) > 2)
    av_log(NULL, AV_LOG_WARNING,
           "Odd rotation angle.\n"
           "If you want to help, upload a sample "
           "of this file to ftp://upload.ffmpeg.org/incoming/ "
           "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)");

  return theta;
}

void exit_program(int ret) {
  exit(ret);
}

#if CONFIG_AVFILTER
int configure_filtergraph(AVFilterGraph* graph,
                          const char* filtergraph,
                          AVFilterContext* source_ctx,
                          AVFilterContext* sink_ctx) {
  int ret;
  unsigned int nb_filters = graph->nb_filters;
  AVFilterInOut *outputs = NULL, *inputs = NULL;

  if (filtergraph) {
    outputs = avfilter_inout_alloc();
    inputs = avfilter_inout_alloc();
    if (!outputs || !inputs) {
      ret = AVERROR(ENOMEM);
      goto fail;
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = source_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = sink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0) {
      goto fail;
    }
  } else {
    if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0) {
      goto fail;
    }
  }

  /* Reorder the filters to ensure that inputs of the custom filters are merged first */
  for (unsigned int i = 0; i < graph->nb_filters - nb_filters; i++) {
    FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);
  }

  ret = avfilter_graph_config(graph, NULL);
fail:
  avfilter_inout_free(&outputs);
  avfilter_inout_free(&inputs);
  return ret;
}
#endif

void calculate_display_rect(SDL_Rect* rect,
                            int scr_xleft,
                            int scr_ytop,
                            int scr_width,
                            int scr_height,
                            int pic_width,
                            int pic_height,
                            AVRational pic_sar) {
  float aspect_ratio;

  if (pic_sar.num == 0) {
    aspect_ratio = 0;
  } else {
    aspect_ratio = av_q2d(pic_sar);
  }

  if (aspect_ratio <= 0.0) {
    aspect_ratio = 1.0;
  }
  aspect_ratio *= static_cast<float>(pic_width) / static_cast<float>(pic_height);

  /* XXX: we suppose the screen has a 1.0 pixel ratio */
  int height = scr_height;
  int width = lrint(height * aspect_ratio) & ~1;
  if (width > scr_width) {
    width = scr_width;
    height = lrint(width / aspect_ratio) & ~1;
  }
  int x = (scr_width - width) / 2;
  int y = (scr_height - height) / 2;
  rect->x = scr_xleft + x;
  rect->y = scr_ytop + y;
  rect->w = FFMAX(width, 1);
  rect->h = FFMAX(height, 1);
}

void fill_rectangle(SDL_Renderer* renderer, int x, int y, int w, int h) {
  if (!renderer || !w || !h) {
    return;
  }

  SDL_Rect rect;
  rect.x = x;
  rect.y = y;
  rect.w = w;
  rect.h = h;
  SDL_RenderFillRect(renderer, &rect);
}

int compute_mod(int a, int b) {
  return a < 0 ? a % b + b : a % b;
}

static unsigned sws_flags = SWS_BICUBIC;

int upload_texture(SDL_Texture* tex, AVFrame* frame, struct SwsContext** img_convert_ctx) {
  int ret = 0;
  switch (frame->format) {
    case AV_PIX_FMT_YUV420P:
      if (frame->linesize[0] < 0 || frame->linesize[1] < 0 || frame->linesize[2] < 0) {
        av_log(NULL, AV_LOG_ERROR, "Negative linesize is not supported for YUV.\n");
        return -1;
      }
      ret = SDL_UpdateYUVTexture(tex, NULL, frame->data[0], frame->linesize[0], frame->data[1],
                                 frame->linesize[1], frame->data[2], frame->linesize[2]);
      break;
    case AV_PIX_FMT_BGRA:
      if (frame->linesize[0] < 0) {
        ret =
            SDL_UpdateTexture(tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1),
                              -frame->linesize[0]);
      } else {
        ret = SDL_UpdateTexture(tex, NULL, frame->data[0], frame->linesize[0]);
      }
      break;
    default:
      /* This should only happen if we are not using avfilter... */
      *img_convert_ctx = sws_getCachedContext(
          *img_convert_ctx, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
          frame->width, frame->height, AV_PIX_FMT_BGRA, sws_flags, NULL, NULL, NULL);
      if (*img_convert_ctx != NULL) {
        uint8_t* pixels[4];
        int pitch[4];
        if (!SDL_LockTexture(tex, NULL, reinterpret_cast<void**>(pixels), pitch)) {
          sws_scale(*img_convert_ctx, const_cast<const uint8_t* const*>(frame->data),
                    frame->linesize, 0, frame->height, pixels, pitch);
          SDL_UnlockTexture(tex);
        }
      } else {
        av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
        ret = -1;
      }
      break;
  }
  return ret;
}

int audio_open(void* opaque,
               int64_t wanted_channel_layout,
               int wanted_nb_channels,
               int wanted_sample_rate,
               struct AudioParams* audio_hw_params,
               SDL_AudioCallback cb) {
  SDL_AudioSpec wanted_spec, spec;
  static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
  static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
  int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

  const char* env = SDL_getenv("SDL_AUDIO_CHANNELS");
  if (env) {
    wanted_nb_channels = atoi(env);
    wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
  }
  if (!wanted_channel_layout ||
      wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
    wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
  }
  wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
  wanted_spec.channels = wanted_nb_channels;
  wanted_spec.freq = wanted_sample_rate;
  if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
    av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
    return -1;
  }
  while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq) {
    next_sample_rate_idx--;
  }
  wanted_spec.format = AUDIO_S16SYS;
  wanted_spec.silence = 0;
  wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE,
                              2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
  wanted_spec.callback = cb;
  wanted_spec.userdata = opaque;
  while (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
    av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n", wanted_spec.channels,
           wanted_spec.freq, SDL_GetError());
    wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
    if (!wanted_spec.channels) {
      wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
      wanted_spec.channels = wanted_nb_channels;
      if (!wanted_spec.freq) {
        av_log(NULL, AV_LOG_ERROR, "No more combinations to try, audio open failed\n");
        return -1;
      }
    }
    wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
  }
  if (spec.format != AUDIO_S16SYS) {
    av_log(NULL, AV_LOG_ERROR, "SDL advised audio format %d is not supported!\n", spec.format);
    return -1;
  }
  if (spec.channels != wanted_spec.channels) {
    wanted_channel_layout = av_get_default_channel_layout(spec.channels);
    if (!wanted_channel_layout) {
      av_log(NULL, AV_LOG_ERROR, "SDL advised channel count %d is not supported!\n", spec.channels);
      return -1;
    }
  }

  audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
  audio_hw_params->freq = spec.freq;
  audio_hw_params->channel_layout = wanted_channel_layout;
  audio_hw_params->channels = spec.channels;
  audio_hw_params->frame_size =
      av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
  audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(
      NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
  if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
    av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
    return -1;
  }
  return spec.size;
}
