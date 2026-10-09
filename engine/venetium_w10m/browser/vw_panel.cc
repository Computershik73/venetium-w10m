// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_panel.h"

#include <windows.h>

#include <dxgi1_3.h>
// winbase.h's GetCurrentTime() macro would rewrite XAML's Storyboard method.
#undef GetCurrentTime
#include <windows.ui.core.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/implements.h>

#include <functional>
#include <memory>
#include <utility>

#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

using Microsoft::WRL::ComPtr;
namespace Core = ABI::Windows::UI::Core;
namespace Controls = ABI::Windows::UI::Xaml::Controls;

// How long an engine thread waits for the XAML thread. It never waits on
// the engine, so this only runs out if the app is being torn down.
constexpr DWORD kUiTripTimeoutMs = 2000;

struct State {
  base::Lock lock;
  // The panel's own interfaces: used on its thread only.
  ComPtr<ISwapChainPanelNative> native;
  ComPtr<Controls::ISwapChainPanel> panel;
  // Agile.
  ComPtr<Core::ICoreDispatcher> dispatcher;
  // Under |lock|: the chain in the panel, its generation and the scale.
  ComPtr<IDXGISwapChain2> current;
  uint32_t generation = 0;
  float scale_x = 1.0f;
  float scale_y = 1.0f;
};

State& GetState() {
  static base::NoDestructor<State> state;
  return *state;
}

// Engine threads are not COM-initialised; WinRT calls want an apartment.
void EnsureMta() {
  thread_local bool done = false;
  if (!done) {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    done = true;
  }
}

HRESULT ApplyScale(IDXGISwapChain2* chain, float scale_x, float scale_y) {
  DXGI_MATRIX_3X2_F inverse = {};
  inverse._11 = 1.0f / (scale_x > 0 ? scale_x : 1.0f);
  inverse._22 = 1.0f / (scale_y > 0 ? scale_y : 1.0f);
  return chain->SetMatrixTransform(&inverse);
}

// One trip to the panel's thread. The work and its result live on the heap:
// after a timeout the trip may still run, and must not find a dead stack.
struct Trip {
  Trip() : done(::CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET,
                                 EVENT_ALL_ACCESS)) {}
  ~Trip() {
    if (done) {
      ::CloseHandle(done);
    }
  }
  HANDLE done;
  HRESULT result = E_PENDING;
  std::function<HRESULT()> work;
};

HRESULT RunOnUiThread(Core::ICoreDispatcher* dispatcher,
                      std::function<HRESULT()> work,
                      DWORD timeout_ms) {
  boolean access = FALSE;
  if (SUCCEEDED(dispatcher->get_HasThreadAccess(&access)) && access) {
    return work();
  }
  auto trip = std::make_shared<Trip>();
  if (!trip->done) {
    return E_FAIL;
  }
  trip->work = std::move(work);
  ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
  HRESULT hr = dispatcher->RunAsync(
      Core::CoreDispatcherPriority_Normal,
      Microsoft::WRL::Callback<Microsoft::WRL::Implements<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Core::IDispatchedHandler, Microsoft::WRL::FtmBase>>(
          [trip]() -> HRESULT {
            trip->result = trip->work();
            ::SetEvent(trip->done);
            return S_OK;
          })
          .Get(),
      &action);
  if (FAILED(hr)) {
    return hr;
  }
  if (::WaitForSingleObjectEx(trip->done, timeout_ms, FALSE) !=
      WAIT_OBJECT_0) {
    return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
  }
  return trip->result;
}

const char* KindName(PresentKind kind) {
  switch (kind) {
    case PresentKind::kD3D11:
      return "software frames through Direct3D 11";
    case PresentKind::kAngle:
      return "ANGLE";
    case PresentKind::kCompositor:
      return "the Direct3D 11 compositor";
    default:
      return "bitmap";
  }
}

}  // namespace

// static
PanelHost* PanelHost::Get() {
  static PanelHost host;
  return &host;
}

void PanelHost::SetPanel(IInspectable* raw) {
  State& state = GetState();
  if (!raw || has_panel_.load()) {
    return;
  }
  ComPtr<IInspectable> inspectable(raw);
  ComPtr<ISwapChainPanelNative> native;
  ComPtr<Controls::ISwapChainPanel> panel;
  ComPtr<ABI::Windows::UI::Xaml::IDependencyObject> dependency;
  ComPtr<Core::ICoreDispatcher> dispatcher;
  if (FAILED(inspectable.As(&native)) || FAILED(inspectable.As(&panel)) ||
      FAILED(inspectable.As(&dependency)) ||
      FAILED(dependency->get_Dispatcher(&dispatcher)) || !dispatcher) {
    Note("panel: what the shell handed over is not a SwapChainPanel");
    return;
  }
  // The scale can change while the app runs (a display setting); the
  // chain in the panel follows it.
  EventRegistrationToken token;
  panel->add_CompositionScaleChanged(
      Microsoft::WRL::Callback<ABI::Windows::Foundation::ITypedEventHandler<
          Controls::SwapChainPanel*, IInspectable*>>(
          [](Controls::ISwapChainPanel* sender, IInspectable*) -> HRESULT {
            float scale_x = 1.0f, scale_y = 1.0f;
            sender->get_CompositionScaleX(&scale_x);
            sender->get_CompositionScaleY(&scale_y);
            ComPtr<IDXGISwapChain2> chain;
            {
              State& state = GetState();
              base::AutoLock hold(state.lock);
              state.scale_x = scale_x;
              state.scale_y = scale_y;
              chain = state.current;
            }
            if (chain) {
              ApplyScale(chain.Get(), scale_x, scale_y);
            }
            NoteF("panel: composition scale %.3fx%.3f", scale_x, scale_y);
            return S_OK;
          })
          .Get(),
      &token);
  {
    base::AutoLock hold(state.lock);
    state.native = native;
    state.panel = panel;
    state.dispatcher = dispatcher;
  }
  has_panel_.store(true);
  Note("panel: the shell's SwapChainPanel is here");
}

uint32_t PanelHost::Attach(IDXGISwapChain1* chain, PresentKind kind) {
  State& state = GetState();
  if (!has_panel_.load() || !chain) {
    return 0;
  }
  EnsureMta();
  ComPtr<IDXGISwapChain1> keep(chain);
  ComPtr<ISwapChainPanelNative> native;
  ComPtr<Controls::ISwapChainPanel> panel;
  ComPtr<Core::ICoreDispatcher> dispatcher;
  uint32_t generation = 0;
  {
    base::AutoLock hold(state.lock);
    native = state.native;
    panel = state.panel;
    dispatcher = state.dispatcher;
    generation = ++state.generation;
  }
  auto scale = std::make_shared<std::pair<float, float>>(1.0f, 1.0f);
  const HRESULT hr = RunOnUiThread(
      dispatcher.Get(),
      [native, panel, keep, generation, scale]() -> HRESULT {
        {
          State& state = GetState();
          base::AutoLock hold(state.lock);
          if (state.generation != generation) {
            return E_ABORT;  // a newer chain, or the attempt was given up
          }
        }
        panel->get_CompositionScaleX(&scale->first);
        panel->get_CompositionScaleY(&scale->second);
        return native->SetSwapChain(keep.Get());
      },
      kUiTripTimeoutMs);
  if (FAILED(hr)) {
    {
      base::AutoLock hold(state.lock);
      if (state.generation == generation) {
        ++state.generation;  // a trip that still runs later finds it stale
      }
    }
    NoteF("panel: the swap chain was not taken (0x%08lx)",
          static_cast<unsigned long>(hr));
    return 0;
  }
  ComPtr<IDXGISwapChain2> chain2;
  HRESULT scaled = E_NOINTERFACE;
  if (SUCCEEDED(keep.As(&chain2))) {
    scaled = ApplyScale(chain2.Get(), scale->first, scale->second);
  }
  {
    base::AutoLock hold(state.lock);
    state.current = chain2;
    state.scale_x = scale->first;
    state.scale_y = scale->second;
  }
  presenting_.store(static_cast<int32_t>(kind));
  NoteF("panel: chain %u shows %s, scale %.3fx%.3f, inverse transform 0x%08lx",
        generation, KindName(kind), scale->first, scale->second,
        static_cast<unsigned long>(scaled));
  return generation;
}

void PanelHost::Detach(uint32_t generation) {
  State& state = GetState();
  ComPtr<ISwapChainPanelNative> native;
  ComPtr<Core::ICoreDispatcher> dispatcher;
  uint32_t cleared = 0;
  {
    base::AutoLock hold(state.lock);
    if (!generation || state.generation != generation) {
      return;
    }
    cleared = ++state.generation;
    state.current.Reset();
    native = state.native;
    dispatcher = state.dispatcher;
  }
  presenting_.store(static_cast<int32_t>(PresentKind::kBitmap));
  NoteF("panel: chain %u taken out", generation);
  if (!dispatcher) {
    return;
  }
  EnsureMta();
  ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
  dispatcher->RunAsync(
      Core::CoreDispatcherPriority_Normal,
      Microsoft::WRL::Callback<Microsoft::WRL::Implements<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Core::IDispatchedHandler, Microsoft::WRL::FtmBase>>(
          [native, cleared]() -> HRESULT {
            {
              State& state = GetState();
              base::AutoLock hold(state.lock);
              if (state.generation != cleared) {
                return S_OK;  // a newer chain is in the panel by now
              }
            }
            native->SetSwapChain(nullptr);
            return S_OK;
          })
          .Get(),
      &action);
}

void PanelHost::Presented(uint32_t generation) {
  State& state = GetState();
  {
    base::AutoLock hold(state.lock);
    if (state.generation != generation) {
      return;
    }
  }
  presented_frames_.fetch_add(1);
}

}  // namespace venetium_w10m
