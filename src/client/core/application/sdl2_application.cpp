/*  Copyright (C) 2014-2017 FastoGT. All right reserved.

    This file is part of FastoTV.

    FastoTV is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FastoTV is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FastoTV. If not, see <http://www.gnu.org/licenses/>.
*/

#include "client/core/application/sdl2_application.h"

#include <SDL2/SDL.h>         // for SDL_INIT_AUDIO, etc
#include <SDL2/SDL_error.h>   // for SDL_GetError
#include <SDL2/SDL_mutex.h>   // for SDL_CreateMutex, etc
#include <SDL2/SDL_stdinc.h>  // for SDL_getenv, SDL_setenv, etc
#include <SDL2/SDL_ttf.h>

#include <common/threads/event_bus.h>

#include "client/core/events/events.h"
#include "client/core/types.h"

#define FASTO_EVENT (SDL_USEREVENT)

#undef ERROR

namespace {
Keysym SDLKeySymToOur(SDL_Keysym sks) {
  Keysym ks;
  ks.mod = sks.mod;
  ks.scancode = static_cast<Scancode>(sks.scancode);
  ks.sym = sks.sym;
  return ks;
}
}

namespace fasto {
namespace fastotv {
namespace client {
namespace core {
namespace application {

Sdl2Application::Sdl2Application(int argc, char** argv)
    : common::application::IApplicationImpl(argc, argv), dispatcher_() {}

Sdl2Application::~Sdl2Application() {
  THREAD_MANAGER()->FreeInstance();
}

int Sdl2Application::PreExec() {
  Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
  int res = SDL_Init(flags);
  if (res != 0) {
    ERROR_LOG() << "Could not initialize SDL error: " << SDL_GetError();
    return EXIT_FAILURE;
  }

  SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
  SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

  res = TTF_Init();
  if (res != 0) {
    ERROR_LOG() << "SDL_ttf could not error: " << TTF_GetError();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int Sdl2Application::Exec() {
  SDL_TimerID my_timer_id = SDL_AddTimer(event_timeout_wait_msec, timer_callback, this);

  SDL_Event event;
  while (SDL_WaitEvent(&event)) {
    bool is_stop_event = event.type == FASTO_EVENT && event.user.data1 == NULL;
    if (is_stop_event) {
      break;
    }
    ProcessEvent(&event);
  }

  SDL_RemoveTimer(my_timer_id);
  return EXIT_SUCCESS;
}

void Sdl2Application::ProcessEvent(SDL_Event* event) {
  switch (event->type) {
    case SDL_KEYDOWN: {
      HandleKeyPressEvent(&event->key);
      break;
    }
    case SDL_KEYUP: {
      break;
    }
    case SDL_MOUSEBUTTONDOWN: {
      HandleMousePressEvent(&event->button);
      break;
    }
    case SDL_MOUSEBUTTONUP: {
      HandleMouseReleaseEvent(&event->button);
      break;
    }
    case SDL_MOUSEMOTION: {
      HandleMouseMoveEvent(&event->motion);
      break;
    }
    case SDL_WINDOWEVENT: {
      HandleWindowEvent(&event->window);
      break;
    }
    case SDL_QUIT: {
      HandleQuitEvent(&event->quit);
      break;
    }
    case FASTO_EVENT: {
      events::Event* fevent = static_cast<events::Event*>(event->user.data1);
      HandleEvent(fevent);
      break;
    }
    default:
      break;
  }
}

int Sdl2Application::PostExec() {
  TTF_Quit();
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == FASTO_EVENT) {
      events::Event* fevent = static_cast<events::Event*>(event.user.data1);
      delete fevent;
    }
  }
  SDL_Quit();
  return EXIT_SUCCESS;
}

void Sdl2Application::Subscribe(common::IListener* listener, common::events_size_t id) {
  dispatcher_.Subscribe(static_cast<events::EventListener*>(listener), id);
}

void Sdl2Application::UnSubscribe(common::IListener* listener, common::events_size_t id) {
  dispatcher_.UnSubscribe(static_cast<events::EventListener*>(listener), id);
}

void Sdl2Application::UnSubscribe(common::IListener* listener) {
  dispatcher_.UnSubscribe(static_cast<events::EventListener*>(listener));
}

void Sdl2Application::SendEvent(common::IEvent* event) {
  if (THREAD_MANAGER()->IsMainThread()) {
    events::Event* fevent = static_cast<events::Event*>(event);
    HandleEvent(fevent);
    return;
  }

  DNOTREACHED();
  PostEvent(event);
}

void Sdl2Application::PostEvent(common::IEvent* event) {
  SDL_Event sevent;
  sevent.type = FASTO_EVENT;
  sevent.user.data1 = event;
  int res = SDL_PushEvent(&sevent);
  if (res != 1) {
    DNOTREACHED();
    delete event;
  }
}

void Sdl2Application::Exit(int result) {
  UNUSED(result);
  PostEvent(NULL);  // FIX ME
}

void Sdl2Application::ShowCursor() {
  SDL_ShowCursor(1);
}

void Sdl2Application::HideCursor() {
  SDL_ShowCursor(0);
}

void Sdl2Application::HandleEvent(events::Event* event) {
  msec_t start_time = GetCurrentMsec();
  EventsType event_type = event->GetEventType();
  bool is_filtered_event = event_type == PRE_EXEC_EVENT || event_type == POST_EXEC_EVENT;
  dispatcher_.ProcessEvent(event);
  msec_t finish_time = GetCurrentMsec();

  msec_t diff_time = finish_time - start_time;
  if (diff_time >= event_timeout_wait_msec && !is_filtered_event) {
    WARNING_LOG() << "Long time execution(" << diff_time << " msec) of event type: " << event_type;
  }
}

void Sdl2Application::HandleKeyPressEvent(SDL_KeyboardEvent* event) {
  if (event->type == SDL_KEYDOWN && event->repeat == 0) {
    Keysym ks = SDLKeySymToOur(event->keysym);
    events::KeyPressInfo inf(event->state == SDL_PRESSED, ks);
    events::KeyPressEvent* key_press = new events::KeyPressEvent(this, inf);
    HandleEvent(key_press);
  } else if (event->type == SDL_KEYUP) {
    Keysym ks = SDLKeySymToOur(event->keysym);
    events::KeyReleaseInfo inf(event->state == SDL_PRESSED, ks);
    events::KeyReleaseEvent* key_release = new events::KeyReleaseEvent(this, inf);
    HandleEvent(key_release);
  }
}

void Sdl2Application::HandleWindowEvent(SDL_WindowEvent* event) {  // SDL_WindowEventID
  if (event->event == SDL_WINDOWEVENT_RESIZED) {
    Size new_size(event->data1, event->data2);
    events::WindowResizeInfo inf(new_size);
    events::WindowResizeEvent* wind_resize = new events::WindowResizeEvent(this, inf);
    HandleEvent(wind_resize);
  } else if (event->event == SDL_WINDOWEVENT_EXPOSED) {
    events::WindowExposeInfo inf;
    events::WindowExposeEvent* wind_exp = new events::WindowExposeEvent(this, inf);
    HandleEvent(wind_exp);
  } else if (event->event == SDL_WINDOWEVENT_CLOSE) {
    events::WindowCloseInfo inf;
    events::WindowCloseEvent* wind_close = new events::WindowCloseEvent(this, inf);
    HandleEvent(wind_close);
  }
}

void Sdl2Application::HandleMousePressEvent(SDL_MouseButtonEvent* event) {
  events::MousePressInfo inf(event->button, event->state);
  events::MousePressEvent* mouse_press_event = new events::MousePressEvent(this, inf);
  HandleEvent(mouse_press_event);
}

void Sdl2Application::HandleMouseReleaseEvent(SDL_MouseButtonEvent* event) {
  events::MouseReleaseInfo inf(event->button, event->state);
  events::MouseReleaseEvent* mouse_release_event = new events::MouseReleaseEvent(this, inf);
  HandleEvent(mouse_release_event);
}

void Sdl2Application::HandleMouseMoveEvent(SDL_MouseMotionEvent* event) {
  UNUSED(event);

  events::MouseMoveInfo inf;
  events::MouseMoveEvent* mouse_move_event = new events::MouseMoveEvent(this, inf);
  HandleEvent(mouse_move_event);
}

void Sdl2Application::HandleQuitEvent(SDL_QuitEvent* event) {
  UNUSED(event);

  events::QuitInfo inf;
  events::QuitEvent* quit_event = new events::QuitEvent(this, inf);
  HandleEvent(quit_event);
}

Uint32 Sdl2Application::timer_callback(Uint32 interval, void* user_data) {
  Sdl2Application* app = static_cast<Sdl2Application*>(user_data);
  events::TimeInfo inf;
  events::TimerEvent* timer_event = new events::TimerEvent(app, inf);
  app->PostEvent(timer_event);
  return interval;
}
}
}
}
}
}
