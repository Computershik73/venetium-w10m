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
#include "base/time/time.h"
#include "ui/gl/w10m_panel.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

using Microsoft::WRL::ComPtr;
namespace Core = ABI::Windows::UI::Core;
namespace Controls = ABI::Windows::UI::Xaml::Controls;

// How long an engine thread waits for the XAML thread. It never waits on
// the engine, so this only runs out if the app is being torn down.
constexpr DWORD kUiTripTimeoutMs = 2000;

// How long one waits for the XAML thread to take a chain out when another is
// made right after it. Shorter than the wait to put one in: nothing is lost
// if the panel lets go a little late, and at the end of a run the XAML
// thread may not come back at all.
constexpr DWORD kUiDetachTimeoutMs = 500;

// How long the panel may be without a chain while one of the same kind is
// made to replace it, and still be said to show that kind.
constexpr int64_t kReplaceGraceMs = 1000;

// Milliseconds on TimeTicks' clock, and never 0.
int64_t NowMs() {
  return (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds() | 1;
}

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
  // The generation of ANGLE's chain, while it is in the panel.
  uint32_t angle_generation = 0;
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

// ANGLE, on the GPU thread, with a chain it made for the panel (or null,
// when its surface goes).
HRESULT __stdcall SetAngleSwapChain(void* context, IDXGISwapChain1* chain) {
  PanelHost* host = PanelHost::Get();
  State& state = GetState();
  if (!chain) {
    uint32_t generation = 0;
    {
      base::AutoLock hold(state.lock);
      generation = state.angle_generation;
      state.angle_generation = 0;
    }
    // ANGLE's surface is going. When Chromium makes the surface again (its
    // swap chain would not be resized, or the context was lost) the next
    // chain is made at once, on this thread: this one has to be out of the
    // panel by then, and the shell is not told the bitmap is back for the
    // moment in between.
    host->DetachToReplace(generation);
    return S_OK;
  }
  const uint32_t generation = host->Attach(chain, PresentKind::kAngle);
  if (!generation) {
    return E_FAIL;
  }
  base::AutoLock hold(state.lock);
  state.angle_generation = generation;
  return S_OK;
}

gl::W10mPanelWindow g_angle_window = {gl::kW10mPanelMagic, 1, nullptr,
                                      &SetAngleSwapChain, 0, 0};

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
      if (!state.current) {
        // Nothing is in the panel: the chain this one was to replace has
        // been taken out (DetachToReplace). The shell shows its bitmap.
        replacing_since_.store(0);
        presenting_.store(static_cast<int32_t>(PresentKind::kBitmap));
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
    // With the lock held, like every other change of what is on screen:
    // presenting() may be giving up on a replacement at this very moment.
    replacing_since_.store(0);
    presenting_.store(static_cast<int32_t>(kind));
  }
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
    replacing_since_.store(0);
    presenting_.store(static_cast<int32_t>(PresentKind::kBitmap));
  }
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

void PanelHost::DetachToReplace(uint32_t generation) {
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
    replacing_since_.store(NowMs());
  }
  NoteF("panel: chain %u taken out, another is to replace it", generation);
  if (!dispatcher) {
    return;
  }
  EnsureMta();
  const HRESULT hr = RunOnUiThread(
      dispatcher.Get(),
      [native, cleared]() -> HRESULT {
        {
          State& state = GetState();
          base::AutoLock hold(state.lock);
          if (state.generation != cleared) {
            return S_OK;  // a newer chain is in the panel by now
          }
        }
        return native->SetSwapChain(nullptr);
      },
      kUiDetachTimeoutMs);
  if (FAILED(hr)) {
    NoteF("panel: chain %u was not out of the panel in time (0x%08lx)",
          generation, static_cast<unsigned long>(hr));
  }
}

void PanelHost::NotReplaced() {
  State& state = GetState();
  base::AutoLock hold(state.lock);
  if (replacing_since_.load() && !state.current) {
    replacing_since_.store(0);
    presenting_.store(static_cast<int32_t>(PresentKind::kBitmap));
  }
}

PresentKind PanelHost::presenting() {
  if (replacing_since_.load()) {
    bool gave_up = false;
    {
      State& state = GetState();
      base::AutoLock hold(state.lock);
      const int64_t since = replacing_since_.load();
      if (since && NowMs() - since > kReplaceGraceMs) {
        // Nothing came to replace the chain that was taken out: whoever made
        // it has given up (GPU compositing has ended, say). Frames are the
        // bitmap's from here, and the shell is to show it.
        replacing_since_.store(0);
        presenting_.store(static_cast<int32_t>(PresentKind::kBitmap));
        gave_up = true;
      }
    }
    if (gave_up) {
      Note("panel: no chain replaced the one taken out: the bitmap from here");
    }
  }
  return static_cast<PresentKind>(presenting_.load());
}

gfx::AcceleratedWidget PanelHost::angle_window() {
  return reinterpret_cast<gfx::AcceleratedWidget>(&g_angle_window);
}

void PanelHost::SetSize(const gfx::Size& pixels) {
  ::InterlockedExchange(&g_angle_window.width, pixels.width());
  ::InterlockedExchange(&g_angle_window.height, pixels.height());
}

bool PanelHost::CompositorPresented() {
  State& state = GetState();
  {
    base::AutoLock hold(state.lock);
    // ANGLE's frame only if ANGLE's chain is the one in the panel. While its
    // surface is being made again it is not, nor once the software path has
    // put its own chain there; the caller asks again.
    if (!state.angle_generation ||
        state.generation != state.angle_generation) {
      return false;
    }
  }
  presented_frames_.fetch_add(1);
  return true;
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
