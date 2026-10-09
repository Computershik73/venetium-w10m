// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The exports of venetium_engine.dll, other than venetium_w10m_run
// (app/vw_main.cc). Each one is safe from any thread: it either reads an
// atomic the browser keeps current, or posts its work to the browser UI
// thread through VwEngine -- queued until the browser has started.

#include "venetium_w10m/capi/venetium_w10m_capi.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "venetium_w10m/browser/vw_browser.h"
#include "venetium_w10m/browser/vw_browser_main_parts.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_frame_sink.h"
#include "venetium_w10m/browser/vw_platform.h"
#include "venetium_w10m/browser/vw_tab.h"
#include "venetium_w10m/common/vw_log.h"

using venetium_w10m::VwBrowser;
using venetium_w10m::VwEngine;

namespace {

std::string Text(const char* utf8) {
  return utf8 ? std::string(utf8) : std::string();
}

std::u16string Text16(const uint16_t* text, int32_t length) {
  if (!text || length <= 0) {
    return std::u16string();
  }
  return std::u16string(reinterpret_cast<const char16_t*>(text),
                        static_cast<size_t>(length));
}

void Post(VwEngine::BrowserTask task) {
  VwEngine::Get()->PostToBrowser(std::move(task));
}

}  // namespace

extern "C" {

VW_EXPORT void venetium_w10m_set_logger(venetium_w10m_log_fn fn) {
  venetium_w10m::SetShellLogger(fn);
}

VW_EXPORT void venetium_w10m_quit(void) {
  Post(base::BindOnce(
      [](VwBrowser*) { venetium_w10m::QuitBrowserMainLoop(); }));
}

VW_EXPORT void venetium_w10m_app_state(int32_t suspended) {
  Post(base::BindOnce(
      [](bool suspended, VwBrowser* browser) {
        browser->AppStateChanged(suspended);
      },
      suspended != 0));
}

VW_EXPORT void venetium_w10m_memory_pressure(int32_t level) {
  Post(base::BindOnce(
      [](int32_t level, VwBrowser* browser) { browser->MemoryPressure(level); },
      level));
}

VW_EXPORT int32_t venetium_w10m_frame_copy(void* dest,
                                           int32_t capacity,
                                           int32_t* width,
                                           int32_t* height,
                                           uint64_t* serial) {
  return venetium_w10m::FrameBuffer::Get()->Copy(dest, capacity, width, height,
                                                 serial);
}

VW_EXPORT void venetium_w10m_resize(int32_t width, int32_t height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  const gfx::Size size(width, height);
  VwEngine::Get()->UpdateSize(size);
  Post(base::BindOnce(
      [](gfx::Size size, VwBrowser* browser) { browser->Resize(size); },
      size));
}

VW_EXPORT void venetium_w10m_set_screen(int32_t width, int32_t height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  const gfx::Size screen(width, height);
  VwEngine::Get()->UpdateScreen(screen);
  Post(base::BindOnce(
      [](gfx::Size screen, VwBrowser* browser) {
        browser->platform()->SetScreen(screen);
      },
      screen));
}

VW_EXPORT void venetium_w10m_set_dpi(int32_t dpi) {
  Post(base::BindOnce(
      [](int32_t dpi, VwBrowser* browser) { browser->platform()->SetDpi(dpi); },
      dpi));
}

VW_EXPORT void venetium_w10m_set_scale(double scale) {
  if (scale <= 0) {
    return;
  }
  VwEngine::Get()->UpdateScale(scale);
  Post(base::BindOnce(
      [](double scale, VwBrowser* browser) {
        browser->platform()->SetScale(static_cast<float>(scale));
      },
      scale));
}

VW_EXPORT void venetium_w10m_set_keyboard_inset(int32_t pixels) {
  Post(base::BindOnce(
      [](int32_t pixels, VwBrowser* browser) {
        browser->SetKeyboardInset(pixels);
      },
      pixels));
}

VW_EXPORT void venetium_w10m_input_touch(int32_t pointer_id,
                                         int32_t state,
                                         int32_t x,
                                         int32_t y,
                                         int64_t time_qpc) {
  Post(base::BindOnce(
      [](int32_t id, int32_t state, int32_t x, int32_t y, int64_t time,
         VwBrowser* browser) {
        browser->platform()->Touch(id, state, x, y, time);
      },
      pointer_id, state, x, y, time_qpc));
}

VW_EXPORT void venetium_w10m_input_mouse(int32_t message, int32_t x, int32_t y) {
  Post(base::BindOnce(
      [](int32_t message, int32_t x, int32_t y, VwBrowser* browser) {
        browser->platform()->Mouse(message, x, y);
      },
      message, x, y));
}

VW_EXPORT void venetium_w10m_input_wheel(int32_t x,
                                         int32_t y,
                                         double dx,
                                         double dy) {
  Post(base::BindOnce(
      [](int32_t x, int32_t y, double dx, double dy, VwBrowser* browser) {
        browser->platform()->Wheel(x, y, dx, dy);
      },
      x, y, dx, dy));
}

VW_EXPORT void venetium_w10m_input_key(int32_t vkey,
                                       int32_t down,
                                       uint32_t modifiers) {
  Post(base::BindOnce(
      [](int32_t vkey, bool down, uint32_t modifiers, VwBrowser* browser) {
        browser->platform()->Key(vkey, down, modifiers);
      },
      vkey, down != 0, modifiers));
}

VW_EXPORT void venetium_w10m_input_text(const uint16_t* text, int32_t length) {
  std::u16string utf16 = Text16(text, length);
  if (utf16.empty()) {
    return;
  }
  Post(base::BindOnce(
      [](std::u16string text, VwBrowser* browser) {
        browser->platform()->Text(text);
      },
      std::move(utf16)));
}

VW_EXPORT void venetium_w10m_input_composition(const uint16_t* text,
                                               int32_t length) {
  Post(base::BindOnce(
      [](std::u16string text, VwBrowser* browser) {
        browser->platform()->Composition(text);
      },
      Text16(text, length)));
}

VW_EXPORT int32_t venetium_w10m_text_input_wanted(void) {
  return VwEngine::Get()->text_input_wanted.load();
}

VW_EXPORT uint32_t venetium_w10m_text_input_state(void) {
  return VwEngine::Get()->text_input_state.load();
}

VW_EXPORT int32_t venetium_w10m_tab_create(const char* url, int32_t select) {
  VwEngine* engine = VwEngine::Get();
  const int32_t id = engine->NextTabId();
  Post(base::BindOnce(
      [](int32_t id, std::string text, bool select, VwBrowser* browser) {
        browser->CreateTab(id, VwBrowser::ResolveText(text), select, 0);
      },
      id, Text(url), select != 0));
  return id;
}

VW_EXPORT void venetium_w10m_tab_close(int32_t tab) {
  Post(base::BindOnce(
      [](int32_t tab, VwBrowser* browser) { browser->CloseTab(tab); }, tab));
}

VW_EXPORT void venetium_w10m_tab_select(int32_t tab) {
  Post(base::BindOnce(
      [](int32_t tab, VwBrowser* browser) { browser->SelectTab(tab); }, tab));
}

VW_EXPORT void venetium_w10m_tab_navigate(int32_t tab, const char* text) {
  Post(base::BindOnce(
      [](int32_t tab, std::string text, VwBrowser* browser) {
        browser->NavigateText(tab, text);
      },
      tab, Text(text)));
}

namespace {

void WithTab(int32_t id, void (venetium_w10m::VwTab::*action)()) {
  Post(base::BindOnce(
      [](int32_t id, void (venetium_w10m::VwTab::*action)(),
         VwBrowser* browser) {
        venetium_w10m::VwTab* tab = id ? browser->tab(id) : browser->selected();
        if (tab) {
          (tab->*action)();
        }
      },
      id, action));
}

}  // namespace

VW_EXPORT void venetium_w10m_tab_back(int32_t tab) {
  WithTab(tab, &venetium_w10m::VwTab::GoBack);
}

VW_EXPORT void venetium_w10m_tab_forward(int32_t tab) {
  WithTab(tab, &venetium_w10m::VwTab::GoForward);
}

VW_EXPORT void venetium_w10m_tab_reload(int32_t tab) {
  WithTab(tab, &venetium_w10m::VwTab::Reload);
}

VW_EXPORT void venetium_w10m_tab_stop(int32_t tab) {
  WithTab(tab, &venetium_w10m::VwTab::Stop);
}

VW_EXPORT int32_t venetium_w10m_back_button(void) {
  VwEngine* engine = VwEngine::Get();
  if (!engine->browser_running() || !engine->selected_can_go_back.load()) {
    return 0;
  }
  Post(base::BindOnce([](VwBrowser* browser) { browser->BackButton(); }));
  return 1;
}

VW_EXPORT int32_t venetium_w10m_open_url(const char* url) {
  VwEngine* engine = VwEngine::Get();
  if (!engine->browser_running()) {
    return 0;
  }
  const int32_t id = engine->NextTabId();
  Post(base::BindOnce(
      [](int32_t id, std::string text, VwBrowser* browser) {
        browser->CreateTab(id, VwBrowser::ResolveText(text), true, 0);
      },
      id, Text(url)));
  return 1;
}

VW_EXPORT void venetium_w10m_set_search_url(const char* url_template) {
  if (url_template && *url_template) {
    VwEngine::Get()->SetSearchTemplate(url_template);
  }
}

VW_EXPORT void venetium_w10m_set_desktop_mode(int32_t desktop) {
  VwEngine::Get()->desktop_mode.store(desktop != 0);
  Post(base::BindOnce(
      [](bool desktop, VwBrowser* browser) {
        browser->SetDesktopMode(desktop);
      },
      desktop != 0));
}

VW_EXPORT void venetium_w10m_set_event_sink(const venetium_w10m_events* sink) {
  VwEngine::Get()->SetEventSink(sink);
}

VW_EXPORT void venetium_w10m_dialog_reply(int32_t id,
                                          int32_t accepted,
                                          const char* text) {
  Post(base::BindOnce(
      [](int32_t id, bool accepted, std::string text, VwBrowser* browser) {
        browser->dialogs()->Reply(id, accepted, text);
      },
      id, accepted != 0, Text(text)));
}

VW_EXPORT void venetium_w10m_permission_reply(int32_t id, int32_t granted) {
  // Permissions are not routed to the shell yet (vw_permission_manager.h).
  venetium_w10m::NoteF("permission %d answered %d", id, granted);
}

VW_EXPORT void venetium_w10m_file_reply(int32_t id, const char* paths) {
  Post(base::BindOnce(
      [](int32_t id, std::string paths, VwBrowser* browser) {
        browser->dialogs()->FilesChosen(id, paths);
      },
      id, Text(paths)));
}

VW_EXPORT void venetium_w10m_clipboard_phone_text(const char* text) {
  Post(base::BindOnce(
      [](std::string text, VwBrowser* browser) {
        browser->clipboard()->SetPhoneText(text);
      },
      Text(text)));
}

}  // extern "C"
