// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_platform.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/containers/flat_set.h"
#include "base/observer_list.h"
#include "base/scoped_observation.h"
#include "ui/aura/client/default_capture_client.h"
#include "ui/aura/client/focus_change_observer.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/display/display.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/compositor/compositor.h"
#include "ui/events/pointer_details.h"
#include "ui/aura/client/cursor_shape_client.h"
#include "ui/wm/core/cursor_loader.h"
#include "ui/wm/core/default_activation_client.h"
#include "ui/wm/core/default_screen_position_client.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_input_method.h"
#include "venetium_w10m/browser/vw_panel.h"
#include "venetium_w10m/browser/vw_present_guard.h"
#include "venetium_w10m/browser/vw_vsync.h"
#include "venetium_w10m/browser/vw_window_manager.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

constexpr int64_t kDisplayId = 1;

// How many frames that were not ANGLE's the compositor may show before it
// is no longer asked about them (GPU compositing has ended, then).
constexpr int kMaxCompositorFrameAsks = 120;

VwPlatform* g_platform = nullptr;

// Made by InstallScreen, never destroyed: Chrome asks display::Screen for
// the display until the process ends.
VwScreen* g_installed_screen = nullptr;

// A page in fullscreen, or Chrome's window: the shell hides the status bar.
void TellFullscreen(bool on) {
  VwEngine::Get()->fullscreen.store(on);
  venetium_w10m_events events = VwEngine::Get()->events();
  if (events.fullscreen) {
    events.fullscreen(on ? 1 : 0);
  }
}

// Like //headless's focus client: one focused window, observers told.
class VwFocusClient : public aura::client::FocusClient,
                      public aura::WindowObserver {
 public:
  VwFocusClient() = default;
  ~VwFocusClient() override = default;

  void AddObserver(aura::client::FocusChangeObserver* observer) override {
    observers_.AddObserver(observer);
  }
  void RemoveObserver(aura::client::FocusChangeObserver* observer) override {
    observers_.RemoveObserver(observer);
  }
  void FocusWindow(aura::Window* window) override {
    if (window && !window->CanFocus()) {
      return;
    }
    if (focused_) {
      observation_.Reset();
    }
    aura::Window* old = focused_;
    focused_ = window;
    if (focused_) {
      observation_.Observe(focused_.get());
    }
    for (aura::client::FocusChangeObserver& observer : observers_) {
      observer.OnWindowFocused(focused_, old);
    }
    if (auto* observer = aura::client::GetFocusChangeObserver(old)) {
      observer->OnWindowFocused(focused_, old);
    }
    if (auto* observer = aura::client::GetFocusChangeObserver(focused_)) {
      observer->OnWindowFocused(focused_, old);
    }
  }
  void ResetFocusWithinActiveWindow(aura::Window* window) override {
    if (!window->Contains(focused_)) {
      FocusWindow(window);
    }
  }
  aura::Window* GetFocusedWindow() override { return focused_; }

  void OnWindowDestroying(aura::Window* window) override {
    FocusWindow(nullptr);
  }

 private:
  raw_ptr<aura::Window> focused_ = nullptr;
  base::ScopedObservation<aura::Window, aura::WindowObserver> observation_{
      this};
  base::ObserverList<aura::client::FocusChangeObserver> observers_;
};

// Windows without a parent -- popups, the list of a <select> -- go under the
// root, positioned in screen coordinates.
class VwParentingClient : public aura::client::WindowParentingClient {
 public:
  explicit VwParentingClient(aura::Window* root) : root_(root) {
    aura::client::SetWindowParentingClient(root_, this);
  }
  ~VwParentingClient() override {
    aura::client::SetWindowParentingClient(root_, nullptr);
  }

  aura::Window* GetDefaultParent(aura::Window* window,
                                 const gfx::Rect& bounds,
                                 int64_t display_id) override {
    return root_;
  }

 private:
  raw_ptr<aura::Window> root_;
};

int ModifierFlags(uint32_t modifiers) {
  int flags = 0;
  if (modifiers & 1) {
    flags |= ui::EF_SHIFT_DOWN;
  }
  if (modifiers & 2) {
    flags |= ui::EF_CONTROL_DOWN;
  }
  if (modifiers & 4) {
    flags |= ui::EF_ALT_DOWN;
  }
  return flags;
}

}  // namespace

// --- VwScreen ---------------------------------------------------------------

VwScreen::VwScreen(const gfx::Size& screen_pixels, float scale) {
  Update(screen_pixels, scale);
}

VwScreen::~VwScreen() = default;

void VwScreen::Update(const gfx::Size& screen_pixels, float scale) {
  screen_pixels_ = screen_pixels.IsEmpty() ? gfx::Size(720, 1280)
                                           : screen_pixels;
  scale_ = scale > 0 ? scale : 1.0f;
  display::Display display(kDisplayId);
  display.SetScaleAndBounds(scale_, gfx::Rect(screen_pixels_));
  // The work area is the browser's room, not the whole screen: a menu or a
  // submenu placed to fit the screen went under the navigation bar.
  if (!room_pixels_.IsEmpty()) {
    gfx::Rect work(gfx::ScaleToFlooredSize(room_pixels_, 1.0f / scale_));
    work.Intersect(display.bounds());
    if (!work.IsEmpty()) {
      display.set_work_area(work);
    }
  }
  display.set_touch_support(display::Display::TouchSupport::AVAILABLE);
  display.set_accelerometer_support(
      display::Display::AccelerometerSupport::UNAVAILABLE);
  ProcessDisplayChanged(display, /*is_primary=*/true);
  NoteF("screen: %dx%d px at %.2f -> %dx%d dip, work area %dx%d",
        screen_pixels_.width(), screen_pixels_.height(), scale_,
        display.bounds().width(), display.bounds().height(),
        display.work_area().width(), display.work_area().height());
}

void VwScreen::SetRoom(const gfx::Size& room_pixels) {
  if (room_pixels.IsEmpty() || room_pixels == room_pixels_) {
    return;
  }
  room_pixels_ = room_pixels;
  Update(screen_pixels_, scale_);
}

display::Display VwScreen::GetDisplayNearestWindow(
    gfx::NativeWindow window) const {
  return GetPrimaryDisplay();
}

// --- VwWindowTreeHost -------------------------------------------------------

VwWindowTreeHost::VwWindowTreeHost(const gfx::Rect& bounds_in_pixels)
    : bounds_(bounds_in_pixels) {
  CreateCompositor();
  if (VwEngine::Get()->config().gpu_compositing) {
    // The compositor alone gets the panel for a window: GL draws into it.
    // GetAcceleratedWidget() stays null, so nothing that takes a widget for
    // an HWND (IME, native window tracking) ever sees it.
    PanelHost::Get()->SetSize(bounds_in_pixels.size());
    compositor()->SetAcceleratedWidget(PanelHost::Get()->angle_window());
  } else {
    OnAcceleratedWidgetAvailable();
  }
}

VwWindowTreeHost::~VwWindowTreeHost() {
  DestroyCompositor();
  DestroyDispatcher();
}

ui::EventSource* VwWindowTreeHost::GetEventSource() {
  return this;
}

gfx::AcceleratedWidget VwWindowTreeHost::GetAcceleratedWidget() {
  // No HWND: viz composites into memory and the frame sink takes it from
  // there (vw_frame_sink.cc).
  return gfx::kNullAcceleratedWidget;
}

void VwWindowTreeHost::ShowImpl() {}

void VwWindowTreeHost::HideImpl() {}

gfx::Rect VwWindowTreeHost::GetBoundsInPixels() const {
  return bounds_;
}

void VwWindowTreeHost::SetBoundsInPixels(const gfx::Rect& bounds) {
  // The root window's bounds are DIPs: OnHostResizedInPixels sizes it.
  if (bounds_ == bounds) {
    return;
  }
  const bool origin_changed = bounds_.origin() != bounds.origin();
  bounds_ = bounds;
  if (origin_changed) {
    OnHostMovedInPixels();
  }
  OnHostResizedInPixels(bounds.size());
}

gfx::Point VwWindowTreeHost::GetLocationOnScreenInPixels() const {
  return bounds_.origin();
}

void VwWindowTreeHost::SetCapture() {}

void VwWindowTreeHost::ReleaseCapture() {}

bool VwWindowTreeHost::CaptureSystemKeyEventsImpl(
    std::optional<base::flat_set<ui::DomCode>> codes) {
  return false;
}

void VwWindowTreeHost::ReleaseSystemKeyEventCapture() {}

bool VwWindowTreeHost::IsKeyLocked(ui::DomCode dom_code) {
  return false;
}

base::flat_map<std::string, std::string>
VwWindowTreeHost::GetKeyboardLayoutMap() {
  return {};
}

void VwWindowTreeHost::SetCursorNative(gfx::NativeCursor cursor) {}

void VwWindowTreeHost::MoveCursorToScreenLocationInPixels(
    const gfx::Point& location) {}

void VwWindowTreeHost::OnCursorVisibilityChangedNative(bool show) {}

void VwWindowTreeHost::RefreshScale() {
  OnHostResizedInPixels(bounds_.size());
}

// --- VwPlatform -------------------------------------------------------------

// static
VwPlatform* VwPlatform::Get() {
  return g_platform;
}

// static
void VwPlatform::InstallScreen(const gfx::Size& screen_pixels, float scale) {
  if (g_installed_screen) {
    g_installed_screen->Update(screen_pixels, scale);
    return;
  }
  g_installed_screen = new VwScreen(screen_pixels, scale);
  display::Screen::SetScreenInstance(g_installed_screen);
}

VwPlatform::VwPlatform(const gfx::Size& content_pixels,
                       const gfx::Size& screen_pixels,
                       float scale,
                       bool window_manager,
                       base::RepeatingClosure windows_changed)
    : screen_pixels_(screen_pixels), scale_(scale) {
  g_platform = this;
  // The screen first: the host asks it for the scale as soon as it is
  // initialised.
  const gfx::Size screen_size =
      screen_pixels.IsEmpty() ? content_pixels : screen_pixels;
  if (g_installed_screen) {
    screen_ = g_installed_screen;
    screen_->Update(screen_size, scale);
  } else {
    owned_screen_ = std::make_unique<VwScreen>(screen_size, scale);
    screen_ = owned_screen_.get();
    display::Screen::SetScreenInstance(screen_);
  }
  screen_->SetRoom(content_pixels);

  const gfx::Rect bounds(content_pixels.IsEmpty() ? gfx::Size(720, 1100)
                                                  : content_pixels);
  host_ = std::make_unique<VwWindowTreeHost>(bounds);
  input_method_ = std::make_unique<VwInputMethod>(host_.get());
  host_->SetSharedInputMethod(input_method_.get());
  host_->InitHost();
  host_->SetBoundsInPixels(bounds);
  host_->window()->Show();

  if (window_manager) {
    window_manager_ = std::make_unique<VwWindowManager>(
        root(), base::BindRepeating(&TellFullscreen),
        std::move(windows_changed));
  } else {
    focus_client_ = std::make_unique<VwFocusClient>();
    aura::client::SetFocusClient(root(), focus_client_.get());
    // Deletes itself with the root window.
    new wm::DefaultActivationClient(root());
    parenting_client_ = std::make_unique<VwParentingClient>(root());
  }
  capture_client_ =
      std::make_unique<aura::client::DefaultCaptureClient>(root());
  screen_position_client_ =
      std::make_unique<wm::DefaultScreenPositionClient>(root());
  // Cursor shapes: a page's CSS cursor is looked up through this when a
  // mouse is over it, and there must be one to ask (it CHECKs). LoadCursor
  // is a stub on the phone, so every shape comes back empty: there is no
  // pointer on screen to give it to anyway.
  cursor_loader_ = std::make_unique<wm::CursorLoader>();
  aura::client::SetCursorShapeClient(cursor_loader_.get());

  host_->Show();
  NoteF("platform: browser window %dx%d px", bounds.width(), bounds.height());

  // BeginFrames come from the display's vertical blank inside viz
  // (components/viz/service/frame_sinks/external_begin_frame_source_w10m.cc):
  // one thread waiting on the display is enough. "--vw-vsync=timer" among
  // the shell's switches puts viz back on its timer, and only then is that
  // timer steered from here, as it was before.
  if (VwEngine::Get()->config().switches.find("--vw-vsync=timer") !=
      std::string::npos) {
    vsync_ = std::make_unique<VwVsync>(base::BindRepeating(
        &VwPlatform::OnVSync, weak_factory_.GetWeakPtr()));
  }

  // The first frame the compositor itself puts on screen (GPU compositing;
  // the software paths count their own presents).
  if (VwEngine::Get()->config().gpu_compositing) {
    WatchCompositorFrame();
  }
}

void VwPlatform::WatchCompositorFrame() {
  host_->compositor()->RequestSuccessfulPresentationTimeForNextFrame(
      base::BindOnce(&VwPlatform::OnCompositorFrame,
                     weak_factory_.GetWeakPtr()));
}

void VwPlatform::OnCompositorFrame(const viz::FrameTimingDetails&) {
  // GL has been given up: every frame from here is software compositing's,
  // and those count themselves (a present through Direct3D 11, or the
  // shell's first copy of the bitmap). Not asked again, and no frame forced.
  if (VwEngine::Get()->gpu_compositing_failed()) {
    return;
  }
  // Counted only if ANGLE's chain is the one in the panel at this moment. It
  // is not while its surface is being made again (a swap chain that would
  // not be resized), nor once software compositing has taken over.
  if (PanelHost::Get()->CompositorPresented()) {
    VwEngine::Get()->HardwarePresentWorked(PresentKind::kAngle);
    // One frame is a start, not proof: the run has to go on for a little
    // before it stops counting against GPU compositing.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce([]() {
          if (!VwEngine::Get()->gpu_compositing_failed()) {
            PresentGuard::Get()->Proven(PresentKind::kAngle);
            Note("gpu: GPU compositing has run for five seconds");
          }
        }),
        base::Seconds(5));
    return;
  }
  if (PanelHost::Get()->presenting() == PresentKind::kD3D11 ||
      ++compositor_frame_asks_ > kMaxCompositorFrameAsks) {
    // Software frames, which count themselves; or frames keep arriving that
    // are not ANGLE's.
    return;
  }
  // Asked again, and a frame asked for with it: a page that stands still
  // draws no other, and asking alone does not make one.
  WatchCompositorFrame();
  host_->compositor()->ScheduleFullRedraw();
}

void VwPlatform::OnVSync(base::TimeTicks timebase, base::TimeDelta interval) {
  host_->compositor()->SetDisplayVSyncParameters(timebase, interval);
}

void VwPlatform::SetActive(bool active) {
  // Suspended with a video only just started on the GPU's decoder: the run
  // did not die of that, and the phone may end a suspended app without
  // another word. Resumed with it still there: it counts again.
  PresentGuard::Get()->VideoSuspended(!active);
  if (!active && !VwEngine::Get()->gpu_compositing_failed()) {
    // Suspended by the user with GPU compositing on screen: the run did not
    // die of it, however short it was.
    PresentGuard::Get()->Proven(PresentKind::kAngle);
  }
  if (!active) {
    // The same for the Direct3D 11 compositor. And a run suspended before
    // the compositor drew, or tried to draw, a single frame did nothing the
    // driver could have died of: it is not counted against it.
    PresentGuard::Get()->Untried(PresentKind::kCompositor);
    if (!VwEngine::Get()->compositor_failed()) {
      PresentGuard::Get()->Proven(PresentKind::kCompositor);
    }
  }
  if (vsync_) {
    vsync_->SetActive(active);
  }
}

VwPlatform::~VwPlatform() {
  vsync_.reset();
  aura::client::SetCursorShapeClient(nullptr);
  cursor_loader_.reset();
  screen_position_client_.reset();
  parenting_client_.reset();
  capture_client_.reset();
  window_manager_.reset();
  aura::client::SetFocusClient(root(), nullptr);
  host_.reset();
  focus_client_.reset();
  input_method_.reset();
  screen_ = nullptr;
  if (owned_screen_) {
    display::Screen::SetScreenInstance(nullptr);
    owned_screen_.reset();
  }
  g_platform = nullptr;
}

void VwPlatform::Resize(const gfx::Size& content_pixels) {
  if (content_pixels.IsEmpty()) {
    return;
  }
  // Before the host: the GL surface is made again at the new size, and
  // reads it from the panel.
  PanelHost::Get()->SetSize(content_pixels);
  host_->SetBoundsInPixels(gfx::Rect(content_pixels));
  screen_->SetRoom(content_pixels);
}

void VwPlatform::SetScreen(const gfx::Size& screen_pixels) {
  if (screen_pixels.IsEmpty() || screen_pixels == screen_pixels_) {
    return;
  }
  screen_pixels_ = screen_pixels;
  screen_->Update(screen_pixels_, scale_);
}

void VwPlatform::SetScale(float scale) {
  if (scale <= 0 || scale == scale_) {
    return;
  }
  scale_ = scale;
  screen_->Update(screen_pixels_, scale_);
  host_->RefreshScale();
}

base::TimeTicks VwPlatform::EventTime(int64_t time_qpc) {
  base::TimeTicks now = base::TimeTicks::Now();
  if (time_qpc > 0) {
    // The shell stamps an event with the performance counter when it gets
    // it; it may reach us a frame or two later. Keeping the spacing keeps
    // flings from reading a burst of queued moves as a very fast flick. The
    // counter is only used for the event's age: TimeTicks may run on another
    // clock (on ARM, base picks QPC or timeGetTime at random).
    LARGE_INTEGER counter = {};
    ::QueryPerformanceCounter(&counter);
    if (counter.QuadPart >= time_qpc) {
      now -= base::TimeDelta::FromQPCValue(counter.QuadPart - time_qpc);
    }
  }
  if (now < last_event_time_) {
    now = last_event_time_;
  }
  last_event_time_ = now;
  return now;
}

void VwPlatform::SetKeyboardInset(int32_t pixels) {
  const float scale = scale_ > 0 ? scale_ : 1.0f;
  const gfx::Size area = root()->bounds().size();
  const int height = std::min(
      area.height(), static_cast<int>(std::lround(std::max(pixels, 0) / scale)));
  input_method_->KeyboardChanged(
      height > 0 ? gfx::Rect(0, area.height() - height, area.width(), height)
                 : gfx::Rect());
}

void VwPlatform::Dispatch(ui::Event* event) {
  std::ignore = host_->SendEventToSink(event);
}

void VwPlatform::Touch(int32_t pointer_id,
                       int32_t state,
                       int32_t x,
                       int32_t y,
                       int64_t time_qpc) {
  ui::EventType type;
  switch (state) {
    case 0:
      type = ui::EventType::kTouchPressed;
      break;
    case 1:
      type = ui::EventType::kTouchMoved;
      break;
    case 2:
      type = ui::EventType::kTouchReleased;
      break;
    default:
      type = ui::EventType::kTouchCancelled;
      break;
  }

  int32_t id;
  auto it = fingers_.find(pointer_id);
  if (it != fingers_.end()) {
    id = it->second;
  } else {
    if (type != ui::EventType::kTouchPressed) {
      return;  // a finger we never saw go down
    }
    // The smallest id not in use: the gesture recognizer keeps per-id state
    // in small arrays.
    id = 0;
    while (true) {
      bool taken = false;
      for (const auto& [shell_id, aura_id] : fingers_) {
        taken |= aura_id == id;
      }
      if (!taken) {
        break;
      }
      ++id;
    }
    fingers_[pointer_id] = id;
  }

  // A finger is about 7 mm across; the radius helps Blink's touch
  // adjustment pick the link a finger meant.
  const float radius = dpi_ > 0 ? dpi_ * 3.5f / 25.4f : 12.0f * scale_;
  ui::PointerDetails details(ui::EventPointerType::kTouch, id, radius, radius,
                             /*force=*/0.5f);
  const gfx::PointF location(x, y);
  ui::TouchEvent event(type, location, location, EventTime(time_qpc), details);
  Dispatch(&event);

  if (type == ui::EventType::kTouchReleased ||
      type == ui::EventType::kTouchCancelled) {
    fingers_.erase(pointer_id);
  }
}

void VwPlatform::Mouse(int32_t message, int32_t x, int32_t y) {
  ui::EventType type = ui::EventType::kMouseMoved;
  int changed = 0;
  switch (message) {
    case 0x201:  // WM_LBUTTONDOWN
      type = ui::EventType::kMousePressed;
      changed = ui::EF_LEFT_MOUSE_BUTTON;
      break;
    case 0x202:  // WM_LBUTTONUP
      type = ui::EventType::kMouseReleased;
      changed = ui::EF_LEFT_MOUSE_BUTTON;
      break;
    case 0x204:  // WM_RBUTTONDOWN
      type = ui::EventType::kMousePressed;
      changed = ui::EF_RIGHT_MOUSE_BUTTON;
      break;
    case 0x205:  // WM_RBUTTONUP
      type = ui::EventType::kMouseReleased;
      changed = ui::EF_RIGHT_MOUSE_BUTTON;
      break;
    case 0x207:  // WM_MBUTTONDOWN
      type = ui::EventType::kMousePressed;
      changed = ui::EF_MIDDLE_MOUSE_BUTTON;
      break;
    case 0x208:  // WM_MBUTTONUP
      type = ui::EventType::kMouseReleased;
      changed = ui::EF_MIDDLE_MOUSE_BUTTON;
      break;
    default:
      if (mouse_flags_) {
        type = ui::EventType::kMouseDragged;
      }
      break;
  }
  if (type == ui::EventType::kMouseReleased && !(mouse_flags_ & changed)) {
    // A release of a button that is not down: the shell's idea of which one
    // it was differs, or the release is a stray one. Release what is down.
    if (!mouse_flags_) {
      return;
    }
    changed = mouse_flags_;
  }
  if (type == ui::EventType::kMousePressed) {
    mouse_flags_ |= changed;
  }
  const int flags = mouse_flags_ | ModifierFlags(modifiers_);
  if (type == ui::EventType::kMouseReleased) {
    mouse_flags_ &= ~changed;
  }
  const gfx::PointF location(x, y);
  ui::MouseEvent event(type, location, location, base::TimeTicks::Now(), flags,
                       changed);
  if (type == ui::EventType::kMousePressed) {
    event.SetClickCount(1);
  }
  Dispatch(&event);
}

void VwPlatform::Wheel(int32_t x, int32_t y, double dx, double dy) {
  // The shell's deltas are the wheel's (WM_MOUSEWHEEL and WM_MOUSEHWHEEL):
  // dy > 0 scrolls up, dx > 0 right, in device pixels. MouseWheelEvent
  // offsets are DIPs with x > 0 meaning left.
  const float scale = scale_ > 0 ? scale_ : 1.0f;
  wheel_remainder_ += gfx::Vector2dF(static_cast<float>(-dx / scale),
                                     static_cast<float>(dy / scale));
  const gfx::Vector2d offset(static_cast<int>(wheel_remainder_.x()),
                             static_cast<int>(wheel_remainder_.y()));
  if (offset.IsZero()) {
    return;
  }
  wheel_remainder_ -= gfx::Vector2dF(offset.x(), offset.y());
  const gfx::PointF location(x, y);
  ui::MouseWheelEvent event(offset, location, location, base::TimeTicks::Now(),
                            ModifierFlags(modifiers_), 0);
  Dispatch(&event);
}

void VwPlatform::Key(int32_t vkey, bool down, uint32_t modifiers) {
  modifiers_ = modifiers;
  const auto key_code = static_cast<ui::KeyboardCode>(vkey & 0xff);
  ui::KeyEvent event(
      down ? ui::EventType::kKeyPressed : ui::EventType::kKeyReleased,
      key_code, ui::UsLayoutKeyboardCodeToDomCode(key_code),
      ModifierFlags(modifiers), base::TimeTicks::Now());
  Dispatch(&event);
}

void VwPlatform::Text(const std::u16string& text) {
  input_method_->CommitText(text);
}

void VwPlatform::Composition(const std::u16string& text) {
  input_method_->SetComposition(text);
}

}  // namespace venetium_w10m
