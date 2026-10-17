// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Chrome's browser window on the phone.
//
// On the desktop a browser window is a DesktopNativeWidgetAura: an aura root
// of its own in an HWND of its own. The phone gives the app one window, the
// shell's panel, and the engine one aura root in it (VwPlatform), so here a
// browser window is a NativeWidgetAura under that root, as on ChromeOS. It
// is always maximized -- the window manager in VwPlatform keeps it the size
// of the screen less the keyboard -- and has no system menu, no saved
// placement and nothing to restore.

#ifndef VENETIUM_W10M_CHROME_VW_BROWSER_NATIVE_WIDGET_H_
#define VENETIUM_W10M_CHROME_VW_BROWSER_NATIVE_WIDGET_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/views/frame/browser_native_widget.h"
#include "chrome/browser/ui/views/frame/browser_native_widget_factory.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class BrowserWidget;

namespace venetium_w10m {

class VwBrowserNativeWidget : public views::NativeWidgetAura,
                              public BrowserNativeWidget,
                              public views::WidgetObserver {
 public:
  VwBrowserNativeWidget(BrowserWidget* browser_widget,
                        BrowserView* browser_view);
  VwBrowserNativeWidget(const VwBrowserNativeWidget&) = delete;
  VwBrowserNativeWidget& operator=(const VwBrowserNativeWidget&) = delete;

 protected:
  ~VwBrowserNativeWidget() override;

  // BrowserNativeWidget:
  views::Widget::InitParams GetWidgetParams(
      views::Widget::InitParams::Ownership ownership) override;
  bool UseCustomFrame() const override;
  bool UsesNativeSystemMenu() const override;
  bool ShouldSaveWindowPlacement() const override;
  void GetWindowPlacement(
      gfx::Rect* bounds,
      ui::mojom::WindowShowState* show_state) const override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      const input::NativeWebKeyboardEvent& event) override;
  bool HandleKeyboardEvent(const input::NativeWebKeyboardEvent& event) override;
  bool ShouldRestorePreviousBrowserWidgetState() const override;
  bool ShouldUseInitialVisibleOnAllWorkspaces() const override;

  // views::WidgetObserver:
  void OnWidgetDestroyed(views::Widget* widget) override;

 private:
  // The BrowserView is the widget's client view; null once the widget is
  // gone.
  raw_ptr<BrowserView> browser_view_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

// Makes every browser window a VwBrowserNativeWidget.
class VwBrowserNativeWidgetFactory : public BrowserNativeWidgetFactory {
 public:
  // Before the first browser window. Chrome owns the factory from here.
  static void Install();

  // BrowserNativeWidgetFactory:
  BrowserNativeWidget* Create(BrowserWidget* browser_widget,
                              BrowserView* browser_view) override;

 private:
  VwBrowserNativeWidgetFactory() = default;
  ~VwBrowserNativeWidgetFactory() override = default;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_BROWSER_NATIVE_WIDGET_H_
