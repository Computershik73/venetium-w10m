// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// A window manager for Chrome's interface on the engine's one aura root.
//
// On the desktop every Chrome window -- the browser, a menu, a bubble, a
// tooltip -- is a window of the system's, which stacks them, activates them
// and sizes a maximized one. Here they are all windows under one aura root
// (chrome/browser/ui/views/native_widget_factory.cc), so what the system did
// is done here, as ash does it on ChromeOS, in small:
//   - three containers, stacked: windows (browser windows, dialogs, bubbles
//     and popups, each over the window it belongs to), menus (Chrome's and
//     the pages' <select> lists), and on top tooltips and the windows that
//     must not be covered (autofill, and anything always on top);
//   - focus and activation by rules (wm::FocusController), so a tapped
//     window becomes active and a bubble that loses activation closes;
//   - a maximized or fullscreen window is kept the size of the screen, and
//     any other is kept on it;
//   - tooltips, window-modal dialogs, shadows, show and hide animations.
// Chrome itself makes the rest (wm::WMState with transient windows, in
// ChromeBrowserMainExtraPartsViews).

#ifndef VENETIUM_W10M_BROWSER_VW_WINDOW_MANAGER_H_
#define VENETIUM_W10M_BROWSER_VW_WINDOW_MANAGER_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"

namespace aura {
class Window;
namespace client {
class WindowParentingClient;
}  // namespace client
}  // namespace aura

namespace views::corewm {
class TooltipController;
}  // namespace views::corewm

namespace wm {
class FocusController;
class ShadowController;
class VisibilityController;
class WindowModalityController;
}  // namespace wm

namespace venetium_w10m {

class VwWindowManager {
 public:
  // |fullscreen_changed| hears whether any window is fullscreen, when that
  // changes (the shell hides the phone's status bar for it).
  VwWindowManager(aura::Window* root,
                  base::RepeatingCallback<void(bool)> fullscreen_changed);
  VwWindowManager(const VwWindowManager&) = delete;
  VwWindowManager& operator=(const VwWindowManager&) = delete;
  ~VwWindowManager();

  wm::FocusController* focus_controller() { return focus_controller_.get(); }
  aura::Window* windows_container() { return windows_; }

 private:
  class Layout;
  class Parenting;
  class Rules;

  aura::Window* MakeContainer(const char* name);
  bool IsContainer(const aura::Window* window) const;

  raw_ptr<aura::Window> root_;
  // Owned by the root window.
  raw_ptr<aura::Window> windows_ = nullptr;
  raw_ptr<aura::Window> menus_ = nullptr;
  raw_ptr<aura::Window> top_ = nullptr;
  std::unique_ptr<wm::FocusController> focus_controller_;
  std::unique_ptr<Parenting> parenting_;
  std::unique_ptr<wm::WindowModalityController> modality_;
  std::unique_ptr<views::corewm::TooltipController> tooltips_;
  std::unique_ptr<wm::VisibilityController> visibility_;
  std::unique_ptr<wm::ShadowController> shadows_;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_WINDOW_MANAGER_H_
