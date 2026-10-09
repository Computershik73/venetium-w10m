// MainPage.cpp
#include "pch.h"

#include "MainPage.h"

#include <algorithm>
#include <thread>

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
  progress_.Visibility(Visibility::Collapsed);
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
    ProgressRing ring;
    ring.IsActive(true);
    ring.Width(32);
    ring.Height(32);
    ring.Foreground(Brush(Accent()));
    ring.Margin(ThicknessHelper::FromLengths(0, 24, 0, 0));
    stack.Children().Append(ring);
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
  // the browser stays inside what is visible.
  auto view = ApplicationView::GetForCurrentView();
  view.SetDesiredBoundsMode(ApplicationViewBoundsMode::UseVisible);
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
    if (Api().tab_reload) Api().tab_reload(selected_);
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
    Clipboard::SetContent(package);
    ShowNotice(L"Link copied");
  });
  item(L"Log", [this]() { ShowLog(); });
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
    if (tabs_.empty() && Api().tab_create) {
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
    const std::wstring name = path.substr(path.find_last_of(L'\\') + 1);
    ShowNotice(L"Downloaded " + name);
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
    Clipboard::SetContent(package);
  };
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

  // The whole display in raw pixels, and its density.
  try {
    auto info = DisplayInformation::GetForCurrentView();
    int32_t sw = 0, sh = 0;
    if (ApiInformation::IsPropertyPresent(
            L"Windows.Graphics.Display.DisplayInformation",
            L"ScreenWidthInRawPixels")) {
      sw = static_cast<int32_t>(info.ScreenWidthInRawPixels());
      sh = static_cast<int32_t>(info.ScreenHeightInRawPixels());
    } else {
      auto bounds = CoreWindow::GetForCurrentThread().Bounds();
      sw = static_cast<int32_t>(bounds.Width * rawPerView_);
      sh = static_cast<int32_t>(bounds.Height * rawPerView_);
    }
    if (Api().set_screen) Api().set_screen(sw, sh);
    if (Api().set_dpi) Api().set_dpi(static_cast<int32_t>(info.RawDpiX()));
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
  params.initialUrl =
      Narrow(initialUrl.empty() ? Settings::HomePage() : initialUrl);
  ::CreateDirectoryW(params.profileDir.c_str(), nullptr);

  if (!host.Start(params)) {
    splashText_.Text(L"The engine could not be started.\nMenu \u2192 Log.");
    return;
  }
  engineView_->OnFirstFrame([this]() {
    splash_.Visibility(Visibility::Collapsed);
    Log::Write(L"page: first frame on screen");
  });
  engineView_->Start();
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
  progress_.Visibility(loading ? Visibility::Visible : Visibility::Collapsed);
  progress_.Value(tab ? tab->progress : 0);
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

void MainPage::SetFullscreen(bool on) {
  fullscreen_ = on;
  chrome_.Visibility(on ? Visibility::Collapsed : Visibility::Visible);
  progress_.Visibility(Visibility::Collapsed);
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
  [](int32_t id, bool multiple, bool save) -> winrt::fire_and_forget {
    std::string paths;
    try {
      if (!save) {
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
  }(id, multiple, save);
}

void MainPage::ShowNotice(std::wstring const& text) {
  noticeText_.Text(hstring(text));
  notice_.Visibility(Visibility::Visible);
  noticeTimer_.Stop();
  noticeTimer_.Start();
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
