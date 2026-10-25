// MainPage.cpp
#include "pch.h"

#include "MainPage.h"

#include <algorithm>
#include <thread>

#include "client/CrashLog.h"
#include "client/FileExport.h"
#include "client/Log.h"
#include "client/Settings.h"
#include "engine/EngineHost.h"

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Metadata;
using namespace winrt::Windows::Graphics::Display;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;
using namespace winrt::Windows::System;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::ViewManagement;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Controls::Primitives;
using namespace winrt::Windows::UI::Xaml::Input;
using namespace winrt::Windows::UI::Xaml::Media;

namespace venetium_w10m {

using client::Log;
using client::Settings;
using engine::EngineHost;
using engine::Narrow;

namespace {

Color ChromeBg() { return ColorHelper::FromArgb(255, 0x1c, 0x1b, 0x22); }
Color ChromeFg() { return ColorHelper::FromArgb(255, 0xf0, 0xf0, 0xf4); }
Color FieldBg() { return ColorHelper::FromArgb(255, 0x2b, 0x2a, 0x33); }
Color Accent() { return ColorHelper::FromArgb(255, 0x7b, 0x5c, 0xff); }
Color Dim() { return ColorHelper::FromArgb(255, 0x9a, 0x99, 0xa6); }

SolidColorBrush Brush(Color c) { return SolidColorBrush(c); }

const engine::Api& Api() { return EngineHost::Get().api(); }

Button IconButton(std::wstring_view glyph) {
  Button b;
  FontIcon icon;
  icon.Glyph(hstring(glyph));
  icon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
  icon.FontSize(18);
  icon.Foreground(Brush(ChromeFg()));
  b.Content(icon);
  b.MinWidth(44);
  b.MinHeight(40);
  b.Padding(ThicknessHelper::FromUniformLength(8));
  b.Background(Brush(ColorHelper::FromArgb(0, 0, 0, 0)));
  b.BorderThickness(ThicknessHelper::FromUniformLength(0));
  b.UseSystemFocusVisuals(false);
  return b;
}

std::wstring DisplayUrl(std::wstring const& url) {
  // What a person reads in the bar: no scheme, no trailing slash.
  std::wstring text = url;
  for (const wchar_t* scheme : {L"https://", L"http://"}) {
    if (text.rfind(scheme, 0) == 0) {
      text = text.substr(wcslen(scheme));
      break;
    }
  }
  if (text.rfind(L"www.", 0) == 0) text = text.substr(4);
  if (!text.empty() && text.back() == L'/' &&
      text.find(L'/') == text.size() - 1) {
    text.pop_back();
  }
  return text;
}

}  // namespace

MainPage::MainPage() {
  try {
    rawPerView_ = DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
  } catch (...) {
    rawPerView_ = 1.0;
  }
  engineView_ = std::make_unique<client::EngineView>(rawPerView_);
  BuildUi();
  WireEngineEvents();
}

void MainPage::BuildUi() {
  root_ = Grid();
  root_.Background(Brush(ChromeBg()));
  auto row = [](GridLength length) {
    RowDefinition r;
    r.Height(length);
    return r;
  };
  root_.RowDefinitions().Append(row(GridLengthHelper::Auto()));
  root_.RowDefinitions().Append(row(GridLengthHelper::Auto()));
  root_.RowDefinitions().Append(
      row(GridLengthHelper::FromValueAndType(1, GridUnitType::Star)));

  // --- the bar: tabs | address | reload | menu ---------------------------
  chrome_ = Grid();
  chrome_.Background(Brush(ChromeBg()));
  chrome_.Padding(ThicknessHelper::FromLengths(2, 4, 2, 4));
  for (auto width : {GridLengthHelper::Auto(),
                     GridLengthHelper::FromValueAndType(1, GridUnitType::Star),
                     GridLengthHelper::Auto(), GridLengthHelper::Auto()}) {
    ColumnDefinition c;
    c.Width(width);
    chrome_.ColumnDefinitions().Append(c);
  }

  tabsButton_ = Button();
  tabsButton_.MinWidth(44);
  tabsButton_.MinHeight(40);
  tabsButton_.Background(Brush(ColorHelper::FromArgb(0, 0, 0, 0)));
  tabsButton_.BorderThickness(ThicknessHelper::FromUniformLength(0));
  tabsButton_.UseSystemFocusVisuals(false);
  {
    Border box;
    box.BorderBrush(Brush(ChromeFg()));
    box.BorderThickness(ThicknessHelper::FromUniformLength(1.5));
    box.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
    box.MinWidth(24);
    box.Height(24);
    tabsCount_ = TextBlock();
    tabsCount_.Text(L"1");
    tabsCount_.FontSize(13);
    tabsCount_.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    tabsCount_.Foreground(Brush(ChromeFg()));
    tabsCount_.HorizontalAlignment(HorizontalAlignment::Center);
    tabsCount_.VerticalAlignment(VerticalAlignment::Center);
    box.Child(tabsCount_);
    tabsButton_.Content(box);
  }
  Grid::SetColumn(tabsButton_, 0);
  chrome_.Children().Append(tabsButton_);

  Grid field;
  field.Margin(ThicknessHelper::FromLengths(2, 0, 2, 0));
  lockIcon_ = FontIcon();
  lockIcon_.Glyph(L"\uE72E");  // Lock
  lockIcon_.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
  lockIcon_.FontSize(12);
  lockIcon_.Foreground(Brush(Dim()));
  lockIcon_.HorizontalAlignment(HorizontalAlignment::Left);
  lockIcon_.VerticalAlignment(VerticalAlignment::Center);
  lockIcon_.Margin(ThicknessHelper::FromLengths(10, 0, 0, 0));
  lockIcon_.Visibility(Visibility::Collapsed);
  lockIcon_.IsHitTestVisible(false);

  address_ = TextBox();
  address_.PlaceholderText(L"Search or enter address");
  address_.FontSize(15);
  address_.MinHeight(36);
  address_.VerticalAlignment(VerticalAlignment::Center);
  address_.Background(Brush(FieldBg()));
  address_.Foreground(Brush(ChromeFg()));
  address_.BorderThickness(ThicknessHelper::FromUniformLength(0));
  address_.IsSpellCheckEnabled(false);
  address_.IsTextPredictionEnabled(false);
  {
    InputScope scope;
    InputScopeName name;
    name.NameValue(InputScopeNameValue::Url);
    scope.Names().Append(name);
    address_.InputScope(scope);
  }
  field.Children().Append(address_);
  field.Children().Append(lockIcon_);
  Grid::SetColumn(field, 1);
  chrome_.Children().Append(field);

  reloadButton_ = IconButton(L"\uE72C");  // Refresh
  reloadIcon_ = reloadButton_.Content().as<FontIcon>();
  Grid::SetColumn(reloadButton_, 2);
  chrome_.Children().Append(reloadButton_);

  menuButton_ = IconButton(L"\uE712");  // More
  Grid::SetColumn(menuButton_, 3);
  chrome_.Children().Append(menuButton_);
  BuildMenu();

  Grid::SetRow(chrome_, 0);
  root_.Children().Append(chrome_);

  progress_ = ProgressBar();
  progress_.Minimum(0);
  progress_.Maximum(1);
  progress_.Height(2);
  progress_.MinHeight(2);
  progress_.Foreground(Brush(Accent()));
  progress_.Background(Brush(ChromeBg()));
  progress_.Opacity(0);
  Grid::SetRow(progress_, 1);
  root_.Children().Append(progress_);

  // --- the page, and what goes over it ------------------------------------
  content_ = Grid();
  Grid::SetRow(content_, 2);
  content_.Children().Append(engineView_->Root());

  splash_ = Grid();
  splash_.Background(Brush(ChromeBg()));
  {
    StackPanel stack;
    stack.VerticalAlignment(VerticalAlignment::Center);
    stack.HorizontalAlignment(HorizontalAlignment::Center);
    TextBlock name;
    name.Text(L"Venetium");
    name.FontSize(32);
    name.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiLight());
    name.Foreground(Brush(ChromeFg()));
    name.HorizontalAlignment(HorizontalAlignment::Center);
    stack.Children().Append(name);
    splashText_ = TextBlock();
    splashText_.Text(L"Starting the engine\u2026");
    splashText_.FontSize(14);
    splashText_.Foreground(Brush(Dim()));
    splashText_.TextWrapping(TextWrapping::Wrap);
    splashText_.TextAlignment(TextAlignment::Center);
    splashText_.Margin(ThicknessHelper::FromLengths(24, 12, 24, 0));
    stack.Children().Append(splashText_);
    splashRing_ = ProgressRing();
    splashRing_.IsActive(true);
    splashRing_.Width(32);
    splashRing_.Height(32);
    splashRing_.Foreground(Brush(Accent()));
    splashRing_.Margin(ThicknessHelper::FromLengths(0, 24, 0, 0));
    stack.Children().Append(splashRing_);
    splash_.Children().Append(stack);
  }
  content_.Children().Append(splash_);

  tabsPanel_ = Grid();
  tabsPanel_.Background(Brush(ChromeBg()));
  tabsPanel_.Visibility(Visibility::Collapsed);
  {
    RowDefinition top;
    top.Height(GridLengthHelper::Auto());
    RowDefinition rest;
    rest.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    tabsPanel_.RowDefinitions().Append(top);
    tabsPanel_.RowDefinitions().Append(rest);
    Grid bar;
    bar.Padding(ThicknessHelper::FromLengths(16, 8, 8, 8));
    TextBlock title;
    title.Text(L"Tabs");
    title.FontSize(20);
    title.Foreground(Brush(ChromeFg()));
    title.VerticalAlignment(VerticalAlignment::Center);
    bar.Children().Append(title);
    Button add = IconButton(L"\uE710");  // Add
    add.HorizontalAlignment(HorizontalAlignment::Right);
    add.Click([this](auto&&, auto&&) {
      ShowTabs(false);
      if (Api().tab_create) {
        Api().tab_create(Narrow(Settings::HomePage()).c_str(), 1);
      }
    });
    bar.Children().Append(add);
    tabsPanel_.Children().Append(bar);
    ScrollViewer scroller;
    tabsList_ = StackPanel();
    tabsList_.Padding(ThicknessHelper::FromLengths(8, 0, 8, 8));
    scroller.Content(tabsList_);
    Grid::SetRow(scroller, 1);
    tabsPanel_.Children().Append(scroller);
  }
  content_.Children().Append(tabsPanel_);

  notice_ = Border();
  notice_.Background(Brush(ColorHelper::FromArgb(235, 0x2b, 0x2a, 0x33)));
  notice_.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
  notice_.Padding(ThicknessHelper::FromLengths(16, 12, 16, 12));
  notice_.Margin(ThicknessHelper::FromLengths(12, 0, 12, 16));
  notice_.VerticalAlignment(VerticalAlignment::Bottom);
  notice_.Visibility(Visibility::Collapsed);
  noticeText_ = TextBlock();
  noticeText_.Foreground(Brush(ChromeFg()));
  noticeText_.TextWrapping(TextWrapping::Wrap);
  notice_.Child(noticeText_);
  content_.Children().Append(notice_);
  noticeTimer_ = DispatcherTimer();
  noticeTimer_.Interval(std::chrono::seconds(4));
  noticeTimer_.Tick([this](auto&&, auto&&) {
    notice_.Visibility(Visibility::Collapsed);
    noticeTimer_.Stop();
  });

  root_.Children().Append(content_);

  // --- behaviour ---------------------------------------------------------
  address_.GotFocus([this](auto&&, auto&&) {
    editingAddress_ = true;
    lockIcon_.Visibility(Visibility::Collapsed);
    address_.Padding(ThicknessHelper::FromLengths(10, 6, 10, 6));
    if (TabInfo* tab = Selected()) address_.Text(hstring(tab->url));
    address_.SelectAll();
  });
  address_.LostFocus([this](auto&&, auto&&) {
    editingAddress_ = false;
    RefreshChrome();
  });
  address_.KeyDown([this](auto&&, KeyRoutedEventArgs const& e) {
    if (e.Key() == VirtualKey::Enter) {
      e.Handled(true);
      Navigate(std::wstring(address_.Text()));
    }
  });
  tabsButton_.Click([this](auto&&, auto&&) {
    ShowTabs(tabsPanel_.Visibility() != Visibility::Visible);
  });
  reloadButton_.Click([this](auto&&, auto&&) {
    TabInfo* tab = Selected();
    if (tab && tab->loading) {
      if (Api().tab_stop) Api().tab_stop(selected_);
    } else if (Api().tab_reload) {
      Api().tab_reload(selected_);
    }
  });

  // The phone's back button: the tab switcher, the address bar, then the
  // page (keyboard, fullscreen, history, a tab a page opened); only when none
  // of them wants it does it go to the phone and put the app away.
  SystemNavigationManager::GetForCurrentView().BackRequested(
      [this](auto&&, BackRequestedEventArgs const& args) {
        if (tabsPanel_.Visibility() == Visibility::Visible) {
          ShowTabs(false);
          args.Handled(true);
          return;
        }
        if (editingAddress_) {
          reloadButton_.Focus(FocusState::Programmatic);
          args.Handled(true);
          return;
        }
        if (engineView_->HandleBack()) {
          args.Handled(true);
        }
      });

  // The status bar and the navigation bar of a phone sit over the window;
  // the browser stays inside what is visible. UseVisible alone does not do
  // it on a phone (gecko-w10m): the room is padded by what the bars cover,
  // again whenever they change.
  auto view = ApplicationView::GetForCurrentView();
  view.SetDesiredBoundsMode(ApplicationViewBoundsMode::UseVisible);
  view.VisibleBoundsChanged([this](auto&&, auto&&) { ApplyVisibleBounds(); });
  ApplyVisibleBounds();
  if (ApiInformation::IsTypePresent(L"Windows.UI.ViewManagement.StatusBar")) {
    auto status = StatusBar::GetForCurrentView();
    status.BackgroundColor(ChromeBg());
    status.BackgroundOpacity(1.0);
    status.ForegroundColor(ChromeFg());
  }
  DisplayInformation::GetForCurrentView().DpiChanged(
      [this](DisplayInformation const& info, auto&&) {
        rawPerView_ = info.RawPixelsPerViewPixel();
        engineView_->SetScale(rawPerView_);
      });
}

void MainPage::BuildMenu() {
  menu_ = MenuFlyout();
  auto item = [this](std::wstring_view text, auto handler) {
    MenuFlyoutItem i;
    i.Text(hstring(text));
    i.Click([handler](auto&&, auto&&) { handler(); });
    menu_.Items().Append(i);
    return i;
  };
  item(L"New tab", [this]() {
    if (Api().tab_create) {
      Api().tab_create(Narrow(Settings::HomePage()).c_str(), 1);
    }
  });
  forwardItem_ = item(L"Forward", [this]() {
    if (Api().tab_forward) Api().tab_forward(selected_);
  });
  item(L"Home page", [this]() { Navigate(Settings::HomePage()); });
  menu_.Items().Append(MenuFlyoutSeparator());

  desktopItem_ = ToggleMenuFlyoutItem();
  desktopItem_.Text(L"Desktop site");
  desktopItem_.IsChecked(Settings::DesktopMode());
  desktopItem_.Click([this](auto&&, auto&&) {
    const bool desktop = desktopItem_.IsChecked();
    Settings::SetDesktopMode(desktop);
    if (Api().set_desktop_mode) Api().set_desktop_mode(desktop ? 1 : 0);
  });
  menu_.Items().Append(desktopItem_);

  MenuFlyoutSubItem search;
  search.Text(L"Search engine");
  const auto& engines = Settings::SearchEngines();
  for (int i = 0; i < (int)engines.size(); ++i) {
    ToggleMenuFlyoutItem choice;
    choice.Text(engines[i].name);
    choice.IsChecked(i == Settings::SearchEngineIndex());
    choice.Click([this, i](auto&&, auto&&) {
      Settings::SetSearchEngineIndex(i);
      if (Api().set_search_url) {
        Api().set_search_url(Narrow(Settings::SearchTemplate()).c_str());
      }
      BuildMenu();  // re-check the right one
    });
    search.Items().Append(choice);
  }
  menu_.Items().Append(search);

  ToggleMenuFlyoutItem jit;
  jit.Text(L"JavaScript JIT (restart)");
  jit.IsChecked(Settings::Jit());
  jit.Click([jit](auto&&, auto&&) { Settings::SetJit(jit.IsChecked()); });
  menu_.Items().Append(jit);
  menu_.Items().Append(MenuFlyoutSeparator());

  item(L"Copy link", [this]() {
    TabInfo* tab = Selected();
    if (!tab || tab->url.empty()) return;
    DataPackage package;
    package.SetText(hstring(tab->url));
    try {
      Clipboard::SetContent(package);
      ShowNotice(L"Link copied");
    } catch (winrt::hresult_error const& e) {
      Log::Write(L"clipboard: set failed", std::wstring(e.message()));
    }
  });
  ToggleMenuFlyoutItem hardware;
  hardware.Text(L"Hardware graphics (restart)");
  hardware.IsChecked(Settings::HardwareGraphics());
  hardware.Click([hardware](auto&&, auto&&) {
    Settings::SetHardwareGraphics(hardware.IsChecked());
  });
  menu_.Items().Append(hardware);
  item(L"Log", [this]() { ShowLog(); });
  item(L"Engine switches (restart)", [this]() { ShowEngineSwitches(); });
  item(L"About", [this]() { ShowAbout(); });
  item(L"Copy logs to Pictures", [this]() {
    std::thread([]() { Log::Mirror(); }).detach();
    ShowNotice(L"Logs go to Pictures\\Venetium logs");
  });
  menuButton_.Flyout(menu_);
}

void MainPage::WireEngineEvents() {
  engine::Events events;
  events.tabState = [this](int32_t tab, std::wstring url, std::wstring title,
                           float progress, bool loading, bool canBack,
                           bool canForward, bool secure) {
    TabInfo& info = tabs_[tab];
    if (std::find(tabOrder_.begin(), tabOrder_.end(), tab) == tabOrder_.end()) {
      tabOrder_.push_back(tab);
    }
    info.url = std::move(url);
    info.title = std::move(title);
    info.progress = progress;
    info.loading = loading;
    info.canBack = canBack;
    info.canForward = canForward;
    info.secure = secure;
    if (tab == selected_) RefreshChrome();
    if (tabsPanel_.Visibility() == Visibility::Visible) RebuildTabList();
  };
  events.tabOpened = [this](int32_t tab, int32_t opener, bool selected) {
    if (std::find(tabOrder_.begin(), tabOrder_.end(), tab) == tabOrder_.end()) {
      tabOrder_.push_back(tab);
    }
    tabs_[tab];
    if (selected) selected_ = tab;
    RefreshChrome();
  };
  events.tabClosed = [this](int32_t tab) {
    tabs_.erase(tab);
    tabOrder_.erase(std::remove(tabOrder_.begin(), tabOrder_.end(), tab),
                    tabOrder_.end());
    if (selected_ == tab) selected_ = 0;
    if (tabs_.empty() && !chromeUi_ && Api().tab_create) {
      // The last tab closed: a phone browser is never without a page.
      Api().tab_create(Narrow(Settings::HomePage()).c_str(), 1);
    }
    RefreshChrome();
    if (tabsPanel_.Visibility() == Visibility::Visible) RebuildTabList();
  };
  events.tabSelected = [this](int32_t tab) {
    selected_ = tab;
    RefreshChrome();
  };
  events.fullscreen = [this](bool on) { SetFullscreen(on); };
  events.launchUri = [this](std::wstring uri) {
    Log::Write(L"launch", uri);
    try {
      Launcher::LaunchUriAsync(Uri(hstring(uri)));
    } catch (winrt::hresult_error const&) {
      ShowNotice(L"No app opens " + uri);
    }
  };
  events.downloadDone = [this](std::wstring path, std::wstring mime) {
    // Copied out of the app's staging folder into Downloads\Venetium, or
    // into the file picked in a Save As (client/FileExport.h).
    const std::wstring name = path.substr(path.find_last_of(L'\\') + 1);
    auto dispatcher = CoreWindow::GetForCurrentThread().Dispatcher();
    client::ExportDownload(path, [this, dispatcher, name](std::wstring where,
                                                          std::wstring error) {
      dispatcher.RunAsync(CoreDispatcherPriority::Normal,
                          [this, name, where, error]() {
                            if (!error.empty()) {
                              ShowNotice(L"Downloaded " + name +
                                         L", but it could not be copied to "
                                         L"Downloads (the log says why)");
                              return;
                            }
                            const size_t slash = where.find_last_of(L'\\');
                            std::wstring folder =
                                slash == std::wstring::npos
                                    ? std::wstring()
                                    : where.substr(0, slash);
                            const size_t data = folder.find(L"\\Downloads");
                            if (data != std::wstring::npos) {
                              folder = folder.substr(data + 1);
                            }
                            ShowNotice(L"Downloaded " + name +
                                       (folder.empty() ? L""
                                                       : L" to " + folder));
                          });
    });
  };
  events.dialog = [this](int32_t id, int32_t kind, std::wstring message,
                         std::wstring defaultText) {
    ShowDialog(id, kind, message, defaultText);
  };
  events.fileChooser = [this](int32_t id, std::wstring accept, bool multiple,
                              bool save) {
    ChooseFiles(id, accept, multiple, save);
  };
  events.clipboardSet = [](std::wstring text) {
    DataPackage package;
    package.SetText(hstring(text));
    try {
      Clipboard::SetContent(package);
    } catch (winrt::hresult_error const& e) {
      Log::Write(L"clipboard: set failed", std::wstring(e.message()));
    }
  };
  events.shellCommand = [this](int32_t command) {
    switch (command) {
      case 1:
        ShowAbout();
        break;
      case 2:
        ShowLog();
        break;
      case 3:
        ShowEngineSwitches();
        break;
      case 4:
        std::thread([]() { Log::Mirror(); }).detach();
        ShowNotice(L"Logs go to Pictures\\Venetium logs");
        break;
      case 5:
      case 6:
        // Chrome's menu: JavaScript's JIT on or off, from the next start.
        Settings::SetJit(command == 5);
        ShowNotice(command == 5 ? L"JavaScript JIT on after a restart"
                                : L"JavaScript JIT off after a restart");
        break;
    }
  };
  events.openPath = [this](std::wstring path, int32_t how) {
    Log::Write(how == 0   ? L"open file"
               : how == 1 ? L"open folder"
                          : L"open the folder of",
               path);
    // A download copied out of the staging folder: the copy is the one the
    // user knows, so that is the one opened or shown.
    if (how != 1 && client::OpenDownloadCopy(path, how == 2)) {
      return;
    }
    if (how == 2) {
      const size_t slash = path.find_last_of(L'\\');
      if (slash != std::wstring::npos) {
        path.resize(slash);
      }
    }
    const bool folder = how != 0;
    [](MainPage* page, std::wstring path, bool folder)
        -> winrt::fire_and_forget {
      bool opened = false;
      try {
        if (folder) {
          auto dir = co_await StorageFolder::GetFolderFromPathAsync(path);
          opened = co_await Launcher::LaunchFolderAsync(dir);
        } else {
          auto file = co_await StorageFile::GetFileFromPathAsync(path);
          LauncherOptions options;
          // No app for the type: the phone offers to look for one.
          options.DisplayApplicationPicker(false);
          opened = co_await Launcher::LaunchFileAsync(file, options);
        }
      } catch (winrt::hresult_error const& e) {
        Log::Write(L"open: failed", std::wstring(e.message()));
      }
      if (!opened) {
        page->ShowNotice(L"Nothing opens " +
                         path.substr(path.find_last_of(L'\\') + 1));
      }
    }(this, std::move(path), folder);
  };
  events.engineStopped = [this](int32_t code) { OnEngineStopped(code); };
  EngineHost::Get().SetEvents(std::move(events));
}

void MainPage::Start(std::wstring initialUrl) {
  auto& host = EngineHost::Get();
  auto dispatcher = CoreWindow::GetForCurrentThread().Dispatcher();
  if (!host.Load(dispatcher)) {
    splashText_.Text(
        L"The engine could not be loaded.\nMenu \u2192 Log says why.");
    return;
  }
  if (Api().set_search_url) {
    Api().set_search_url(Narrow(Settings::SearchTemplate()).c_str());
  }
  if (Api().set_desktop_mode) {
    Api().set_desktop_mode(Settings::DesktopMode() ? 1 : 0);
  }
  // What the crash log's heartbeat measures every two seconds -- the address
  // space left, the app's memory against its limit -- goes to the engine,
  // whose memory pressure goes by it: it trims its caches and unloads the
  // pages of background tabs as the numbers fall.
  client::SetMemorySink(Api().memory_status);

  ReportScreen();
  try {
    DisplayInformation::GetForCurrentView().OrientationChanged(
        [this](auto&&, auto&&) { ReportScreen(); });
  } catch (winrt::hresult_error const&) {
  }

  engine::StartParams params;
  params.installDir = std::wstring(Package::Current().InstalledLocation().Path());
  auto data = ApplicationData::Current();
  const std::wstring local(data.LocalFolder().Path());
  params.profileDir = local + L"\\profile";
  params.cacheDir = std::wstring(data.LocalCacheFolder().Path()) + L"\\Cache";
  params.tempDir = std::wstring(data.TemporaryFolder().Path());
  params.scale = rawPerView_;
  params.jit = Settings::Jit();
  params.switches = Narrow(Settings::EngineSwitches());
  client::SetDiagnostics(params.switches.find("--vw-diagnostics") != std::string::npos);
  if (!Settings::HardwareGraphics()) {
    params.switches += " --vw-present=bitmap";
  } else if (Settings::TakeGraphicsRetry()) {
    params.switches += " --vw-gpu=retry";
  }
  if (Api().ui_owner &&
      Api().ui_owner(params.switches.c_str(), params.profileDir.c_str()) == 1) {
    EnterChromeUi();
    // Chrome's interface, and the pages' prefers-color-scheme, in the
    // phone's theme, as Chrome for Android follows Android's.
    try {
      UISettings settings;
      if (settings.GetColorValue(UIColorType::Background).R < 128) {
        params.switches += " --force-dark-mode";
      }
    } catch (winrt::hresult_error const&) {
    }
  }
  // The panel the engine presents into, before it starts.
  if (Api().set_panel) Api().set_panel(winrt::get_abi(engineView_->Panel()));
  // Chrome's interface opens what Chrome's own startup settings say (the
  // new tab page, or the last session): only a URL from outside is passed.
  params.initialUrl = Narrow(initialUrl.empty() && !chromeUi_
                                 ? Settings::HomePage()
                                 : initialUrl);
  ::CreateDirectoryW(params.profileDir.c_str(), nullptr);

  engineView_->WhenRoomKnown([this, params]() {
    if (!EngineHost::Get().Start(params)) {
      splashText_.Text(L"The engine could not be started.\nMenu \u2192 Log.");
      return;
    }
    engineView_->OnFirstFrame([this]() {
      splash_.Visibility(Visibility::Collapsed);
      Log::Write(L"page: first frame on screen");
    });
    // Downloads copied out two days ago leave the staging folder (once the
    // engine has said where it is).
    std::thread([]() {
      ::Sleep(30000);
      client::CleanUpStagedDownloads();
    }).detach();
    engineView_->Start();
    if (!pendingUrl_.empty()) {
      const std::wstring url = std::move(pendingUrl_);
      pendingUrl_.clear();
      OpenExternalUrl(url);
    }
  });

  try {
    MemoryManager::AppMemoryUsageIncreased([](auto&&, auto&&) {
      if (!Api().memory_pressure) return;
      const auto level = MemoryManager::AppMemoryUsageLevel();
      if (level == AppMemoryUsageLevel::OverLimit) {
        Api().memory_pressure(2);
      } else if (level == AppMemoryUsageLevel::High) {
        Api().memory_pressure(1);
      }
    });
    MemoryManager::AppMemoryUsageLimitChanging(
        [](auto&&, AppMemoryUsageLimitChangingEventArgs const& args) {
          if (Api().memory_pressure &&
              MemoryManager::AppMemoryUsage() >= args.NewLimit()) {
            Api().memory_pressure(2);
          }
        });
  } catch (winrt::hresult_error const& e) {
    Log::Write(L"memory: no usage events", std::wstring(e.message()));
  }

  // The phone's clipboard, for a paste in a page: an app may read it only
  // while in front, so when it comes there and when it changes meanwhile.
  Clipboard::ContentChanged([this](auto&&, auto&&) { SendPhoneClipboard(); });
  CoreWindow::GetForCurrentThread().Activated(
      [this](auto&&, WindowActivatedEventArgs const& args) {
        if (args.WindowActivationState() !=
            CoreWindowActivationState::Deactivated) {
          SendPhoneClipboard();
        }
      });
  SendPhoneClipboard();
}

void MainPage::OpenExternalUrl(std::wstring const& url) {
  Log::Write(L"open", url);
  if (!EngineHost::Get().started()) {
    pendingUrl_ = url;
    return;
  }
  if (Api().open_url && Api().open_url(Narrow(url).c_str()) == 1) return;
  // Still starting: as a new tab once it can take one.
  if (Api().tab_create) Api().tab_create(Narrow(url).c_str(), 1);
}

void MainPage::OnSuspending() {
  if (Api().app_state) Api().app_state(1);
}

void MainPage::OnResuming() {
  if (Api().app_state) Api().app_state(0);
}

MainPage::TabInfo* MainPage::Selected() {
  auto it = tabs_.find(selected_);
  return it == tabs_.end() ? nullptr : &it->second;
}

void MainPage::Navigate(std::wstring const& text) {
  if (text.empty() || !Api().tab_navigate) return;
  Api().tab_navigate(selected_, Narrow(text).c_str());
  // Off the address bar, so the keyboard goes and the page shows.
  reloadButton_.Focus(FocusState::Programmatic);
  engineView_->HideKeyboard();
}

void MainPage::RefreshChrome() {
  tabsCount_.Text(std::to_wstring(tabs_.size() ? tabs_.size() : 1));
  TabInfo* tab = Selected();
  if (!editingAddress_) {
    address_.Text(tab ? hstring(DisplayUrl(tab->url)) : hstring());
    const bool lock = tab && tab->secure;
    lockIcon_.Visibility(lock ? Visibility::Visible : Visibility::Collapsed);
    address_.Padding(ThicknessHelper::FromLengths(lock ? 28 : 10, 6, 10, 6));
  }
  const bool loading = tab && tab->loading;
  reloadIcon_.Glyph(loading ? L"\uE711" : L"\uE72C");  // Cancel : Refresh
  progress_.Opacity(loading ? 1.0 : 0.0);
  progress_.Value(tab && tab->progress > 0 ? tab->progress : 0);
  if (forwardItem_) forwardItem_.IsEnabled(tab && tab->canForward);
}

void MainPage::ShowTabs(bool show) {
  if (show) RebuildTabList();
  tabsPanel_.Visibility(show ? Visibility::Visible : Visibility::Collapsed);
}

void MainPage::RebuildTabList() {
  tabsList_.Children().Clear();
  for (int32_t id : tabOrder_) {
    auto it = tabs_.find(id);
    if (it == tabs_.end()) continue;
    const TabInfo& info = it->second;
    Grid row;
    row.Margin(ThicknessHelper::FromLengths(0, 4, 0, 4));
    row.Padding(ThicknessHelper::FromLengths(12, 10, 4, 10));
    row.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
    row.Background(Brush(id == selected_ ? ColorHelper::FromArgb(255, 0x3a, 0x34, 0x5c)
                                         : FieldBg()));
    ColumnDefinition text;
    text.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    ColumnDefinition close;
    close.Width(GridLengthHelper::Auto());
    row.ColumnDefinitions().Append(text);
    row.ColumnDefinitions().Append(close);
    StackPanel lines;
    TextBlock title;
    title.Text(hstring(info.title.empty() ? (info.url.empty() ? L"New tab"
                                                              : info.url)
                                          : info.title));
    title.Foreground(Brush(ChromeFg()));
    title.FontSize(15);
    title.TextTrimming(TextTrimming::CharacterEllipsis);
    // Its page was unloaded to give its memory back (progress -1); it
    // loads again when the tab is chosen.
    if (info.progress < 0) title.Opacity(0.5);
    lines.Children().Append(title);
    TextBlock url;
    url.Text(hstring(DisplayUrl(info.url)));
    url.Foreground(Brush(Dim()));
    url.FontSize(12);
    url.TextTrimming(TextTrimming::CharacterEllipsis);
    lines.Children().Append(url);
    row.Children().Append(lines);
    Button x = IconButton(L"\uE711");  // Cancel
    x.Click([this, id](auto&&, auto&&) {
      if (Api().tab_close) Api().tab_close(id);
    });
    Grid::SetColumn(x, 1);
    row.Children().Append(x);
    row.Tapped([this, id](auto&&, auto&&) {
      if (Api().tab_select) Api().tab_select(id);
      selected_ = id;
      ShowTabs(false);
      RefreshChrome();
    });
    tabsList_.Children().Append(row);
  }
}

void MainPage::ApplyVisibleBounds() {
  auto view = ApplicationView::GetForCurrentView();
  const auto visible = view.VisibleBounds();
  const auto window = Window::Current().Bounds();
  // In fullscreen nothing is padded: the navigation bar is an overlay there
  // that comes and goes, and padding for it would resize the page each time.
  bool fullscreen = fullscreen_;
  try {
    fullscreen = fullscreen || view.IsFullScreenMode();
  } catch (winrt::hresult_error const&) {
  }
  if (fullscreen) {
    root_.Padding(ThicknessHelper::FromUniformLength(0));
    return;
  }
  const double left = visible.X - window.X;
  const double top = visible.Y - window.Y;
  const double right = (window.X + window.Width) - (visible.X + visible.Width);
  const double bottom =
      (window.Y + window.Height) - (visible.Y + visible.Height);
  // Rectangles that do not compare give nonsense; no padding beats wrong.
  auto sane = [](double v) { return (v > 0 && v < 400) ? v : 0.0; };
  root_.Padding(ThicknessHelper::FromLengths(sane(left), sane(top),
                                             sane(right), sane(bottom)));
  Log::Write(L"view: visible bounds padding " +
             std::to_wstring(static_cast<int>(sane(top))) + L" top, " +
             std::to_wstring(static_cast<int>(sane(bottom))) + L" bottom");
}

void MainPage::EnterChromeUi() {
  chromeUi_ = true;
  chrome_.Visibility(Visibility::Collapsed);
  progress_.Visibility(Visibility::Collapsed);
  tabsPanel_.Visibility(Visibility::Collapsed);
  Log::Write(L"interface: Chrome's own, drawn by the engine");
}

void MainPage::SetFullscreen(bool on) {
  fullscreen_ = on;
  const bool bar = !on && !chromeUi_;
  chrome_.Visibility(bar ? Visibility::Visible : Visibility::Collapsed);
  progress_.Visibility(bar ? Visibility::Visible : Visibility::Collapsed);
  progress_.Opacity(0);
  auto view = ApplicationView::GetForCurrentView();
  try {
    if (on) {
      view.TryEnterFullScreenMode();
    } else {
      view.ExitFullScreenMode();
    }
  } catch (winrt::hresult_error const&) {
  }
  if (ApiInformation::IsTypePresent(L"Windows.UI.ViewManagement.StatusBar")) {
    auto status = StatusBar::GetForCurrentView();
    if (on) {
      status.HideAsync();
    } else {
      status.ShowAsync();
    }
  }
  ApplyVisibleBounds();
  if (!on) RefreshChrome();
}

void MainPage::ShowDialog(int32_t id, int32_t kind, std::wstring const& message,
                          std::wstring const& defaultText) {
  [](MainPage* page, int32_t id, int32_t kind, std::wstring message,
     std::wstring defaultText) -> winrt::fire_and_forget {
    ContentDialog dialog;
    TextBox input{nullptr};
    if (kind == 3) {
      dialog.Title(box_value(L"Leave this page?"));
      dialog.Content(box_value(L"Changes you made may not be saved."));
      dialog.PrimaryButtonText(L"Leave");
      dialog.SecondaryButtonText(L"Stay");
    } else {
      dialog.Title(box_value(L"This page says"));
      TextBlock text;
      text.Text(hstring(message));
      text.TextWrapping(TextWrapping::Wrap);
      if (kind == 2) {
        StackPanel stack;
        stack.Children().Append(text);
        input = TextBox();
        input.Text(hstring(defaultText));
        input.Margin(ThicknessHelper::FromLengths(0, 12, 0, 0));
        stack.Children().Append(input);
        dialog.Content(stack);
      } else {
        dialog.Content(text);
      }
      dialog.PrimaryButtonText(L"OK");
      if (kind != 0) dialog.SecondaryButtonText(L"Cancel");
    }
    bool accepted = true;
    try {
      auto result = co_await dialog.ShowAsync();
      accepted = result == ContentDialogResult::Primary || kind == 0;
    } catch (winrt::hresult_error const&) {
      // Another dialog is open; the page is answered as if cancelled.
      accepted = kind == 0;
    }
    const std::string text =
        input ? Narrow(std::wstring(input.Text())) : std::string();
    if (Api().dialog_reply) Api().dialog_reply(id, accepted ? 1 : 0, text.c_str());
  }(this, id, kind, message, defaultText);
}

void MainPage::ChooseFiles(int32_t id, std::wstring const& accept,
                           bool multiple, bool save) {
  [](int32_t id, std::wstring offered, bool multiple,
     bool save) -> winrt::fire_and_forget {
    std::string paths;
    try {
      if (save) {
        // Chrome's Save As; |offered| is the file name it suggests.
        FileSavePicker picker;
        picker.SuggestedStartLocation(PickerLocationId::Downloads);
        std::wstring extension;
        const size_t dot = offered.rfind(L'.');
        if (dot != std::wstring::npos && dot + 1 < offered.size()) {
          extension = offered.substr(dot);
        }
        // The type list may not be empty; the picker adds the extension.
        const std::wstring type = extension.empty() ? L".bin" : extension;
        picker.FileTypeChoices().Insert(
            hstring(extension.empty() ? L"File" : extension.substr(1)),
            single_threaded_vector<hstring>({hstring(type)}));
        picker.DefaultFileExtension(hstring(type));
        std::wstring stem = offered;
        if (!extension.empty()) {
          stem.resize(dot);
        }
        if (!stem.empty()) {
          picker.SuggestedFileName(hstring(stem));
        }
        auto picked = co_await picker.PickSaveFileAsync();
        if (picked) {
          paths = Narrow(client::PathToSaveInto(picked));
        }
      } else {
        FileOpenPicker picker;
        picker.ViewMode(PickerViewMode::List);
        picker.FileTypeFilter().Append(L"*");
        std::vector<StorageFile> files;
        if (multiple) {
          auto picked = co_await picker.PickMultipleFilesAsync();
          for (auto const& f : picked) files.push_back(f);
        } else {
          auto f = co_await picker.PickSingleFileAsync();
          if (f) files.push_back(f);
        }
        // The engine opens files with Win32 calls, which an app reaches only
        // in its own folders: each pick is copied to TempState first.
        auto temp = ApplicationData::Current().TemporaryFolder();
        for (auto const& f : files) {
          auto copy = co_await f.CopyAsync(temp, f.Name(),
                                           NameCollisionOption::GenerateUniqueName);
          if (!paths.empty()) paths += "\n";
          paths += Narrow(std::wstring(copy.Path()));
        }
      }
    } catch (winrt::hresult_error const& e) {
      Log::Write(L"files: picker failed", std::wstring(e.message()));
    }
    if (Api().file_reply) Api().file_reply(id, paths.c_str());
  }(id, accept, multiple, save);
}

void MainPage::ShowNotice(std::wstring const& text) {
  noticeText_.Text(hstring(text));
  notice_.Visibility(Visibility::Visible);
  noticeTimer_.Stop();
  noticeTimer_.Start();
}

// The whole display in raw pixels, the way it is turned now (the panel's
// own size is the portrait one), and its density.
void MainPage::ReportScreen() {
  try {
    auto info = DisplayInformation::GetForCurrentView();
    int32_t sw = 0, sh = 0;
    if (ApiInformation::IsPropertyPresent(
            L"Windows.Graphics.Display.DisplayInformation",
            L"ScreenWidthInRawPixels")) {
      sw = static_cast<int32_t>(info.ScreenWidthInRawPixels());
      sh = static_cast<int32_t>(info.ScreenHeightInRawPixels());
      const auto orientation = info.CurrentOrientation();
      const bool landscape =
          orientation == DisplayOrientations::Landscape ||
          orientation == DisplayOrientations::LandscapeFlipped;
      if (landscape != (sw > sh)) std::swap(sw, sh);
    } else {
      auto bounds = CoreWindow::GetForCurrentThread().Bounds();
      sw = static_cast<int32_t>(bounds.Width * rawPerView_);
      sh = static_cast<int32_t>(bounds.Height * rawPerView_);
    }
    if (Api().set_screen) Api().set_screen(sw, sh);
    if (Api().set_dpi) Api().set_dpi(static_cast<int32_t>(info.RawDpiX()));
  } catch (winrt::hresult_error const&) {
  }
}

void MainPage::OnEngineStopped(int32_t code) {
  engineView_->Stop();
  if (chromeUi_) {
    // Chrome's menu went with the engine: the shell's bar comes back for its
    // own, which has the log and the engine switches ("--vw-ui=shell").
    chromeUi_ = false;
    chrome_.Visibility(Visibility::Visible);
  }
  splashRing_.IsActive(false);
  splashText_.Text(L"The engine stopped (code " + std::to_wstring(code) +
                   L").\nMenu \u2192 Log says why.");
  splash_.Visibility(Visibility::Visible);
}

void MainPage::SendPhoneClipboard() {
  if (!Api().clipboard_phone_text) return;
  []() -> winrt::fire_and_forget {
    try {
      DataPackageView content = Clipboard::GetContent();
      if (!content.Contains(StandardDataFormats::Text())) co_return;
      hstring text = co_await content.GetTextAsync();
      Api().clipboard_phone_text(Narrow(std::wstring(text)).c_str());
    } catch (winrt::hresult_error const&) {
      // Not in front after all, or the clipboard is busy: next time.
    }
  }();
}

// About the port and its authors, as gecko-w10m's Settings has it.
// Chromium's and the engine's own command-line switches, for trying one
// thing against another on a phone without a new build. Taken at the next
// start.
void MainPage::ShowEngineSwitches() {
  [](MainPage*) -> winrt::fire_and_forget {
    ContentDialog dialog;
    dialog.Title(box_value(L"Engine switches"));
    StackPanel panel;
    TextBlock note;
    note.Text(L"Extra command-line switches for the engine, separated by "
              L"spaces; used from the next start. For example:\n"
              L"--vw-gpu=off  (no GPU compositing)\n"
              L"--vw-gpu=d3d11  (the Direct3D 11 compositor, device held to "
              L"9_3)\n"
              L"--vw-vsync=timer  (frames from a timer)\n"
              L"--vw-present-buffers=2\n"
              L"--vw-codecs=all  (VP9 and AV1 video offered again)\n"
              L"--vw-video=software  (H.264 decoded on the CPU)\n"
              L"--vw-discard=off  (background tabs stay loaded when memory "
              L"is short)\n"
              L"--vw-trim=off  (the graphics device is not trimmed when the "
              L"app is suspended)");
    note.FontSize(12);
    note.Opacity(0.7);
    note.TextWrapping(TextWrapping::Wrap);
    note.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
    panel.Children().Append(note);
    TextBox box;
    box.Text(hstring(Settings::EngineSwitches()));
    box.AcceptsReturn(false);
    box.IsSpellCheckEnabled(false);
    box.TextWrapping(TextWrapping::Wrap);
    panel.Children().Append(box);
    dialog.Content(panel);
    dialog.PrimaryButtonText(L"Save");
    dialog.SecondaryButtonText(L"Cancel");
    try {
      if (co_await dialog.ShowAsync() == ContentDialogResult::Primary) {
        Settings::SetEngineSwitches(std::wstring(box.Text()));
        Log::Write(L"settings: engine switches", std::wstring(box.Text()));
      }
    } catch (winrt::hresult_error const&) {
    }
  }(this);
}

void MainPage::ShowAbout() {
  [](MainPage* self) -> winrt::fire_and_forget {
    ContentDialog dialog;
    dialog.Title(box_value(L"Venetium for Windows 10 Mobile"));

    StackPanel panel;
    auto line = [&panel](std::wstring const& text, double size, bool dim) {
      TextBlock block;
      block.Text(hstring(text));
      block.FontSize(size);
      block.TextWrapping(TextWrapping::Wrap);
      block.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
      if (dim) block.Opacity(0.7);
      panel.Children().Append(block);
    };
    // A link opens in a new tab of this browser, or in the phone's settings.
    auto link = [&panel, &dialog, self](std::wstring const& text,
                                         std::wstring const& uri) {
      HyperlinkButton button;
      button.Content(box_value(hstring(text)));
      button.Padding(ThicknessHelper::FromLengths(0, 4, 0, 4));
      // Weakly: the dialog owns the button, the button this handler.
      button.Click([weak = winrt::make_weak(dialog), self, uri](auto&&,
                                                                 auto&&) {
        if (auto shown = weak.get()) shown.Hide();
        if (uri.rfind(L"ms-settings:", 0) == 0) {
          try {
            Launcher::LaunchUriAsync(Uri(hstring(uri)));
          } catch (winrt::hresult_error const&) {
          }
        } else {
          self->OpenExternalUrl(uri);
        }
      });
      panel.Children().Append(button);
    };

    std::wstring version = L"?";
    std::wstring engine = L"Chromium";
    try {
      const auto v = Package::Current().Id().Version();
      version = std::to_wstring(v.Major) + L"." + std::to_wstring(v.Minor) +
                L"." + std::to_wstring(v.Build) + L"." +
                std::to_wstring(v.Revision);
      engine = L"Chromium " + std::to_wstring(v.Major) + L".0." +
               std::to_wstring(v.Minor) + L"." + std::to_wstring(v.Build);
    } catch (winrt::hresult_error const&) {
    }
    line(L"Venetium is Chromium \u2014 the engine of Chrome \u2014 running on "
         L"Windows 10 Mobile.",
         15, false);
    line(L"Port version: " + version, 15, false);
    line(L"The first three numbers are the engine's (" + engine +
             L"), the last is the build number.",
         12, true);
    link(L"Make this the default browser", L"ms-settings:defaultapps");

    line(L"Ported to Windows 10 Mobile by Computershik", 15, false);
    line(L"Porting, building and debugging on the device.", 12, true);
    link(L"Profile on 4pda",
         L"https://4pda.to/forum/index.php?showuser=4458524");
    link(L"Telegram channel @cmplog", L"https://t.me/cmplog");
    link(L"Donate (within the CIS for now)",
         L"https://pay.cloudtips.ru/p/83821e32");
    line(L"Outside the CIS and would like to help? Send a message to the "
         L"Telegram channel.",
         12, true);

    line(L"Based on Venetium by hamed7ir", 15, false);
    line(L"Venetium is Chromium 150 for Windows 10 on 32-bit ARM (Surface RT, "
         L"Surface 2): the whole ARM32 port of the engine is hamed7ir's "
         L"work, and this Windows 10 Mobile port is built on it, at "
         L"hamed7ir's request.",
         12, true);
    link(L"Venetium on GitHub", L"https://github.com/hamed7ir/venetium");
    line(L"Venetium is built on Supermium by win32ss; Chromium is the work "
         L"of The Chromium Authors. The Windows 10 Mobile approach comes "
         L"from gecko-w10m.",
         12, true);

    ScrollViewer scroller;
    scroller.MaxHeight(440);
    scroller.Content(panel);
    dialog.Content(scroller);
    dialog.PrimaryButtonText(L"Close");
    try {
      co_await dialog.ShowAsync();
    } catch (winrt::hresult_error const&) {
    }
  }(this);
}

void MainPage::ShowLog() {
  [](MainPage*) -> winrt::fire_and_forget {
    std::wstring all;
    for (auto const& line : Log::Recent()) {
      all += line;
      all += L"\n";
    }
    ContentDialog dialog;
    dialog.Title(box_value(L"Log"));
    ScrollViewer scroller;
    scroller.MaxHeight(420);
    TextBlock text;
    text.Text(hstring(all));
    text.FontFamily(FontFamily(L"Consolas"));
    text.FontSize(10);
    text.TextWrapping(TextWrapping::Wrap);
    text.IsTextSelectionEnabled(true);
    scroller.Content(text);
    dialog.Content(scroller);
    dialog.PrimaryButtonText(L"Close");
    try {
      co_await dialog.ShowAsync();
    } catch (winrt::hresult_error const&) {
    }
  }(this);
}

}  // namespace venetium_w10m
