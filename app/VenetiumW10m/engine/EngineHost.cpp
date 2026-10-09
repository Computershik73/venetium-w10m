// EngineHost.cpp
#include "pch.h"

#include "engine/EngineHost.h"

#include "client/Log.h"
#include "engine/ImportProbe.h"

namespace venetium_w10m::engine {

using client::Log;

std::wstring Widen(const char* utf8) {
  if (!utf8 || !*utf8) return std::wstring();
  int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  if (n <= 1) return std::wstring();
  std::wstring out(static_cast<size_t>(n - 1), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), n);
  return out;
}

std::string Narrow(std::wstring_view text) {
  if (text.empty()) return std::string();
  int n = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(),
                                nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(n), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(),
                        n, nullptr, nullptr);
  return out;
}

EngineHost& EngineHost::Get() {
  static EngineHost host;
  return host;
}

namespace {

template <typename T>
void Resolve(HMODULE module, const char* name, T& slot, int& missing) {
  slot = reinterpret_cast<T>(::GetProcAddress(module, name));
  if (!slot) {
    ++missing;
    Log::Write(L"engine: export missing", Widen(name));
  }
}

struct ThreadArgs {
  StartParams params;
  decltype(&venetium_w10m_run) run;
};

DWORD WINAPI EngineThread(LPVOID raw) {
  std::unique_ptr<ThreadArgs> args(static_cast<ThreadArgs*>(raw));
  const StartParams& p = args->params;
  venetium_w10m_config config{};
  config.install_dir = p.installDir.c_str();
  config.profile_dir = p.profileDir.c_str();
  config.cache_dir = p.cacheDir.c_str();
  config.temp_dir = p.tempDir.c_str();
  config.width = p.width;
  config.height = p.height;
  config.scale = p.scale;
  config.jit = p.jit ? 1 : 0;
  config.switches = p.switches.c_str();
  config.initial_url = p.initialUrl.c_str();
  Log::Write(L"engine: thread started, entering venetium_w10m_run");
  const int32_t rc = args->run(&config);
  Log::WriteNum(L"engine: venetium_w10m_run returned", rc);
  EngineHost::Get().OnRunReturned(rc);
  return static_cast<DWORD>(rc);
}

}  // namespace

bool EngineHost::Load(winrt::Windows::UI::Core::CoreDispatcher const& dispatcher) {
  if (module_) return true;
  dispatcher_ = dispatcher;
  const ULONGLONG started = ::GetTickCount64();
  module_ = ::LoadPackagedLibrary(L"venetium_engine.dll", 0);
  if (!module_) {
    const DWORD error = ::GetLastError();
    Log::WriteNum(L"engine: LoadPackagedLibrary(venetium_engine.dll) failed, "
                  L"error",
                  static_cast<long long>(error));
    // 126/127 name nothing: ask the phone for each import, the loader's way.
    ProbeImports(L"venetium_engine.dll");
    return false;
  }
  Log::WriteNum(L"engine: venetium_engine.dll loaded in ms",
                static_cast<long long>(::GetTickCount64() - started));
  int missing = 0;
#define VW_RESOLVE(field) \
  Resolve(module_, "venetium_w10m_" #field, api_.field, missing)
  VW_RESOLVE(set_logger);
  VW_RESOLVE(run);
  VW_RESOLVE(quit);
  VW_RESOLVE(app_state);
  VW_RESOLVE(memory_pressure);
  VW_RESOLVE(frame_copy);
  VW_RESOLVE(resize);
  VW_RESOLVE(set_screen);
  VW_RESOLVE(set_dpi);
  VW_RESOLVE(set_scale);
  VW_RESOLVE(set_keyboard_inset);
  VW_RESOLVE(input_touch);
  VW_RESOLVE(input_mouse);
  VW_RESOLVE(input_wheel);
  VW_RESOLVE(input_key);
  VW_RESOLVE(input_text);
  VW_RESOLVE(input_composition);
  VW_RESOLVE(text_input_wanted);
  VW_RESOLVE(text_input_state);
  VW_RESOLVE(tab_create);
  VW_RESOLVE(tab_close);
  VW_RESOLVE(tab_select);
  VW_RESOLVE(tab_navigate);
  VW_RESOLVE(tab_back);
  VW_RESOLVE(tab_forward);
  VW_RESOLVE(tab_reload);
  VW_RESOLVE(tab_stop);
  VW_RESOLVE(back_button);
  VW_RESOLVE(open_url);
  VW_RESOLVE(set_search_url);
  VW_RESOLVE(set_desktop_mode);
  VW_RESOLVE(set_event_sink);
  VW_RESOLVE(dialog_reply);
  VW_RESOLVE(permission_reply);
  VW_RESOLVE(file_reply);
  VW_RESOLVE(clipboard_phone_text);
  VW_RESOLVE(set_panel);
  VW_RESOLVE(presenting);
  VW_RESOLVE(presented_frames);
#undef VW_RESOLVE
  if (!api_.run || !api_.frame_copy) {
    Log::Write(L"engine: the DLL has no venetium_w10m_run/frame_copy");
    ::FreeLibrary(module_);
    module_ = nullptr;
    return false;
  }
  if (missing) {
    Log::WriteNum(L"engine: exports missing", missing);
  }
  if (api_.set_logger) {
    api_.set_logger(&Log::WriteUtf8);
  }
  static const venetium_w10m_events kEvents = {
      &EngineHost::OnTabState,    &EngineHost::OnTabOpened,
      &EngineHost::OnTabClosed,   &EngineHost::OnTabSelected,
      &EngineHost::OnFullscreen,  &EngineHost::OnLaunchUri,
      &EngineHost::OnDownloadDone, &EngineHost::OnDialog,
      &EngineHost::OnPermission,  &EngineHost::OnFileChooser,
      &EngineHost::OnClipboardSet,
  };
  if (api_.set_event_sink) {
    api_.set_event_sink(&kEvents);
  }
  return true;
}

bool EngineHost::Start(const StartParams& params) {
  if (!module_ || started_) return started_;
  auto* args = new ThreadArgs{params, api_.run};
  // 8 MB reserved, not committed: Chromium's browser UI thread runs deep
  // (layout, V8 in single-process mode), and a phone's default 1 MB is not
  // enough for it.
  HANDLE thread = ::CreateThread(nullptr, 8 * 1024 * 1024, &EngineThread, args,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (!thread) {
    Log::WriteNum(L"engine: CreateThread failed, error",
                  static_cast<long long>(::GetLastError()));
    delete args;
    return false;
  }
  ::CloseHandle(thread);
  started_ = true;
  return true;
}

void EngineHost::OnRunReturned(int32_t code) {
  Post([code]() {
    EngineHost& host = Get();
    host.started_ = false;
    if (host.events_.engineStopped) host.events_.engineStopped(code);
  });
}

void EngineHost::Post(std::function<void()> work) {
  if (!dispatcher_) return;
  dispatcher_.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                       [work = std::move(work)]() { work(); });
}

// --- events from the engine (engine threads) -----------------------------

void EngineHost::OnTabState(int32_t tab, const char* url, const char* title,
                            float progress, int32_t loading, int32_t can_back,
                            int32_t can_forward, int32_t secure) {
  auto& self = Get();
  self.Post([tab, url = Widen(url), title = Widen(title), progress, loading,
             can_back, can_forward, secure]() {
    if (Get().events_.tabState) {
      Get().events_.tabState(tab, url, title, progress, loading != 0,
                             can_back != 0, can_forward != 0, secure != 0);
    }
  });
}

void EngineHost::OnTabOpened(int32_t tab, int32_t opener, int32_t selected) {
  Get().Post([tab, opener, selected]() {
    if (Get().events_.tabOpened) Get().events_.tabOpened(tab, opener, selected != 0);
  });
}

void EngineHost::OnTabClosed(int32_t tab) {
  Get().Post([tab]() {
    if (Get().events_.tabClosed) Get().events_.tabClosed(tab);
  });
}

void EngineHost::OnTabSelected(int32_t tab) {
  Get().Post([tab]() {
    if (Get().events_.tabSelected) Get().events_.tabSelected(tab);
  });
}

void EngineHost::OnFullscreen(int32_t on) {
  Get().Post([on]() {
    if (Get().events_.fullscreen) Get().events_.fullscreen(on != 0);
  });
}

void EngineHost::OnLaunchUri(const char* uri) {
  Get().Post([uri = Widen(uri)]() {
    if (Get().events_.launchUri) Get().events_.launchUri(uri);
  });
}

void EngineHost::OnDownloadDone(const char* path, const char* mime) {
  Get().Post([path = Widen(path), mime = Widen(mime)]() {
    if (Get().events_.downloadDone) Get().events_.downloadDone(path, mime);
  });
}

void EngineHost::OnDialog(int32_t id, int32_t kind, const char* message,
                          const char* default_text) {
  Get().Post([id, kind, message = Widen(message),
              default_text = Widen(default_text)]() {
    if (Get().events_.dialog) {
      Get().events_.dialog(id, kind, message, default_text);
    } else if (Get().api_.dialog_reply) {
      Get().api_.dialog_reply(id, kind == 0 || kind == 3, "");
    }
  });
}

void EngineHost::OnPermission(int32_t id, int32_t kind, const char* origin) {
  Get().Post([id]() {
    if (Get().api_.permission_reply) Get().api_.permission_reply(id, 0);
  });
}

void EngineHost::OnFileChooser(int32_t id, const char* accept, int32_t multiple,
                               int32_t mode) {
  Get().Post([id, accept = Widen(accept), multiple, mode]() {
    if (Get().events_.fileChooser) {
      Get().events_.fileChooser(id, accept, multiple != 0, mode == 1);
    } else if (Get().api_.file_reply) {
      Get().api_.file_reply(id, "");
    }
  });
}

void EngineHost::OnClipboardSet(const char* text) {
  Get().Post([text = Widen(text)]() {
    if (Get().events_.clipboardSet) Get().events_.clipboardSet(text);
  });
}

}  // namespace venetium_w10m::engine
