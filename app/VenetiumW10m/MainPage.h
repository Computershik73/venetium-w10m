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
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "client/EngineView.h"

namespace venetium_w10m {

class MainPage {
 public:
  MainPage();

  winrt::Windows::UI::Xaml::UIElement Root() const { return root_; }

  // Loads the engine and starts it; the splash stays until its first frame.
  void Start(std::wstring initialUrl);
  // A URL from outside (a link in another app, this being the default
  // browser): a new tab.
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
  void ApplyVisibleBounds();

  void Navigate(std::wstring const& text);
  void RefreshChrome();
  void ShowTabs(bool show);
  void RebuildTabList();
  void SetFullscreen(bool on);
  void ShowDialog(int32_t id, int32_t kind, std::wstring const& message,
                  std::wstring const& defaultText);
  void ChooseFiles(int32_t id, std::wstring const& accept, bool multiple,
                   bool save);
  void ShowNotice(std::wstring const& text);
  void ShowLog();
  void ShowAbout();
  void ShowEngineSwitches();
  void SendPhoneClipboard();
  void ReportScreen();
  void OnEngineStopped(int32_t code);

  TabInfo* Selected();

  std::unique_ptr<client::EngineView> engineView_;
  std::map<int32_t, TabInfo> tabs_;
  std::vector<int32_t> tabOrder_;
  int32_t selected_ = 0;
  bool editingAddress_ = false;
  bool fullscreen_ = false;
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
