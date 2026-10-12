// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The shell's XAML SwapChainPanel, which the engine presents into.
//
// A SwapChainPanel may only be touched on the thread that owns it: calling
// SetSwapChain from any other thread does not fail, it corrupts the system
// compositor and the process dies a moment later (gecko-w10m). So the engine
// never touches it but through here: the panel's dispatcher runs the call,
// and the caller waits -- briefly, since the XAML thread never waits on the
// engine. XAML shows a swap chain's pixels as logical units, which on a
// phone at 350% is a picture 3.5 times too big; the inverse of the panel's
// composition scale is applied to every chain it is given.
//
// Ported from gecko-w10m's GeckoW10mPanelSwapChain, with the attach state
// kept on the heap (a wait that times out must not leave the UI thread
// writing to a dead stack), and a generation per chain, so that a late call
// for an old chain never undoes a newer one.

#ifndef VENETIUM_W10M_BROWSER_VW_PANEL_H_
#define VENETIUM_W10M_BROWSER_VW_PANEL_H_

#include <stdint.h>

#include <atomic>

#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_ui_types.h"

struct IDXGISwapChain1;
struct IInspectable;

namespace venetium_w10m {

// What is on screen (venetium_w10m_presenting).
enum class PresentKind : int32_t {
  kBitmap = 0,     // frames copied out through frame_copy
  kD3D11 = 1,      // software frames presented through a D3D11 swap chain
  kAngle = 2,      // GPU compositing through ANGLE
  kCompositor = 3, // the D3D11 compositor
};

class PanelHost {
 public:
  static PanelHost* Get();

  // The shell's UI thread, before the engine runs: the panel (any
  // IInspectable that is a SwapChainPanel). Kept for the whole run.
  void SetPanel(IInspectable* panel);
  bool has_panel() const { return has_panel_.load(); }

  // Any thread but the UI thread: shows |chain| in the panel. Returns the
  // chain's generation (> 0), or 0 if the panel did not take it in time.
  uint32_t Attach(IDXGISwapChain1* chain, PresentKind kind);
  // Takes the chain of |generation| out of the panel, unless a newer one is
  // there by now. Does not wait.
  void Detach(uint32_t generation);

  // A frame of |generation| reached the screen.
  void Presented(uint32_t generation);

  // GPU compositing: what the compositor is given for a window. Not an
  // HWND -- there are none -- but the panel as ANGLE understands it
  // (ui/gl/w10m_panel.h): ANGLE's swap chain comes back through it and is
  // shown as kAngle. Its size is the content area's, in pixels.
  gfx::AcceleratedWidget angle_window();
  void SetSize(const gfx::Size& pixels);
  // UI thread: the compositor's frame is on screen.
  void CompositorPresented();

  PresentKind presenting() const {
    return static_cast<PresentKind>(presenting_.load());
  }
  uint64_t presented_frames() const { return presented_frames_.load(); }

 private:
  std::atomic<bool> has_panel_{false};
  std::atomic<int32_t> presenting_{0};
  std::atomic<uint64_t> presented_frames_{0};
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_PANEL_H_
