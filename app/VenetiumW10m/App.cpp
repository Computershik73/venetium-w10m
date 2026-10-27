// App.cpp -- the UWP application: a code-only C++/WinRT XAML app.
#include "pch.h"

#include <memory>
#include <thread>

#include "MainPage.h"
#include "client/CrashLog.h"
#include "client/Log.h"

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Xaml;

namespace venetium_w10m {

// A code-only XAML app still implements IXamlMetadataProvider -- the
// framework asks the Application for it -- with no custom types to describe.
struct App : ApplicationT<App, winrt::Windows::UI::Xaml::Markup::IXamlMetadataProvider> {
  std::shared_ptr<MainPage> page_;
  bool started_ = false;

  App() {
    // The log first, before anything that can throw: an app that closes at
    // once otherwise leaves nothing to read (gecko-w10m on 1511).
    try {
      client::Log::Init(std::wstring(
          winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()));
      auto version = Package::Current().Id().Version();
      client::Log::Write(L"boot: Venetium " + std::to_wstring(version.Major) +
                         L"." + std::to_wstring(version.Minor) + L"." +
                         std::to_wstring(version.Build) + L"." +
                         std::to_wstring(version.Revision));
      client::InstallCrashLog();
    } catch (...) {
    }
    try {
      UnhandledException([](auto const&, UnhandledExceptionEventArgs const& e) {
        client::Log::Write(L"FATAL: XAML unhandled exception",
                           std::wstring(e.Message()));
      });
    } catch (...) {
    }
    try {
      Suspending([this](auto const&, SuspendingEventArgs const& args) {
        client::Log::Write(L"app: suspending");
        // A phone usually ends a suspended app without another word, so the
        // engine writes out cookies and storage now, and the logs are copied
        // where a person can reach them.
        if (page_) page_->OnSuspending();
        auto deferral = args.SuspendingOperation().GetDeferral();
        std::thread([deferral]() {
          ::Sleep(1500);
          client::Log::Mirror();
          deferral.Complete();
        }).detach();
      });
      Resuming([this](auto const&, auto const&) {
        client::Log::Write(L"app: resuming");
        if (page_) page_->OnResuming();
      });
    } catch (winrt::hresult_error const& e) {
      client::Log::Write(L"boot: lifecycle events unavailable",
                         std::wstring(e.message()));
    }
  }

  void OnLaunched(LaunchActivatedEventArgs const& args) {
    std::wstring url;
    try {
      url = std::wstring(args.Arguments());
    } catch (...) {
    }
    Show(url);
  }

  void OnActivated(IActivatedEventArgs const& args) {
    std::wstring url;
    try {
      if (args.Kind() == ActivationKind::Protocol) {
        if (auto uri = args.as<ProtocolActivatedEventArgs>().Uri()) {
          url = uri.AbsoluteUri();
        }
      }
    } catch (winrt::hresult_error const& e) {
      client::Log::Write(L"activation: unreadable", std::wstring(e.message()));
    }
    Show(url);
  }

  // A file handed over by another app is reachable only through the
  // brokered StorageFile; the engine opens files with Win32 calls, which see
  // the app's own folders alone. So it is copied into TempState and the
  // copy opened, as a file the page picks is.
  void OnFileActivated(FileActivatedEventArgs const& args) { OpenFile(args); }

  fire_and_forget OpenFile(FileActivatedEventArgs args) {
    // The window first: activation has to happen promptly, and the copy can
    // take a while.
    ShowWindow();
    std::wstring url;
    try {
      auto files = args.Files();
      if (files && files.Size() > 0) {
        if (auto file =
                files.GetAt(0).try_as<winrt::Windows::Storage::StorageFile>()) {
          auto copy = co_await file.CopyAsync(
              winrt::Windows::Storage::ApplicationData::Current()
                  .TemporaryFolder(),
              file.Name(),
              winrt::Windows::Storage::NameCollisionOption::GenerateUniqueName);
          url = FileUrl(std::wstring(copy.Path()));
        }
      }
    } catch (winrt::hresult_error const& e) {
      client::Log::Write(L"activation: file unreadable",
                         std::wstring(e.message()));
    }
    // Past the await an exception would end the app with no word: say it.
    try {
      Show(url);
    } catch (winrt::hresult_error const& e) {
      client::Log::Write(L"FATAL: file activation", std::wstring(e.message()));
      throw;
    }
  }

  // file:///C:/... with what a URL cannot carry as is percent-encoded.
  static std::wstring FileUrl(std::wstring const& path) {
    std::wstring url = L"file:///";
    for (wchar_t ch : path) {
      switch (ch) {
        case L'\\': url += L'/'; break;
        case L'%': url += L"%25"; break;
        case L'#': url += L"%23"; break;
        case L'?': url += L"%3F"; break;
        case L' ': url += L"%20"; break;
        default: url += ch; break;
      }
    }
    return url;
  }

  void ShowWindow() {
    if (!page_) {
      page_ = std::make_shared<MainPage>();
      Window::Current().Content(page_->Root());
    }
    Window::Current().Activate();
  }

  // The first activation starts the engine with |url| as its first page;
  // later ones open |url| in a new tab.
  void Show(std::wstring const& url) {
    ShowWindow();
    // An installed app's tile: the browser starts (or is there), then the
    // engine opens the app's window.
    const size_t prefix = wcslen(kAppTileArguments);
    if (url.compare(0, prefix, kAppTileArguments) == 0) {
      if (!started_) {
        started_ = true;
        page_->Start(L"");
      }
      page_->LaunchApp(url.substr(prefix));
      return;
    }
    if (!started_) {
      started_ = true;
      page_->Start(url);
    } else if (!url.empty()) {
      page_->OpenExternalUrl(url);
    }
  }

  winrt::Windows::UI::Xaml::Markup::IXamlType GetXamlType(
      winrt::Windows::UI::Xaml::Interop::TypeName const&) {
    return nullptr;
  }
  winrt::Windows::UI::Xaml::Markup::IXamlType GetXamlType(hstring const&) {
    return nullptr;
  }
  com_array<winrt::Windows::UI::Xaml::Markup::XmlnsDefinition>
  GetXmlnsDefinitions() {
    return {};
  }
};

}  // namespace venetium_w10m

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  Application::Start([](auto&&) { winrt::make<venetium_w10m::App>(); });
  return 0;
}
