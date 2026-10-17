// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_window_manager.h"

#include <utility>

#include "base/scoped_multi_source_observation.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/client/visibility_client.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/client/window_types.h"
#include "ui/aura/layout_manager.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/layer_type.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/corewm/tooltip_aura.h"
#include "ui/views/corewm/tooltip_controller.h"
#include "ui/wm/core/base_focus_rules.h"
#include "ui/wm/core/focus_controller.h"
#include "ui/wm/core/shadow_controller.h"
#include "ui/wm/core/shadow_controller_delegate.h"
#include "ui/wm/core/visibility_controller.h"
#include "ui/wm/core/window_modality_controller.h"
#include "ui/wm/core/window_util.h"
#include "ui/wm/public/activation_client.h"
#include "ui/wm/public/tooltip_client.h"

namespace venetium_w10m {

namespace {

bool FillsTheScreen(const aura::Window* window) {
  switch (window->GetProperty(aura::client::kShowStateKey)) {
    case ui::mojom::WindowShowState::kMaximized:
    case ui::mojom::WindowShowState::kFullscreen:
      return true;
    default:
      return false;
  }
}

}  // namespace

// Sizes a container's windows. kFill: every child is the container's size
// (the containers under the root). kWindows: a maximized or fullscreen child
// is the container's size, and any other is kept on it -- a dialog wider
// than a phone is made narrower rather than cut off.
class VwWindowManager::Layout : public aura::LayoutManager,
                                public aura::WindowObserver {
 public:
  enum class Kind { kFill, kWindows };

  Layout(aura::Window* container,
         Kind kind,
         base::RepeatingCallback<void(bool)> fullscreen_changed,
         base::RepeatingClosure windows_changed)
      : container_(container),
        kind_(kind),
        fullscreen_changed_(std::move(fullscreen_changed)),
        windows_changed_(std::move(windows_changed)) {}
  Layout(const Layout&) = delete;
  Layout& operator=(const Layout&) = delete;
  ~Layout() override = default;

  // aura::LayoutManager:
  void OnWindowResized() override {
    for (aura::Window* child : container_->children()) {
      Place(child);
    }
  }
  void OnWindowAddedToLayout(aura::Window* child) override {
    if (kind_ == Kind::kWindows) {
      observations_.AddObservation(child);
    }
    Place(child);
    UpdateFullscreen();
    Changed();
  }
  void OnWillRemoveWindowFromLayout(aura::Window* child) override {
    if (observations_.IsObservingSource(child)) {
      observations_.RemoveObservation(child);
    }
  }
  void OnWindowRemovedFromLayout(aura::Window* child) override {
    UpdateFullscreen();
    Changed();
  }
  void OnChildWindowVisibilityChanged(aura::Window* child,
                                      bool visible) override {
    UpdateFullscreen();
    Changed();
  }
  void SetChildBounds(aura::Window* child,
                      const gfx::Rect& requested_bounds) override {
    SetChildBoundsDirect(child, Target(child, requested_bounds));
  }

  // aura::WindowObserver:
  void OnWindowPropertyChanged(aura::Window* window,
                               const void* key,
                               intptr_t old) override {
    if (key == aura::client::kShowStateKey) {
      Place(window);
      UpdateFullscreen();
      Changed();
    }
  }

 private:
  void Changed() {
    if (windows_changed_) {
      windows_changed_.Run();
    }
  }

  gfx::Rect Target(const aura::Window* child, const gfx::Rect& requested) {
    const gfx::Rect area(container_->bounds().size());
    if (kind_ == Kind::kFill || FillsTheScreen(child)) {
      return area;
    }
    gfx::Rect bounds = requested;
    bounds.AdjustToFit(area);
    return bounds;
  }

  void Place(aura::Window* child) {
    const gfx::Rect target = Target(child, child->bounds());
    if (target != child->bounds()) {
      SetChildBoundsDirect(child, target);
    }
  }

  void UpdateFullscreen() {
    if (!fullscreen_changed_) {
      return;
    }
    bool fullscreen = false;
    for (const aura::Window* child : container_->children()) {
      fullscreen |= child->IsVisible() &&
                    child->GetProperty(aura::client::kShowStateKey) ==
                        ui::mojom::WindowShowState::kFullscreen;
    }
    if (fullscreen != fullscreen_) {
      fullscreen_ = fullscreen;
      fullscreen_changed_.Run(fullscreen);
    }
  }

  raw_ptr<aura::Window> container_;
  const Kind kind_;
  base::RepeatingCallback<void(bool)> fullscreen_changed_;
  base::RepeatingClosure windows_changed_;
  bool fullscreen_ = false;
  base::ScopedMultiSourceObservation<aura::Window, aura::WindowObserver>
      observations_{this};
};

// Which container a new window goes into.
class VwWindowManager::Parenting : public aura::client::WindowParentingClient {
 public:
  explicit Parenting(VwWindowManager* manager) : manager_(manager) {}
  Parenting(const Parenting&) = delete;
  Parenting& operator=(const Parenting&) = delete;
  ~Parenting() override = default;

  aura::Window* GetDefaultParent(aura::Window* window,
                                 const gfx::Rect& bounds,
                                 int64_t display_id) override {
    switch (window->GetType()) {
      case aura::client::WINDOW_TYPE_TOOLTIP:
        return manager_->top_;
      case aura::client::WINDOW_TYPE_MENU:
        return manager_->menus_;
      default:
        break;
    }
    // Always on top, and autofill's popup (a security surface: nothing may
    // cover it).
    if (window->GetProperty(aura::client::kZOrderingKey) !=
        ui::ZOrderLevel::kNormal) {
      return manager_->top_;
    }
    // A bubble or a popup goes where the window it belongs to is, where
    // transient stacking keeps it over that window.
    for (aura::Window* owner = wm::GetTransientParent(window); owner;
         owner = owner->parent()) {
      if (manager_->IsContainer(owner)) {
        return owner;
      }
    }
    return manager_->windows_;
  }

 private:
  raw_ptr<VwWindowManager> manager_;
};

// Windows in the containers can be activated; nothing else.
class VwWindowManager::Rules : public wm::BaseFocusRules {
 public:
  explicit Rules(VwWindowManager* manager) : manager_(manager) {}
  Rules(const Rules&) = delete;
  Rules& operator=(const Rules&) = delete;
  ~Rules() override = default;

  // wm::BaseFocusRules:
  bool SupportsChildActivation(const aura::Window* window) const override {
    return manager_->IsContainer(window);
  }

 private:
  raw_ptr<VwWindowManager> manager_;
};

VwWindowManager::VwWindowManager(
    aura::Window* root,
    base::RepeatingCallback<void(bool)> fullscreen_changed,
    base::RepeatingClosure windows_changed)
    : root_(root), windows_changed_(std::move(windows_changed)) {
  root_->SetLayoutManager(
      std::make_unique<Layout>(root_, Layout::Kind::kFill,
                               base::RepeatingCallback<void(bool)>(),
                               base::RepeatingClosure()));
  windows_ = MakeContainer("VwWindowsContainer");
  menus_ = MakeContainer("VwMenusContainer");
  top_ = MakeContainer("VwTopContainer");
  windows_->SetLayoutManager(std::make_unique<Layout>(
      windows_, Layout::Kind::kWindows, std::move(fullscreen_changed),
      windows_changed_));
  menus_->SetLayoutManager(std::make_unique<Layout>(
      menus_, Layout::Kind::kWindows, base::RepeatingCallback<void(bool)>(),
      windows_changed_));
  top_->SetLayoutManager(std::make_unique<Layout>(
      top_, Layout::Kind::kWindows, base::RepeatingCallback<void(bool)>(),
      windows_changed_));

  // The first of the root's event handlers: a window-modal dialog takes the
  // events of the window it belongs to before anything else sees them.
  modality_ = std::make_unique<wm::WindowModalityController>(root_.get());

  focus_controller_ = std::make_unique<wm::FocusController>(new Rules(this));
  aura::client::SetFocusClient(root_, focus_controller_.get());
  wm::SetActivationClient(root_, focus_controller_.get());
  // A tap activates the window it lands in, and focuses it.
  root_->AddPreTargetHandler(focus_controller_.get());
  focus_controller_->AddObserver(this);

  parenting_ = std::make_unique<Parenting>(this);
  aura::client::SetWindowParentingClient(root_, parenting_.get());

  tooltips_ = std::make_unique<views::corewm::TooltipController>(
      std::make_unique<views::corewm::TooltipAura>(), focus_controller_.get());
  wm::SetTooltipClient(root_, tooltips_.get());
  root_->AddPreTargetHandler(tooltips_.get());

  visibility_ = std::make_unique<wm::VisibilityController>();
  aura::client::SetVisibilityClient(root_, visibility_.get());

  shadows_ = std::make_unique<wm::ShadowController>(
      focus_controller_.get(), std::unique_ptr<wm::ShadowControllerDelegate>());
}

VwWindowManager::~VwWindowManager() {
  shadows_.reset();
  aura::client::SetVisibilityClient(root_, nullptr);
  visibility_.reset();
  root_->RemovePreTargetHandler(tooltips_.get());
  wm::SetTooltipClient(root_, nullptr);
  tooltips_.reset();
  aura::client::SetWindowParentingClient(root_, nullptr);
  parenting_.reset();
  focus_controller_->RemoveObserver(this);
  root_->RemovePreTargetHandler(focus_controller_.get());
  wm::SetActivationClient(root_, nullptr);
  aura::client::SetFocusClient(root_, nullptr);
  focus_controller_.reset();
  modality_.reset();
}

void VwWindowManager::OnWindowActivated(
    wm::ActivationChangeObserver::ActivationReason reason,
    aura::Window* gained_active,
    aura::Window* lost_active) {
  if (windows_changed_) {
    windows_changed_.Run();
  }
}

aura::Window* VwWindowManager::MakeContainer(const char* name) {
  // Owned by the root, which deletes its children.
  auto* container = new aura::Window(nullptr);
  container->Init(ui::LAYER_NOT_DRAWN);
  container->SetName(name);
  root_->AddChild(container);
  container->Show();
  return container;
}

bool VwWindowManager::IsContainer(const aura::Window* window) const {
  return window && (window == windows_ || window == menus_ || window == top_);
}

}  // namespace venetium_w10m
