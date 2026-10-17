// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_browser_native_widget.h"

#include "base/check.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/mojom/window_show_state.mojom.h"

namespace venetium_w10m {

namespace {

aura::Window* EngineRoot() {
  const auto& hosts = aura::Env::GetInstance()->window_tree_hosts();
  CHECK(!hosts.empty()) << "a browser window before the engine's root";
  return hosts.front()->window();
}

}  // namespace

VwBrowserNativeWidget::VwBrowserNativeWidget(BrowserWidget* browser_widget,
                                             BrowserView* browser_view)
    : views::NativeWidgetAura(browser_widget), browser_view_(browser_view) {
  widget_observation_.Observe(browser_widget);
  GetNativeWindow()->SetName("VwBrowserNativeWidget");
}

VwBrowserNativeWidget::~VwBrowserNativeWidget() = default;

views::Widget::InitParams VwBrowserNativeWidget::GetWidgetParams(
    views::Widget::InitParams::Ownership ownership) {
  views::Widget::InitParams params(ownership);
  params.native_widget = this;
  aura::Window* root = EngineRoot();
  params.context = root;
  // The window manager (VwPlatform) keeps a maximized window the size of the
  // screen less the keyboard; these bounds only last until it does.
  params.bounds = root->bounds();
  params.show_state = ui::mojom::WindowShowState::kMaximized;
  return params;
}

bool VwBrowserNativeWidget::UseCustomFrame() const {
  return true;
}

bool VwBrowserNativeWidget::UsesNativeSystemMenu() const {
  // BrowserWidget shows its own (views) menu.
  return false;
}

bool VwBrowserNativeWidget::ShouldSaveWindowPlacement() const {
  // Always the whole screen: nothing worth remembering.
  return false;
}

void VwBrowserNativeWidget::GetWindowPlacement(
    gfx::Rect* bounds,
    ui::mojom::WindowShowState* show_state) const {
  *bounds = GetWidget()->GetRestoredBounds();
  *show_state = ui::mojom::WindowShowState::kMaximized;
}

content::KeyboardEventProcessingResult
VwBrowserNativeWidget::PreHandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}

bool VwBrowserNativeWidget::HandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  return false;
}

bool VwBrowserNativeWidget::ShouldRestorePreviousBrowserWidgetState() const {
  // The bounds and show state above, not a saved placement.
  return false;
}

bool VwBrowserNativeWidget::ShouldUseInitialVisibleOnAllWorkspaces() const {
  return false;
}

void VwBrowserNativeWidget::OnWidgetDestroyed(views::Widget* widget) {
  browser_view_ = nullptr;
  widget_observation_.Reset();
}

// static
void VwBrowserNativeWidgetFactory::Install() {
  BrowserNativeWidgetFactory::Set(new VwBrowserNativeWidgetFactory());
}

BrowserNativeWidget* VwBrowserNativeWidgetFactory::Create(
    BrowserWidget* browser_widget,
    BrowserView* browser_view) {
  return new VwBrowserNativeWidget(browser_widget, browser_view);
}

}  // namespace venetium_w10m
