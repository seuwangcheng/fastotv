/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "client/core/ffmpeg_vdpau.h"

extern "C" {
#include <libavcodec/vdpau.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vdpau.h>
}

#include <common/macros.h>

#include "client/core/ffmpeg_internal.h"

namespace fasto {
namespace fastotv {
namespace client {
namespace core {

typedef struct VDPAUContext {
  AVBufferRef* hw_frames_ctx;
  AVFrame* tmp_frame;
} VDPAUContext;

typedef void vdpau_uninit_callback_t(AVCodecContext* s);
typedef int vdpau_get_buffer_callback_t(AVCodecContext* s, AVFrame* frame, int flags);
typedef int vdpau_retrieve_data_callback_t(AVCodecContext* s, AVFrame* frame);

static void vdpau_uninit(AVCodecContext* s) {
  InputStream* ist = static_cast<InputStream*>(s->opaque);
  VDPAUContext* ctx = static_cast<VDPAUContext*>(ist->hwaccel_ctx);

  ist->hwaccel_uninit = NULL;
  ist->hwaccel_get_buffer = NULL;
  ist->hwaccel_retrieve_data = NULL;

  av_buffer_unref(&ctx->hw_frames_ctx);
  av_frame_free(&ctx->tmp_frame);

  av_freep(&ist->hwaccel_ctx);
  av_freep(&s->hwaccel_context);
}

static int vdpau_get_buffer(AVCodecContext* s, AVFrame* frame, int flags) {
  UNUSED(flags);
  InputStream* ist = static_cast<InputStream*>(s->opaque);
  VDPAUContext* ctx = static_cast<VDPAUContext*>(ist->hwaccel_ctx);

  return av_hwframe_get_buffer(ctx->hw_frames_ctx, frame, 0);
}

static int vdpau_retrieve_data(AVCodecContext* s, AVFrame* frame) {
  InputStream* ist = static_cast<InputStream*>(s->opaque);
  VDPAUContext* ctx = static_cast<VDPAUContext*>(ist->hwaccel_ctx);

  int ret = av_hwframe_transfer_data(ctx->tmp_frame, frame, 0);
  if (ret < 0) {
    return ret;
  }

  ret = av_frame_copy_props(ctx->tmp_frame, frame);
  if (ret < 0) {
    av_frame_unref(ctx->tmp_frame);
    return ret;
  }

  av_frame_unref(frame);
  av_frame_move_ref(frame, ctx->tmp_frame);
  return 0;
}

static int vdpau_alloc(AVCodecContext* s) {
  InputStream* ist = static_cast<InputStream*>(s->opaque);

  VDPAUContext* ctx = static_cast<VDPAUContext*>(av_mallocz(sizeof(*ctx)));
  if (!ctx) {
    ERROR_LOG() << "VDPAU init failed(alloc VDPAUContext).";
    vdpau_uninit(s);
    return AVERROR(ENOMEM);
  }

  ist->hwaccel_ctx = ctx;
  ist->hwaccel_uninit = vdpau_uninit;
  ist->hwaccel_get_buffer = vdpau_get_buffer;
  ist->hwaccel_retrieve_data = vdpau_retrieve_data;

  ctx->tmp_frame = av_frame_alloc();
  if (!ctx->tmp_frame) {
    ERROR_LOG() << "Failed to create VDPAU frame context.";
    vdpau_uninit(s);
    return AVERROR(ENOMEM);
  }

  AVBufferRef* device_ref = NULL;
  int ret =
      av_hwdevice_ctx_create(&device_ref, AV_HWDEVICE_TYPE_VDPAU, ist->hwaccel_device, NULL, 0);
  if (ret < 0) {
    ERROR_LOG() << "VDPAU init failed error: " << ret;
    vdpau_uninit(s);
    return AVERROR(EINVAL);
  }
  AVHWDeviceContext* device_ctx = reinterpret_cast<AVHWDeviceContext*>(device_ref->data);
  AVVDPAUDeviceContext* device_hwctx = static_cast<AVVDPAUDeviceContext*>(device_ctx->hwctx);

  ctx->hw_frames_ctx = av_hwframe_ctx_alloc(device_ref);
  if (!ctx->hw_frames_ctx) {
    ERROR_LOG() << "Failed to create VDPAU frame context.";
    vdpau_uninit(s);
    return AVERROR(ENOMEM);
  }

  av_buffer_unref(&device_ref);

  AVHWFramesContext* frames_ctx = reinterpret_cast<AVHWFramesContext*>(ctx->hw_frames_ctx->data);
  frames_ctx->format = AV_PIX_FMT_VDPAU;
  frames_ctx->sw_format = s->sw_pix_fmt;
  frames_ctx->width = s->coded_width;
  frames_ctx->height = s->coded_height;

  ret = av_hwframe_ctx_init(ctx->hw_frames_ctx);
  if (ret < 0) {
    ERROR_LOG() << "Failed to initialise VDPAU hw_frame context error: " << ret;
    vdpau_uninit(s);
    return AVERROR(EINVAL);
  }

  ret = av_vdpau_bind_context(s, device_hwctx->device, device_hwctx->get_proc_address, 0);
  if (ret != 0) {
    ERROR_LOG() << "Failed to bind VDPAU context error: " << ret;
    vdpau_uninit(s);
    return AVERROR(EINVAL);
  }

  INFO_LOG() << "Using VDPAU to decode input stream.";
  return 0;
}

int vdpau_init(AVCodecContext* decoder_ctx) {
  InputStream* ist = static_cast<InputStream*>(decoder_ctx->opaque);

  if (!ist->hwaccel_ctx) {
    int ret = vdpau_alloc(decoder_ctx);
    if (ret < 0) {
      return ret;
    }
  }

  ist->hwaccel_get_buffer = vdpau_get_buffer;
  ist->hwaccel_retrieve_data = vdpau_retrieve_data;
  return 0;
}
}
}
}
}