// EngineView.cpp
#include "pch.h"

#include "client/EngineView.h"

#include <robuffer.h>

#include <algorithm>

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

}  // namespace

EngineView::EngineView(double rawPerView) : rawPerView_(rawPerView) {
  root_ = Grid();
  root_.Background(SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(
      255, 255, 255, 255)));

  WireKeyboard();
  root_.Children().Append(sink_);

  image_ = Image();
  image_.Stretch(Stretch::Fill);
  image_.HorizontalAlignment(HorizontalAlignment::Stretch);
  image_.VerticalAlignment(VerticalAlignment::Stretch);
  // Every finger goes to the engine; XAML is not to pan or zoom anything.
  image_.ManipulationMode(ManipulationModes::None);
  root_.Children().Append(image_);

  image_.PointerPressed([this](IInspectable const&,
                               PointerRoutedEventArgs const& args) {
    image_.CapturePointer(args.Pointer());
    OnPointer(args, 0);
    args.Handled(true);
  });
  image_.PointerMoved([this](IInspectable const&,
                             PointerRoutedEventArgs const& args) {
    OnPointer(args, 1);
    args.Handled(true);
  });
  image_.PointerReleased([this](IInspectable const&,
                                PointerRoutedEventArgs const& args) {
    OnPointer(args, 2);
    image_.ReleasePointerCapture(args.Pointer());
    args.Handled(true);
  });
  image_.PointerCanceled([this](IInspectable const&,
                                PointerRoutedEventArgs const& args) {
    OnPointer(args, 3);
    args.Handled(true);
  });
  // Also after an ordinary release (ReleasePointerCapture): by then the
  // finger or button is already up and this sends nothing.
  image_.PointerCaptureLost([this](IInspectable const&,
                                   PointerRoutedEventArgs const& args) {
    OnPointer(args, 3);
  });
  image_.PointerWheelChanged([this](IInspectable const&,
                                    PointerRoutedEventArgs const& args) {
    int32_t x = 0, y = 0;
    auto point = args.GetCurrentPoint(image_);
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
  const int32_t w = width_ > 0 ? width_ : roomWidth_;
  const int32_t h = height_ > 0 ? height_ : roomHeight_;
  if (w <= 0 || h <= 0) return false;
  const double sx = image_.ActualWidth() > 0 ? w / image_.ActualWidth()
                                             : rawPerView_;
  const double sy = image_.ActualHeight() > 0 ? h / image_.ActualHeight()
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
  // The performance counter as the event is handled; points the system
  // coalesced into this one get theirs from the gaps between their own
  // timestamps (microseconds).
  const int64_t now = Qpc();
  auto pointer = args.Pointer();
  auto point = args.GetCurrentPoint(image_);
  int32_t x = 0, y = 0;
  if (!ToFrame(point.Position(), &x, &y)) return;
  const uint32_t id = pointer.PointerId();
  if (pointer.PointerDeviceType() ==
      winrt::Windows::Devices::Input::PointerDeviceType::Mouse) {
    if (Api().input_mouse) OnMouse(point, state, x, y);
    return;
  }
  if (!Api().input_touch) return;
  auto it = std::find(fingers_.begin(), fingers_.end(), id);
  if (state == 0) {
    if (it == fingers_.end()) fingers_.push_back(id);
  } else if (it == fingers_.end()) {
    return;  // a finger we never saw go down, or already up
  } else if (state >= 2) {
    fingers_.erase(it);
  }
  if (state == 1) {
    try {
      // Newest first, the current point among them.
      auto points = args.GetIntermediatePoints(image_);
      const uint64_t current = point.Timestamp();
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
  }
  Api().input_touch(static_cast<int32_t>(id), state, x, y, now);
}

void EngineView::SendKey(int32_t vkey) {
  if (!Api().input_key) return;
  Api().input_key(vkey, 1, 0);
  Api().input_key(vkey, 0, 0);
}

void EngineView::WireKeyboard() {
  // The phone's keyboard types into this and nothing else. It is the size of
  // the page behind it (a one-pixel control is not somewhere the system lets
  // anyone type) and invisible, because the field being typed into is drawn
  // by the engine inside the picture. It must stay a tab stop: a control
  // that is not one cannot take focus at all.
  sink_ = TextBox();
  sink_.Opacity(0);
  sink_.HorizontalAlignment(HorizontalAlignment::Stretch);
  sink_.VerticalAlignment(VerticalAlignment::Stretch);
  sink_.AcceptsReturn(false);
  sink_.IsSpellCheckEnabled(false);
  sink_.IsTextPredictionEnabled(false);

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

void EngineView::ReportKeyboard(Rect occluded) {
  if (!Api().set_keyboard_inset) return;
  int32_t pixels = 0;
  if (occluded.Height > 0) {
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
  const uint32_t serial = state >> 8;
  if (wanted && serial != lastTextSerial_) return true;
  if (!wanted && typing_) return true;
  return typing_ && static_cast<int32_t>(kind) != sinkKind_;
}

void EngineView::FollowTextInput() {
  if (!Api().text_input_state) return;
  const uint32_t state = Api().text_input_state();
  const int32_t kind = static_cast<int32_t>(state & kKindMask);
  const bool wanted = kind && (state & kKeyboardWanted);
  const uint32_t serial = state >> 8;
  auto pane = InputPane::GetForCurrentView();
  if (wanted && serial != lastTextSerial_) {
    // The page asked for the keyboard: the user tapped a field (a page that
    // focuses one by itself does not ask).
    lastTextSerial_ = serial;
    typing_ = true;
    ApplyInputKind(kind);
    const bool focused = sink_.Focus(FocusState::Programmatic);
    // Focus alone raises the keyboard only for a focus from a touch, and
    // this one came from the engine, so ask outright as well.
    const bool shown = pane.TryShow();
    Log::Write(std::wstring(L"view: text field, sink ") +
               (focused ? L"focused" : L"refused") + L", keyboard " +
               (shown ? L"shown" : L"refused"));
  } else if (!wanted && typing_) {
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
