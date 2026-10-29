// EngineView.cpp
#include "pch.h"

#include "client/EngineView.h"

#include <robuffer.h>

#include <algorithm>
#include <cstdlib>

#include "client/Log.h"
#include "engine/EngineHost.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Input;
using namespace winrt::Windows::UI::ViewManagement;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Input;
using namespace winrt::Windows::UI::Xaml::Media;
using namespace winrt::Windows::UI::Xaml::Media::Imaging;

namespace venetium_w10m::client {

namespace {

const engine::Api& Api() {
  return engine::EngineHost::Get().api();
}

// venetium_w10m_text_input_state (see the C header).
constexpr uint32_t kKindMask = 0x7f;
constexpr uint32_t kKeyboardWanted = 0x80;

int64_t Qpc() {
  LARGE_INTEGER counter = {};
  ::QueryPerformanceCounter(&counter);
  return counter.QuadPart;
}

int64_t QpcFrequency() {
  static const int64_t frequency = []() {
    LARGE_INTEGER f = {};
    ::QueryPerformanceFrequency(&f);
    return f.QuadPart > 0 ? f.QuadPart : 1;
  }();
  return frequency;
}

// The performance counter's value at a PointerPoint::Timestamp()
// (microseconds since boot). Split so that the product cannot overflow.
int64_t QpcAt(uint64_t microseconds) {
  const uint64_t f = static_cast<uint64_t>(QpcFrequency());
  return static_cast<int64_t>(microseconds / 1000000 * f +
                              microseconds % 1000000 * f / 1000000);
}

}  // namespace

EngineView::EngineView(double rawPerView) : rawPerView_(rawPerView) {
  root_ = Grid();
  root_.Background(SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(
      255, 255, 255, 255)));

  WireKeyboard();
  root_.Children().Append(sink_);

  // The engine's own swap chain when it presents, the bitmap otherwise.
  panel_ = SwapChainPanel();
  panel_.HorizontalAlignment(HorizontalAlignment::Stretch);
  panel_.VerticalAlignment(VerticalAlignment::Stretch);
  root_.Children().Append(panel_);

  image_ = Image();
  image_.Stretch(Stretch::Fill);
  image_.HorizontalAlignment(HorizontalAlignment::Stretch);
  image_.VerticalAlignment(VerticalAlignment::Stretch);
  image_.IsHitTestVisible(false);
  root_.Children().Append(image_);

  // A transparent background makes it a hit target.
  input_ = Border();
  input_.Background(SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
  input_.HorizontalAlignment(HorizontalAlignment::Stretch);
  input_.VerticalAlignment(VerticalAlignment::Stretch);
  // Every finger goes to the engine; XAML is not to pan or zoom anything.
  input_.ManipulationMode(ManipulationModes::None);
  root_.Children().Append(input_);

  input_.PointerPressed([this](IInspectable const&,
                               PointerRoutedEventArgs const& args) {
    input_.CapturePointer(args.Pointer());
    OnPointer(args, 0);
    args.Handled(true);
  });
  input_.PointerMoved([this](IInspectable const&,
                             PointerRoutedEventArgs const& args) {
    OnPointer(args, 1);
    args.Handled(true);
  });
  input_.PointerReleased([this](IInspectable const&,
                                PointerRoutedEventArgs const& args) {
    OnPointer(args, 2);
    input_.ReleasePointerCapture(args.Pointer());
    args.Handled(true);
  });
  input_.PointerCanceled([this](IInspectable const&,
                                PointerRoutedEventArgs const& args) {
    OnPointer(args, 3);
    args.Handled(true);
  });
  // Also after an ordinary release (ReleasePointerCapture): by then the
  // finger or button is already up and this sends nothing.
  input_.PointerCaptureLost([this](IInspectable const&,
                                   PointerRoutedEventArgs const& args) {
    OnPointer(args, 3);
  });
  input_.PointerWheelChanged([this](IInspectable const&,
                                    PointerRoutedEventArgs const& args) {
    int32_t x = 0, y = 0;
    auto point = args.GetCurrentPoint(input_);
    if (Api().input_wheel && ToFrame(point.Position(), &x, &y)) {
      const int32_t delta = point.Properties().MouseWheelDelta();
      const bool horizontal = point.Properties().IsHorizontalMouseWheel();
      // 120 to a notch; a notch scrolls about three lines. Signed as the
      // wheel messages are: away from the user scrolls up, a tilt right
      // scrolls right.
      const double pixels = delta / 120.0 * 100.0 * rawPerView_;
      Api().input_wheel(x, y, horizontal ? pixels : 0,
                        horizontal ? 0 : pixels);
    }
    args.Handled(true);
  });

  root_.SizeChanged([this](IInspectable const&, SizeChangedEventArgs const&) {
    PushSize();
  });
}

// The engine is started knowing the room: a browser window made at a guessed
// size is made again a moment later, swap chain and all. A layout that does
// not come (the app started out of sight) does not hold the engine back for
// long.
void EngineView::WhenRoomKnown(std::function<void()> then) {
  roomWidth_ = roomHeight_ = 0;  // told again: the engine may be new
  PushSize();
  if (roomWidth_ > 0) {
    then();
    return;
  }
  whenRoom_ = std::move(then);
  roomTimer_ = DispatcherTimer();
  roomTimer_.Interval(std::chrono::milliseconds(1500));
  roomTimer_.Tick([this](auto&&, auto&&) {
    Log::Write(L"view: no layout yet; the engine starts without the room");
    RoomKnown();
  });
  roomTimer_.Start();
}

void EngineView::RoomKnown() {
  if (roomTimer_) {
    roomTimer_.Stop();
    roomTimer_ = nullptr;
  }
  if (!whenRoom_) return;
  auto then = std::move(whenRoom_);
  whenRoom_ = nullptr;
  then();
}

void EngineView::Start() {
  if (tick_) return;
  tick_ = CompositionTarget::Rendering(
      [this](IInspectable const&, IInspectable const&) { Tick(); });
  PushSize();
}

void EngineView::Stop() {
  if (!tick_) return;
  CompositionTarget::Rendering(tick_);
  tick_ = {};
}

void EngineView::SetScale(double rawPerView) {
  if (rawPerView <= 0 || rawPerView == rawPerView_) return;
  rawPerView_ = rawPerView;
  if (Api().set_scale) Api().set_scale(rawPerView_);
  roomWidth_ = roomHeight_ = 0;
  PushSize();
}

void EngineView::PostToUi(std::function<void()> work) {
  CoreWindow::GetForCurrentThread().Dispatcher().RunAsync(
      CoreDispatcherPriority::Normal, [work = std::move(work)]() { work(); });
}

// The room is measured on the container, never on the picture: a stretched
// Image reports the size of what it drew, and feeding that back makes the
// frame decide its own size (gecko-w10m).
void EngineView::PushSize() {
  const int32_t width =
      static_cast<int32_t>(root_.ActualWidth() * rawPerView_ + 0.5);
  const int32_t height =
      static_cast<int32_t>(root_.ActualHeight() * rawPerView_ + 0.5);
  if (width <= 0 || height <= 0) return;
  if (width == roomWidth_ && height == roomHeight_) return;
  roomWidth_ = width;
  roomHeight_ = height;
  Log::Write(L"view: room " + std::to_wstring(width) + L"x" +
             std::to_wstring(height) + L" px");
  if (Api().resize) Api().resize(width, height);
  RoomKnown();
}

void EngineView::EnsureBitmap(int32_t width, int32_t height) {
  if (bitmap_ && width_ == width && height_ == height) return;
  width_ = width;
  height_ = height;
  bitmap_ = WriteableBitmap(width, height);
  image_.Source(bitmap_);
  Log::Write(L"view: frame " + std::to_wstring(width) + L"x" +
             std::to_wstring(height));
}

void EngineView::Tick() {
  auto copy = Api().frame_copy;
  if (!copy) return;

  // Keyboard: noticed here, acted on later. This runs inside XAML's render
  // pass, and raising the keyboard from in here re-enters focus and layout
  // with the frame still in flight (gecko-w10m).
  if (Api().text_input_state && !followPending_ &&
      TextInputChanged(Api().text_input_state())) {
    followPending_ = true;
    PostToUi([this]() {
      followPending_ = false;
      FollowTextInput();
    });
  }

  // The engine presenting into the panel itself: nothing to copy, and the
  // bitmap goes (its memory with it). Back to the bitmap if it stops.
  const int32_t presenting = Api().presenting ? Api().presenting() : 0;
  if (presenting != presenting_) {
    presenting_ = presenting;
    Log::Write(L"view: presenting " + std::to_wstring(presenting));
    if (presenting) {
      image_.Visibility(Visibility::Collapsed);
      image_.Source(nullptr);
      bitmap_ = nullptr;
      width_ = height_ = 0;
      seen_ = 0;
    } else {
      image_.Visibility(Visibility::Visible);
    }
  }
  if (presenting) {
    if (firstFrame_ && Api().presented_frames &&
        Api().presented_frames() > 0) {
      auto handler = std::move(firstFrame_);
      firstFrame_ = nullptr;
      Log::Write(L"view: first frame (presented)");
      handler();
    }
    return;
  }

  int32_t width = width_;
  int32_t height = height_;
  uint64_t serial = seen_;
  if (!bitmap_) {
    width = height = 0;
    copy(nullptr, 0, &width, &height, nullptr);
    if (width > 0 && height > 0) EnsureBitmap(width, height);
    return;
  }

  uint8_t* pixels = nullptr;
  auto access = bitmap_.PixelBuffer()
                    .as<::Windows::Storage::Streams::IBufferByteAccess>();
  if (FAILED(access->Buffer(&pixels)) || !pixels) return;

  if (copy(pixels, width_ * height_ * 4, &width, &height, &serial) == 1) {
    seen_ = serial;
    bitmap_.Invalidate();
    if (firstFrame_) {
      auto handler = std::move(firstFrame_);
      firstFrame_ = nullptr;
      Log::Write(L"view: first frame");
      handler();
    }
    return;
  }
  // A frame of another shape: the engine resized.
  if (width > 0 && height > 0 && (width != width_ || height != height_)) {
    EnsureBitmap(width, height);
  }
}

bool EngineView::ToFrame(Point const& point, int32_t* x, int32_t* y) const {
  // The picture fills the room and the frame is the room's size in raw
  // pixels, so a view point is a frame point times the scale. A point a
  // finger slides past the edge to is clamped, not dropped: the gesture is
  // still going on.
  const int32_t w = roomWidth_ > 0 ? roomWidth_ : width_;
  const int32_t h = roomHeight_ > 0 ? roomHeight_ : height_;
  if (w <= 0 || h <= 0) return false;
  const double sx = root_.ActualWidth() > 0 ? w / root_.ActualWidth()
                                            : rawPerView_;
  const double sy = root_.ActualHeight() > 0 ? h / root_.ActualHeight()
                                             : rawPerView_;
  *x = static_cast<int32_t>(std::clamp(point.X * sx, 0.0, double(w - 1)));
  *y = static_cast<int32_t>(std::clamp(point.Y * sy, 0.0, double(h - 1)));
  return true;
}

void EngineView::OnMouse(PointerPoint const& point, int32_t state, int32_t x,
                         int32_t y) {
  // WM_* numbers, as the engine takes them; the button is the one that
  // changed, remembered from its press.
  int32_t message = 0x200;
  if (state == 0) {
    switch (point.Properties().PointerUpdateKind()) {
      case PointerUpdateKind::RightButtonPressed:
        message = 0x204;
        break;
      case PointerUpdateKind::MiddleButtonPressed:
        message = 0x207;
        break;
      default:
        message = 0x201;
        break;
    }
    mouseDown_ = message;
  } else if (state >= 2) {
    // A release, or the capture lost: the button that went down comes up,
    // once. Nothing down, nothing to send.
    if (!mouseDown_) return;
    message = mouseDown_ + 1;
    mouseDown_ = 0;
  }
  Api().input_mouse(message, x, y);
}

void EngineView::OnPointer(PointerRoutedEventArgs const& args, int32_t state) {
  auto pointer = args.Pointer();
  auto point = args.GetCurrentPoint(input_);
  int32_t x = 0, y = 0;
  if (!ToFrame(point.Position(), &x, &y)) return;
  const uint32_t id = pointer.PointerId();
  if (pointer.PointerDeviceType() ==
      winrt::Windows::Devices::Input::PointerDeviceType::Mouse) {
    if (state == 0) byMouse_ = true;
    if (Api().input_mouse) OnMouse(point, state, x, y);
    return;
  }
  if (state == 0) byMouse_ = false;
  if (!Api().input_touch) return;
  auto it = std::find_if(fingers_.begin(), fingers_.end(),
                         [id](Finger const& f) { return f.id == id; });
  if (state == 0) {
    if (it == fingers_.end()) {
      fingers_.push_back(Finger{id, x, y});
    } else {
      it->x = x;
      it->y = y;
    }
  } else if (it == fingers_.end()) {
    return;  // a finger we never saw go down, or already up
  }
  // Straight to the engine, from inside the event, as gecko-w10m does: a
  // touch that waits for the dispatcher to be idle arrives late and in
  // bursts, and a scroll made of bursts jerks. Points the system coalesced
  // into this one are sent first, each earlier by the gap between their own
  // timestamps.
  int64_t now = Qpc();
  {
    // When the digitizer saw the finger, where its clock is the performance
    // counter's: this handler runs some milliseconds later, and not the same
    // few each time, which Chromium -- it resamples a scroll to the frame's
    // time from these -- shows as judder. A timestamp that is not a recent
    // moment on that clock is not used.
    const int64_t seen = QpcAt(point.Timestamp());
    const bool usable = seen <= now && now - seen < QpcFrequency() / 4;
    static bool noted = false;
    if (!noted) {
      noted = true;
      Log::Write(usable
                     ? L"touch: times from the digitizer; the handler ran " +
                           std::to_wstring((now - seen) * 1000 /
                                           QpcFrequency()) +
                           L" ms later"
                     : L"touch: the digitizer's clock is not the performance "
                       L"counter's; times taken in the handler");
    }
    if (usable) now = seen;
  }
  if (state == 1) {
    // A digitizer reports a resting finger over and over; a page (a video
    // player reading touches itself) takes any touchmove for a drag. Three
    // device pixels is about one CSS pixel (gecko-w10m).
    if (std::abs(x - it->x) + std::abs(y - it->y) < 3) return;
    const uint64_t current = point.Timestamp();
    try {
      auto points = args.GetIntermediatePoints(input_);  // newest first
      for (uint32_t i = points.Size(); i-- > 0;) {
        auto earlier = points.GetAt(i);
        const uint64_t stamp = earlier.Timestamp();
        if (stamp >= current) continue;
        int32_t ex = 0, ey = 0;
        if (!ToFrame(earlier.Position(), &ex, &ey)) continue;
        const int64_t age =
            static_cast<int64_t>(current - stamp) * QpcFrequency() / 1000000;
        Api().input_touch(static_cast<int32_t>(id), 1, ex, ey, now - age);
      }
    } catch (winrt::hresult_error const&) {
    }
    it->x = x;
    it->y = y;
  }
  Api().input_touch(static_cast<int32_t>(id), state, x, y, now);
  if (state >= 2) fingers_.erase(it);
}

// The modifier keys held now, as the engine's bits (1 shift, 2 control,
// 4 alt).
static uint32_t HeldModifiers() {
  uint32_t modifiers = 0;
  try {
    auto window = CoreWindow::GetForCurrentThread();
    using winrt::Windows::System::VirtualKey;
    auto down = [&window](VirtualKey key) {
      return (window.GetKeyState(key) & CoreVirtualKeyStates::Down) ==
             CoreVirtualKeyStates::Down;
    };
    if (down(VirtualKey::Shift)) modifiers |= 1;
    if (down(VirtualKey::Control)) modifiers |= 2;
    if (down(VirtualKey::Menu)) modifiers |= 4;
  } catch (winrt::hresult_error const&) {
  }
  return modifiers;
}

void EngineView::SendKey(int32_t vkey) {
  if (!Api().input_key) return;
  const uint32_t modifiers = HeldModifiers();
  Api().input_key(vkey, 1, modifiers);
  Api().input_key(vkey, 0, modifiers);
}

void EngineView::WireKeyboard() {
  // The phone's keyboard types into this and nothing else. It is the size of
  // the page behind it (a one-pixel control is not somewhere the system lets
  // anyone type) and invisible, because the field being typed into is drawn
  // by the engine inside the picture.
  sink_ = TextBox();
  sink_.Opacity(0);
  sink_.HorizontalAlignment(HorizontalAlignment::Stretch);
  sink_.VerticalAlignment(VerticalAlignment::Stretch);
  sink_.AcceptsReturn(false);
  sink_.IsSpellCheckEnabled(false);
  sink_.IsTextPredictionEnabled(false);
  // Out of the Tab order, so nothing but the page's own request focuses it;
  // a control that is not a tab stop cannot take focus at all, so it becomes
  // one just for that (FollowTextInput).
  sink_.IsTabStop(false);

  // Focus went elsewhere (the address bar, a dialog): the page no longer
  // has the keyboard. The engine hears that the keyboard went, so the next
  // tap on a field asks for it again.
  sink_.LostFocus([this](IInspectable const&, RoutedEventArgs const&) {
    sink_.IsTabStop(false);
    if (!typing_) return;
    typing_ = false;
    if (Api().set_keyboard_inset) Api().set_keyboard_inset(0);
  });

  // The on-screen keyboard edits the focused control through the text
  // services; the only sign of a keystroke is that the control's text
  // changed. What lands here goes to the engine and the sink is emptied, so
  // the next change is a clean delta.
  sink_.TextChanged([this](IInspectable const&, TextChangedEventArgs const&) {
    if (clearing_) return;
    auto typed = sink_.Text();
    if (typed.empty()) return;
    if (Api().input_text && typing_) {
      Api().input_text(reinterpret_cast<const uint16_t*>(typed.c_str()),
                       static_cast<int32_t>(typed.size()));
    }
    clearing_ = true;
    sink_.Text(L"");
    clearing_ = false;
  });

  // A paste is the engine's: Ctrl+V reaches it as a key (below) and it
  // pastes from its own clipboard, which holds the phone's. The sink pasting
  // as well put the text in twice.
  sink_.Paste([](IInspectable const&, TextControlPasteEventArgs const& args) {
    args.Handled(true);
  });

  // Keys that make no text arrive as keys; the rest arrive as the sink's
  // text, never twice. Registered for handled events too: the TextBox marks
  // its editing keys (Back, Delete, the arrows) handled before anyone else
  // sees them.
  sink_.AddHandler(
      UIElement::KeyDownEvent(),
      box_value(KeyEventHandler([this](IInspectable const&,
                                       KeyRoutedEventArgs const& args) {
        if (!typing_) return;
        const int32_t code = static_cast<int32_t>(args.Key());
        // A keyboard's shortcuts (Ctrl+A, C, V, X, Z; Ctrl+L, T, W...)
        // make no text: with Ctrl or Alt held, a letter or a digit goes to
        // the engine as a key.
        if ((HeldModifiers() & (2 | 4)) &&
            ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9'))) {
          SendKey(code);
          args.Handled(true);
          return;
        }
        switch (code) {
          case 8:   // Back
          case 9:   // Tab
          case 13:  // Enter
          case 27:  // Escape
          case 33: case 34: case 35: case 36:  // PageUp, PageDown, End, Home
          case 37: case 38: case 39: case 40:  // arrows
          case 46:  // Delete
            SendKey(code);
            args.Handled(true);
            break;
          default:
            break;
        }
      })),
      true);

  // The keyboard covers the bottom of the screen. The room keeps its size;
  // the engine is told how much is covered and insets the page, which
  // scrolls the field being typed into above it. Posted, never done inside
  // the pane's own handler: changing anything while the pane animates was
  // the last thing logged before every such crash (gecko-w10m).
  auto pane = InputPane::GetForCurrentView();
  pane.Showing([this](InputPane const&,
                      InputPaneVisibilityEventArgs const& args) {
    const Rect occluded = args.OccludedRect();
    args.EnsuredFocusedElementInView(true);
    PostToUi([this, occluded]() { ReportKeyboard(occluded); });
  });
  pane.Hiding([this](InputPane const&,
                     InputPaneVisibilityEventArgs const& args) {
    args.EnsuredFocusedElementInView(true);
    PostToUi([this]() { ReportKeyboard(Rect{0, 0, 0, 0}); });
  });
}

bool EngineView::SinkFocused() const {
  try {
    auto focused = FocusManager::GetFocusedElement();
    return focused && focused == sink_;
  } catch (winrt::hresult_error const&) {
    return false;
  }
}

void EngineView::ReportKeyboard(Rect occluded) {
  if (!Api().set_keyboard_inset) return;
  int32_t pixels = 0;
  // Only a keyboard typing into the page covers the page: one raised for the
  // address bar is the chrome's business.
  if (occluded.Height > 0 && SinkFocused()) {
    // What the keyboard covers of the room, whatever lies below it.
    try {
      const Point origin =
          root_.TransformToVisual(nullptr).TransformPoint(Point{0, 0});
      const double bottom = origin.Y + root_.ActualHeight();
      const double covered = std::max(0.0, bottom - occluded.Y);
      pixels = static_cast<int32_t>(covered * rawPerView_ + 0.5);
    } catch (winrt::hresult_error const&) {
      pixels = static_cast<int32_t>(occluded.Height * rawPerView_ + 0.5);
    }
  }
  Log::Write(L"view: keyboard covers " + std::to_wstring(pixels) + L" px");
  Api().set_keyboard_inset(pixels);
}

void EngineView::ApplyInputKind(int32_t kind) {
  if (kind == sinkKind_) return;
  sinkKind_ = kind;
  InputScopeNameValue value = InputScopeNameValue::Default;
  switch (kind) {
    case 2: value = InputScopeNameValue::Password; break;
    case 3: value = InputScopeNameValue::Search; break;
    case 4: value = InputScopeNameValue::EmailSmtpAddress; break;
    case 5: value = InputScopeNameValue::Number; break;
    case 6: value = InputScopeNameValue::TelephoneNumber; break;
    case 7: value = InputScopeNameValue::Url; break;
    default: break;
  }
  InputScope scope;
  InputScopeName name;
  name.NameValue(value);
  scope.Names().Append(name);
  sink_.InputScope(scope);
}

// Whether the keyboard has something to do: a new request for it, a request
// withdrawn while it is up, or another kind of field while typing.
bool EngineView::TextInputChanged(uint32_t state) const {
  const uint32_t kind = state & kKindMask;
  const bool wanted = kind && (state & kKeyboardWanted);
  // A field clicked with a mouse asks for no keyboard (Chrome shows the
  // on-screen one for touch and pen only) yet has one to type with.
  const bool clicked = kind && !wanted && byMouse_;
  const uint32_t serial = state >> 8;
  if (wanted && serial != lastTextSerial_) return true;
  if (clicked && !typing_) return true;
  if (!wanted && !clicked && typing_) return true;
  return typing_ && static_cast<int32_t>(kind) != sinkKind_;
}

void EngineView::FollowTextInput() {
  if (!Api().text_input_state) return;
  const uint32_t state = Api().text_input_state();
  const int32_t kind = static_cast<int32_t>(state & kKindMask);
  const bool wanted = kind && (state & kKeyboardWanted);
  const bool clicked = kind && !wanted && byMouse_;
  const uint32_t serial = state >> 8;
  auto pane = InputPane::GetForCurrentView();
  if (clicked && !typing_) {
    // A field clicked with a mouse (Continuum): the keyboard there, a USB or
    // Bluetooth one, types into the sink; no on-screen keyboard is raised.
    typing_ = true;
    ApplyInputKind(kind);
    sink_.IsTabStop(true);
    const bool focused = sink_.Focus(FocusState::Programmatic);
    Log::Write(std::wstring(L"view: text field clicked, sink ") +
               (focused ? L"focused" : L"refused"));
    return;
  }
  if (wanted && serial != lastTextSerial_) {
    // The page asked for the keyboard: the user tapped a field (a page that
    // focuses one by itself does not ask).
    lastTextSerial_ = serial;
    typing_ = true;
    ApplyInputKind(kind);
    sink_.IsTabStop(true);
    const bool focused = sink_.Focus(FocusState::Programmatic);
    // Focus alone raises the keyboard only for a focus from a touch, and
    // this one came from the engine, so ask outright as well.
    const bool shown = pane.TryShow();
    Log::Write(std::wstring(L"view: text field, sink ") +
               (focused ? L"focused" : L"refused") + L", keyboard " +
               (shown ? L"shown" : L"refused"));
    // A keyboard already up (it was the address bar's) raises no Showing:
    // report what it covers now.
    try {
      if (pane.OccludedRect().Height > 0) ReportKeyboard(pane.OccludedRect());
    } catch (winrt::hresult_error const&) {
    }
  } else if (!wanted && !clicked && typing_) {
    typing_ = false;
    pane.TryHide();
  } else if (typing_ && kind != sinkKind_) {
    ApplyInputKind(kind);
  }
}

void EngineView::HideKeyboard() {
  typing_ = false;
  try {
    InputPane::GetForCurrentView().TryHide();
  } catch (winrt::hresult_error const&) {
  }
}

bool EngineView::HandleBack() {
  // The keyboard first: closing it is all a back press is for while it is up.
  bool keyboard = false;
  try {
    keyboard = InputPane::GetForCurrentView().Visible();
  } catch (winrt::hresult_error const&) {
  }
  if (keyboard) {
    PostToUi([]() {
      try {
        InputPane::GetForCurrentView().TryHide();
      } catch (winrt::hresult_error const&) {
      }
    });
    return true;
  }
  return Api().back_button && Api().back_button() == 1;
}

}  // namespace venetium_w10m::client
