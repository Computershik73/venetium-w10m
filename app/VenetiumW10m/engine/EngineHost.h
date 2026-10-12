// EngineHost.h -- venetium_engine.dll as the shell sees it.
//
// The engine is loaded by name (LoadPackagedLibrary) and every entry point is
// resolved with GetProcAddress, never linked: the shell is built with cl.exe
// and C++/WinRT, the engine with clang-cl inside the Chromium tree, and the
// shell has to come up -- and show a log -- even when the engine cannot load.
//
// venetium_w10m_run takes over a thread of its own and becomes Chromium's
// browser UI thread. The engine's events arrive on engine threads and are
// posted to the XAML thread here before the page sees them.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

// From engine/venetium_w10m/capi (on the include path of the shell build).
#include "venetium_w10m_capi.h"

namespace venetium_w10m::engine {

// The engine's exports, resolved once. A null member means the engine does
// not have that one (an older engine with a newer shell).
struct Api {
  decltype(&venetium_w10m_set_logger) set_logger = nullptr;
  decltype(&venetium_w10m_run) run = nullptr;
  decltype(&venetium_w10m_quit) quit = nullptr;
  decltype(&venetium_w10m_app_state) app_state = nullptr;
  decltype(&venetium_w10m_memory_pressure) memory_pressure = nullptr;
  decltype(&venetium_w10m_memory_status) memory_status = nullptr;
  decltype(&venetium_w10m_frame_copy) frame_copy = nullptr;
  decltype(&venetium_w10m_resize) resize = nullptr;
  decltype(&venetium_w10m_set_screen) set_screen = nullptr;
  decltype(&venetium_w10m_set_dpi) set_dpi = nullptr;
  decltype(&venetium_w10m_set_scale) set_scale = nullptr;
  decltype(&venetium_w10m_set_keyboard_inset) set_keyboard_inset = nullptr;
  decltype(&venetium_w10m_input_touch) input_touch = nullptr;
  decltype(&venetium_w10m_input_mouse) input_mouse = nullptr;
  decltype(&venetium_w10m_input_wheel) input_wheel = nullptr;
  decltype(&venetium_w10m_input_key) input_key = nullptr;
  decltype(&venetium_w10m_input_text) input_text = nullptr;
  decltype(&venetium_w10m_input_composition) input_composition = nullptr;
  decltype(&venetium_w10m_text_input_wanted) text_input_wanted = nullptr;
  decltype(&venetium_w10m_text_input_state) text_input_state = nullptr;
  decltype(&venetium_w10m_tab_create) tab_create = nullptr;
  decltype(&venetium_w10m_tab_close) tab_close = nullptr;
  decltype(&venetium_w10m_tab_select) tab_select = nullptr;
  decltype(&venetium_w10m_tab_navigate) tab_navigate = nullptr;
  decltype(&venetium_w10m_tab_back) tab_back = nullptr;
  decltype(&venetium_w10m_tab_forward) tab_forward = nullptr;
  decltype(&venetium_w10m_tab_reload) tab_reload = nullptr;
  decltype(&venetium_w10m_tab_stop) tab_stop = nullptr;
  decltype(&venetium_w10m_back_button) back_button = nullptr;
  decltype(&venetium_w10m_open_url) open_url = nullptr;
  decltype(&venetium_w10m_set_search_url) set_search_url = nullptr;
  decltype(&venetium_w10m_set_desktop_mode) set_desktop_mode = nullptr;
  decltype(&venetium_w10m_set_event_sink) set_event_sink = nullptr;
  decltype(&venetium_w10m_dialog_reply) dialog_reply = nullptr;
  decltype(&venetium_w10m_permission_reply) permission_reply = nullptr;
  decltype(&venetium_w10m_file_reply) file_reply = nullptr;
  decltype(&venetium_w10m_clipboard_phone_text) clipboard_phone_text = nullptr;
  decltype(&venetium_w10m_set_panel) set_panel = nullptr;
  decltype(&venetium_w10m_presenting) presenting = nullptr;
  decltype(&venetium_w10m_presented_frames) presented_frames = nullptr;
};

// What the page wants to hear about. Every handler runs on the XAML thread.
struct Events {
  std::function<void(int32_t tab, std::wstring url, std::wstring title,
                     float progress, bool loading, bool canBack,
                     bool canForward, bool secure)>
      tabState;
  std::function<void(int32_t tab, int32_t opener, bool selected)> tabOpened;
  std::function<void(int32_t tab)> tabClosed;
  std::function<void(int32_t tab)> tabSelected;
  std::function<void(bool on)> fullscreen;
  std::function<void(std::wstring uri)> launchUri;
  std::function<void(std::wstring path, std::wstring mime)> downloadDone;
  std::function<void(int32_t id, int32_t kind, std::wstring message,
                     std::wstring defaultText)>
      dialog;
  std::function<void(int32_t id, std::wstring accept, bool multiple,
                     bool save)>
      fileChooser;
  std::function<void(std::wstring text)> clipboardSet;
  // venetium_w10m_run returned: the engine has ended, for good.
  std::function<void(int32_t code)> engineStopped;
};

struct StartParams {
  std::wstring installDir;
  std::wstring profileDir;
  std::wstring cacheDir;
  std::wstring tempDir;
  int32_t width = 0;
  int32_t height = 0;
  double scale = 1.0;
  bool jit = false;
  std::string switches;
  std::string initialUrl;
};

class EngineHost {
 public:
  // The one engine of the process.
  static EngineHost& Get();

  // Loads the DLL and resolves its exports; false (and a log line saying
  // why) when it cannot. The XAML thread's dispatcher receives the events.
  bool Load(winrt::Windows::UI::Core::CoreDispatcher const& dispatcher);
  bool loaded() const { return module_ != nullptr; }
  const Api& api() const { return api_; }

  void SetEvents(Events events) { events_ = std::move(events); }

  // Starts the engine on a thread of its own. Returns at once.
  bool Start(const StartParams& params);
  bool started() const { return started_; }

  // Engine thread: venetium_w10m_run returned |code|.
  void OnRunReturned(int32_t code);

 private:
  EngineHost() = default;

  // Runs |work| on the XAML thread.
  void Post(std::function<void()> work);

  static void OnTabState(int32_t tab, const char* url, const char* title,
                         float progress, int32_t loading, int32_t can_back,
                         int32_t can_forward, int32_t secure);
  static void OnTabOpened(int32_t tab, int32_t opener, int32_t selected);
  static void OnTabClosed(int32_t tab);
  static void OnTabSelected(int32_t tab);
  static void OnFullscreen(int32_t on);
  static void OnLaunchUri(const char* uri);
  static void OnDownloadDone(const char* path, const char* mime);
  static void OnDialog(int32_t id, int32_t kind, const char* message,
                       const char* default_text);
  static void OnPermission(int32_t id, int32_t kind, const char* origin);
  static void OnFileChooser(int32_t id, const char* accept, int32_t multiple,
                            int32_t mode);
  static void OnClipboardSet(const char* text);

  HMODULE module_ = nullptr;
  Api api_;
  Events events_;
  winrt::Windows::UI::Core::CoreDispatcher dispatcher_{nullptr};
  bool started_ = false;
};

std::wstring Widen(const char* utf8);
std::string Narrow(std::wstring_view text);

}  // namespace venetium_w10m::engine
