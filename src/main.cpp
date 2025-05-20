#include <mpv/client.h>
#include <mpv/render_gl.h>

#define SHOGLE_EXPOSE_GLFW 1
#include <shogle/render.hpp>

#include <iostream>
#include <mutex>

template<typename... Args>
[[noreturn]] static void die(fmt::format_string<Args...> fmt, Args&&... args) {
  ntf::logger::error(fmt, std::forward<Args>(args)...);
  std::exit(1);
}

static void* mpv_proc_address([[maybe_unused]] void* ctx, const char* name) {
  return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

class mpv_event_handler {
public:
  enum mpv_event_t {
    MPV_NO_EVENT = 0,
    MPV_ON_STATE_EVENT = 1<<0,
    MPV_ON_RENDER_EVENT = 1<<1,
  };

public:
  static void on_mpv_events(void* ctx) {
    auto& self = *static_cast<mpv_event_handler*>(ctx);

    std::scoped_lock l{self._mtx};
    self._events = (mpv_event_t)(self._events | MPV_ON_STATE_EVENT);
  }

  static void on_mpv_render_update(void* ctx) {
    auto& self = *static_cast<mpv_event_handler*>(ctx);

    std::scoped_lock l{self._mtx};
    self._events = (mpv_event_t)(self._events | MPV_ON_RENDER_EVENT);
  }

public:
  bool poll(mpv_handle* mpv, mpv_render_context* mpv_gl) {
    bool redraw = false;
    {
      std::scoped_lock l{_mtx};
      if (_events & MPV_ON_STATE_EVENT) {
        for (;;) {
          mpv_event* ev = mpv_wait_event(mpv, 0);
          if (ev->event_id == MPV_EVENT_NONE) {
            break;
          }
          if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
            auto* msg = static_cast<mpv_event_log_message*>(ev->data);
            ntf::logger::debug("mpv log: {}", msg->text);
          }
          ntf::logger::debug("mpv event: {}", mpv_event_name(ev->event_id));
        }
      }
      if (_events & MPV_ON_RENDER_EVENT) {
        ntf::uint64 flags = mpv_render_context_update(mpv_gl);
        if (flags & MPV_RENDER_UPDATE_FRAME) {
          redraw = true;
        }
      }
      _events = MPV_NO_EVENT;
    }
    return redraw;
  }

private:
  std::mutex _mtx;
  mpv_event_t _events;
};

int main(int argc, const char* argv[]) {
  ntf::logger::set_level(ntf::log_level::debug);

  const char* path;
  if (argc != 2) {
    path = "./res/two_time.webm";
  } else {
    path = argv[1];
  }

  mpv_handle* mpv = mpv_create();
  if (!mpv) {
    die("Failed to create mpv context");
  }

  // mpv_request_log_messages(mpv, "debug");
  // mpv_set_option_string(mpv, "terminal", "yes");
  mpv_set_option_string(mpv, "config", "no"); // do not load anything from ~/.config/mpv/
  mpv_set_option_string(mpv, "gpu-api", "opengl");
  mpv_set_option_string(mpv, "vd-lavc-dr", "yes");
  mpv_set_option_string(mpv, "hwdec", "auto");
  mpv_set_option_string(mpv, "vo", "libmpv");
  mpv_set_option_string(mpv, "loop", "");
  mpv_set_option_string(mpv, "load-unsafe-playlists", "");
  mpv_set_option_string(mpv, "load-scripts", "no");
  mpv_set_option_string(mpv, "interpolation", "yes");
  mpv_set_option_string(mpv, "video-sync", "display-resample");
  mpv_set_option_string(mpv, "video-timing-offset", "0"); //fixes FPS locked to video FPS
  // mpv_set_option_string(mpv, "msg-level", "all=debug");

  if (mpv_initialize(mpv) < 0) {
    die("Failed to init mpv");
  }

  const ntf::win_gl_params gl_params {
    .ver_major = 4,
    .ver_minor = 3,
  };
  auto win = ntf::renderer_window::create({
    .width = 1280,
    .height = 720,
    .title = "test - shogle_mpv",
    .x11_class_name = "shogle_mpv",
    .x11_instance_name = nullptr,
    .ctx_params = gl_params,
  }).value();
  auto ctx = ntf::renderer_context::create({
    .window = win.handle(),
    .renderer_api = win.renderer(),
    .swap_interval = 0,
    .fb_viewport = {0, 0, 1280, 720},
    .fb_clear = ntf::r_clear_flag::none,
    .fb_color = {.3f, .3f, .3f, 1.f},
    .alloc = nullptr,
  }).value();
  auto imgui = ntf::imgui_ctx::create(ctx.handle());

  mpv_opengl_init_params mpv_gl_params {
    .get_proc_address = &mpv_proc_address,
    .get_proc_address_ctx = nullptr,
  };
  int mpv_yes = 1;
  mpv_render_param mpv_params[] = {
    {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL},
    {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &mpv_gl_params},
    {MPV_RENDER_PARAM_ADVANCED_CONTROL, &mpv_yes},
    {MPV_RENDER_PARAM_INVALID, nullptr},
  };
  mpv_render_context* mpv_gl;
  if (mpv_render_context_create(&mpv_gl, mpv, mpv_params) < 0) {
    die("Failed to init mpv GL context");
  }

  mpv_event_handler event_handler;
  mpv_set_wakeup_callback(mpv, mpv_event_handler::on_mpv_events, &event_handler);
  mpv_render_context_set_update_callback(mpv_gl,
                                         mpv_event_handler::on_mpv_render_update, &event_handler);


  auto mpv_render = [&](ntf::r_context, ntf::r_platform_handle fbo) {
    const auto win_sz = win.fb_size();
    int w = (int)win_sz.x, h = (int)win_sz.y;
    mpv_opengl_fbo gl_fbo {
      .fbo = (int)fbo,
      .w = w,
      .h = h,
      .internal_format = 0,
    };
    mpv_render_param render_params[] {
      {MPV_RENDER_PARAM_OPENGL_FBO, &gl_fbo},
      {MPV_RENDER_PARAM_FLIP_Y, &mpv_yes},
      {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(mpv_gl, render_params);
  };

  auto mpv_pause = [&]() {
    const char *cmd_pause[] = {"cycle", "pause", nullptr};
    mpv_command_async(mpv, 0, cmd_pause);
  };

  auto mpv_reset = [&]() {
    const char* cmd_replay[] = {"seek", "0", "absolute", nullptr};
    mpv_command_async(mpv, 0, cmd_replay);
  };

  auto fbo = ntf::renderer_framebuffer::default_fbo(ctx);
  win.set_viewport_callback([&](auto&, ntf::extent2d ext) {
    fbo.viewport({0, 0, ext.x, ext.y});
  });

  win.set_key_press_callback([&](auto&, const ntf::win_key_data& k) {
    if (k.action == ntf::win_action::press) {
      if (k.key == ntf::win_key::space) {
        mpv_pause();
      }
      if (k.key == ntf::win_key::backspace) {
        mpv_reset();
      }
      if (k.key == ntf::win_key::escape) {
        win.close();
      }
    }
  });

  const char* cmd[] = {"loadfile", path, nullptr};
  mpv_command_async(mpv, 0, cmd);

  ntf::shogle_render_loop(win, ctx, [&](float) {
    mpv_render_context_report_swap(mpv_gl);
    const bool mpv_redraw = event_handler.poll(mpv, mpv_gl);

    imgui.start_frame();
    const auto gui_flags =
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize;
    ImGui::Begin("controls", nullptr, gui_flags);
      ImGui::SetWindowPos(ImVec2{0, 0});
      ImGui::SetWindowSize(ImVec2{80, 80});
      if (ImGui::Button("pause")) {
        mpv_pause();
      }
      if (ImGui::Button("reset")) {
        mpv_reset();
      }
    ImGui::End();
    imgui.end_frame();


    if (mpv_redraw) {
      ntf::r_submit_external_command(ctx.handle(), {
        .target = fbo.handle(),
        .state = { // Disable all tests
          .primitive = ntf::r_primitive::triangles,
          .poly_mode = ntf::r_polygon_mode::fill,
          .poly_width = ntf::nullopt,
          .stencil_test = nullptr,
          .depth_test = nullptr, // <- Important!!!
          .scissor_test = nullptr,
          .face_culling = nullptr,
          .blending = nullptr,
        },
        .callback = mpv_render,
      });
    }
  });

  mpv_render_context_free(mpv_gl);
  mpv_destroy(mpv);

  ntf::logger::info("byebye!");
  return 0;
}
