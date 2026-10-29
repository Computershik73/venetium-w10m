// MainPage.h -- the browser's chrome, in XAML, built in code.
//
// Desktop Venetium's interface is Chrome's views UI: a tab strip and an
// omnibox drawn for a mouse on a large window. A phone gets a browser chrome
// of its own here, the way Chrome for Android and the iOS browsers have one:
// an address bar with the tab count and a menu, a thin progress line, the
// page below, and a tab switcher over it. Everything about the page comes
// from the engine's events; everything the page should do goes back through
// the engine's C interface.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "client/EngineView.h"

namespace venetium_w10m {

// A tile on Start for an installed app opens the app with these arguments
// followed by the app's id.
inline constexpr wchar_t kAppTileArguments[] = L"venetium-app:";

class MainPage {
 public:
  MainPage();

  winrt::Windows::UI::Xaml::UIElement Root() const { return root_; }

  // Loads the engine and starts it; the splash stays until its first frame.
  void Start(std::wstring initialUrl);
  // A URL from outside (a link in another app, this being the default
  // browser): a new tab.
  // An installed app's tile on Start was tapped (its arguments are
  // kAppTileArguments and the app's id): the engine opens the app.
  void LaunchApp(std::wstring const& app_id);
  void OpenExternalUrl(std::wstring const& url);
  void OnSuspending();
  void OnResuming();

 private:
  struct TabInfo {
    std::wstring url;
    std::wstring title;
    float progress = 1.0f;
    bool loading = false;
    bool canBack = false;
    bool canForward = false;
    bool secure = false;
  };

  void BuildUi();
  void BuildMenu();
  void WireEngineEvents();
  // The status bar in the chrome's colours.
  void ColorStatusBar();
  // The status bar hidden (or shown again) with the toolbar.
  void SlideStatusBar(bool shown);
  void ApplyVisibleBounds();

  void Navigate(std::wstring const& text);
  void RefreshChrome();
  void ShowTabs(bool show);
  void RebuildTabList();
  void SetFullscreen(bool on);
  void ShowDialog(int32_t id, int32_t kind, std::wstring const& message,
                  std::wstring const& defaultText);
  void PinAppTile(std::wstring id, std::wstring name, std::wstring icon);
  // Asks for the folder downloads are to go to; |then| hears it (nullptr
  // when none was picked).
  void PickDownloadsFolder(
      std::function<void(winrt::Windows::Storage::StorageFolder)> then);
  void ChooseFiles(int32_t id, std::wstring const& accept, bool multiple,
                   bool save);
  void ShowNotice(std::wstring const& text);
  void ShowLog();
  void ShowAbout();
  void ShowEngineSwitches();
  void SendPhoneClipboard();
  void ReportScreen();
  void OnEngineStopped(int32_t code);
  // Chrome's own interface is drawn by the engine: the shell's bar, tabs
  // and menu go, and the page is all panel.
  void EnterChromeUi();

  TabInfo* Selected();

  std::unique_ptr<client::EngineView> engineView_;
  std::map<int32_t, TabInfo> tabs_;
  std::vector<int32_t> tabOrder_;
  int32_t selected_ = 0;
  bool editingAddress_ = false;
  bool fullscreen_ = false;
  // The status bar hidden with the toolbar as a page scrolled down.
  bool statusSlid_ = false;
  // While downloads go, Windows is asked to keep the app running in the
  // background (an extended execution session): suspended, it would stop
  // them. downloadsGoing_ is the engine's count.
  void KeepRunningForDownloads(int32_t count);
  winrt::Windows::ApplicationModel::ExtendedExecution::ExtendedExecutionSession
      downloadSession_{nullptr};
  int32_t downloadsGoing_ = 0;
  bool downloadSessionAsked_ = false;
  // The phone's media controls -- the lock screen's, the volume flyout's --
  // for what a page plays (the engine's media_state); their buttons go back
  // to the page. Playing, the app keeps running in the background (the
  // backgroundMediaPlayback capability).
  void ShowMedia(int32_t state, std::wstring const& title,
                 std::wstring const& artist, std::wstring const& album,
                 std::wstring const& artwork, int32_t buttons);
  void ShowMediaArtwork(std::wstring const& artwork);
  // The controls, made at start, before any sound (see the .cpp).
  void SetUpMediaControls();
  // The system's player the controls belong to, playing silence while a
  // page plays (see the .cpp); nothing when the phone has none.
  void LoadSilence();
  void PlaySilence(bool playing);
  winrt::Windows::Media::Playback::MediaPlayer silentPlayer_{nullptr};
  bool silenceLoaded_ = false;
  bool silencePlaying_ = false;
  // The phone's consent for the camera (1), the microphone (2) or the
  // location (3), which a site may now use: the system's prompt the first
  // time, the setting when it was turned off; the answer goes to the engine.
  void AskDeviceAccess(int32_t kind);
  winrt::Windows::Media::SystemMediaTransportControls media_{nullptr};
  std::wstring mediaTitle_;
  std::wstring mediaArtist_;
  std::wstring mediaAlbum_;
  std::wstring mediaArtwork_;
  // What the log last said of the controls.
  int32_t mediaState_ = -1;
  int32_t mediaButtons_ = -1;
  // Which artwork is the one to show, as files are opened asynchronously.
  int32_t mediaArtworkSerial_ = 0;
  bool chromeUi_ = false;
  std::wstring pendingUrl_;
  double rawPerView_ = 1.0;

  winrt::Windows::UI::Xaml::Controls::Grid root_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Grid chrome_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBox address_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button tabsButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock tabsCount_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button reloadButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::FontIcon reloadIcon_{nullptr};
  winrt::Windows::UI::Xaml::Controls::FontIcon lockIcon_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Button menuButton_{nullptr};
  winrt::Windows::UI::Xaml::Controls::MenuFlyout menu_{nullptr};
  winrt::Windows::UI::Xaml::Controls::ToggleMenuFlyoutItem desktopItem_{
      nullptr};
  winrt::Windows::UI::Xaml::Controls::MenuFlyoutItem forwardItem_{nullptr};
  winrt::Windows::UI::Xaml::Controls::ProgressBar progress_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Grid content_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Grid splash_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock splashText_{nullptr};
  winrt::Windows::UI::Xaml::Controls::ProgressRing splashRing_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Grid tabsPanel_{nullptr};
  winrt::Windows::UI::Xaml::Controls::StackPanel tabsList_{nullptr};
  winrt::Windows::UI::Xaml::Controls::Border notice_{nullptr};
  winrt::Windows::UI::Xaml::Controls::TextBlock noticeText_{nullptr};
  winrt::Windows::UI::Xaml::DispatcherTimer noticeTimer_{nullptr};
};

}  // namespace venetium_w10m
