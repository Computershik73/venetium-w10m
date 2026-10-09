// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The browser window, without a window.
//
// On the desktop Aura's root window lives in an HWND (ui::WinWindow) and the
// screen comes from EnumDisplayMonitors (display::win::ScreenWin). Windows 10
// Mobile has neither -- CreateWindow and the message functions are stubs
// there -- so, as //headless does, the root is an aura::WindowTreeHost with
// no native window (a null AcceleratedWidget: viz then composites into memory,
// see vw_frame_sink), and the screen is what the shell says it is.
//
// Input arrives from the shell in device pixels and is dispatched into the
// host as ui:: events, which is what an HWND's message handler would have
// done: Aura's gesture recognizer turns fingers into taps, scrolls and
// pinches, and RenderWidgetHostViewAura hands them to Blink.

#ifndef VENETIUM_W10M_BROWSER_VW_PLATFORM_H_
#define VENETIUM_W10M_BROWSER_VW_PLATFORM_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "ui/aura/window_tree_host.h"
#include "ui/display/screen_base.h"
#include "ui/gfx/geometry/rect.h"

namespace aura {
namespace client {
class DefaultCaptureClient;
class FocusClient;
class WindowParentingClient;
}  // namespace client
}  // namespace aura

namespace wm {
class DefaultScreenPositionClient;
}  // namespace wm

namespace venetium_w10m {

class VwInputMethod;

// One display, the phone's, sized and scaled by the shell.
class VwScreen : public display::ScreenBase {
 public:
  VwScreen(const gfx::Size& screen_pixels, float scale);
  ~VwScreen() override;

  void Update(const gfx::Size& screen_pixels, float scale);

  // display::Screen:
  display::Display GetDisplayNearestWindow(
      gfx::NativeWindow window) const override;

 private:
  gfx::Size screen_pixels_;
  float scale_;
};

class VwWindowTreeHost : public aura::WindowTreeHost {
 public:
  explicit VwWindowTreeHost(const gfx::Rect& bounds_in_pixels);
  ~VwWindowTreeHost() override;

  // aura::WindowTreeHost:
  ui::EventSource* GetEventSource() override;
  gfx::AcceleratedWidget GetAcceleratedWidget() override;
  void ShowImpl() override;
  void HideImpl() override;
  gfx::Rect GetBoundsInPixels() const override;
  void SetBoundsInPixels(const gfx::Rect& bounds) override;
  gfx::Point GetLocationOnScreenInPixels() const override;
  void SetCapture() override;
  void ReleaseCapture() override;
  bool CaptureSystemKeyEventsImpl(
      std::optional<base::flat_set<ui::DomCode>> codes) override;
  void ReleaseSystemKeyEventCapture() override;
  bool IsKeyLocked(ui::DomCode dom_code) override;
  base::flat_map<std::string, std::string> GetKeyboardLayoutMap() override;
  void SetCursorNative(gfx::NativeCursor cursor) override;
  void MoveCursorToScreenLocationInPixels(const gfx::Point& location) override;
  void OnCursorVisibilityChangedNative(bool show) override;

  // Re-reads the display's scale (after VwScreen::Update).
  void RefreshScale();

 private:
  gfx::Rect bounds_;
};

// Everything that makes the host usable: the screen, focus, capture,
// activation, window parenting (popups, <select> lists), the input method,
// and turning the shell's input into events.
class VwPlatform {
 public:
  VwPlatform(const gfx::Size& content_pixels,
             const gfx::Size& screen_pixels,
             float scale);
  ~VwPlatform();

  VwWindowTreeHost* host() { return host_.get(); }
  aura::Window* root() { return host_->window(); }
  VwInputMethod* input_method() { return input_method_.get(); }

  void Resize(const gfx::Size& content_pixels);
  void SetScreen(const gfx::Size& screen_pixels);
  void SetScale(float scale);
  void SetDpi(int32_t dpi) { dpi_ = dpi; }

  // Input in content pixels; time_ms is the shell's GetTickCount64.
  void Touch(int32_t pointer_id,
             int32_t state,
             int32_t x,
             int32_t y,
             uint64_t time_ms);
  void Mouse(int32_t message, int32_t x, int32_t y);
  void Wheel(int32_t x, int32_t y, double dx, double dy);
  void Key(int32_t vkey, bool down, uint32_t modifiers);
  void Text(const std::u16string& text);
  void Composition(const std::u16string& text);

 private:
  base::TimeTicks EventTime(uint64_t time_ms);
  void Dispatch(ui::Event* event);

  std::unique_ptr<VwScreen> screen_;
  std::unique_ptr<VwWindowTreeHost> host_;
  std::unique_ptr<aura::client::FocusClient> focus_client_;
  std::unique_ptr<aura::client::DefaultCaptureClient> capture_client_;
  std::unique_ptr<aura::client::WindowParentingClient> parenting_client_;
  std::unique_ptr<wm::DefaultScreenPositionClient> screen_position_client_;
  std::unique_ptr<VwInputMethod> input_method_;

  gfx::Size screen_pixels_;
  float scale_;
  int32_t dpi_ = 0;
  // Fingers down, by the shell's pointer id, and the id Aura knows them by
  // (small and reused, as touch ids are on Windows).
  std::map<int32_t, int32_t> fingers_;
  // Last mouse button state, for the flags of moves.
  int mouse_flags_ = 0;
  uint32_t modifiers_ = 0;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_PLATFORM_H_
