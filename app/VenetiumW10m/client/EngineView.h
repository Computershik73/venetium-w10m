// EngineView.h -- the page on screen, and the phone's input back to the page.
//
// The engine composites each frame into memory and the view copies the
// newest one, once per display refresh, into a WriteableBitmap shown by an
// Image the size of the content area (venetium_w10m_frame_copy: an idle page
// costs one lock and one comparison per refresh).
//
// Fingers go to the engine as touch points in frame pixels; Aura's gesture
// recogniser makes taps, scrolls, flings and pinches of them. Typing: the
// phone raises its keyboard only for a focused text control, so an invisible
// TextBox takes the keyboard's edits and passes them on.
//
// Adapted from gecko-w10m's client/EngineView, which found out the hard way
// how a Windows 10 Mobile keyboard and back button behave.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace venetium_w10m::client {

class EngineView {
 public:
  explicit EngineView(double rawPerView);

  // What goes in the page's content row: the picture and, under it, the
  // keyboard's text sink.
  winrt::Windows::UI::Xaml::FrameworkElement Root() const { return root_; }

  // Starts copying frames and reporting the room. Call once the engine has
  // been asked to start.
  void Start();
  // The display's scale (raw pixels per view pixel) changed.
  void SetScale(double rawPerView);

  // Called once, when the engine has drawn its first frame.
  void OnFirstFrame(std::function<void()> handler) {
    firstFrame_ = std::move(handler);
  }

  // The phone's back button: true if the page used it.
  bool HandleBack();
  // Takes the keyboard away (the address bar wants it, or a dialog).
  void HideKeyboard();

 private:
  void Tick();
  void EnsureBitmap(int32_t width, int32_t height);
  void PushSize();
  bool ToFrame(winrt::Windows::Foundation::Point const& point, int32_t* x,
               int32_t* y) const;
  void OnPointer(winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&
                     args,
                 int32_t state);
  void WireKeyboard();
  void FollowTextInput();
  void ApplyInputKind(int32_t kind);
  void SendKey(int32_t vkey);
  static void PostToUi(std::function<void()> work);

  winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Image image_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBox sink_{nullptr};
  winrt::Windows::UI::Xaml::Media::Imaging::WriteableBitmap bitmap_{nullptr};
  winrt::event_token tick_{};

  double rawPerView_ = 1.0;
  int32_t width_ = 0;    // the bitmap
  int32_t height_ = 0;
  int32_t roomWidth_ = 0;  // the content area, raw pixels
  int32_t roomHeight_ = 0;
  uint64_t seen_ = 0;
  std::function<void()> firstFrame_;

  // Fingers on the glass, by XAML pointer id.
  std::vector<uint32_t> fingers_;
  bool touched_ = false;

  // Text input.
  bool typing_ = false;
  bool clearing_ = false;
  bool followPending_ = false;
  uint32_t lastTextSerial_ = 0;
  int32_t sinkKind_ = -1;
};

}  // namespace venetium_w10m::client
