// FileExport.cpp -- see FileExport.h.
#include "pch.h"

#include "client/FileExport.h"

#include <algorithm>
#include <cwchar>
#include <thread>
#include <utility>
#include <vector>

#include <winrt/Windows.Storage.AccessCache.h>

#include "client/Log.h"

using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Foundation::IAsyncOperation;
using winrt::Windows::Storage::CreationCollisionOption;
using winrt::Windows::Storage::DownloadsFolder;
using winrt::Windows::Storage::IStorageItem;
using winrt::Windows::Storage::StorageFile;
using winrt::Windows::Storage::StorageFolder;
using winrt::Windows::Storage::AccessCache::StorageApplicationPermissions;
using winrt::Windows::System::Launcher;

namespace venetium_w10m::client {

namespace {

// A staged download is moved out and a placeholder of no bytes left under
// its name, so Chrome, which checks that its downloads' files exist, still
// lists it as one it can open (and opening it goes to the moved file). The
// placeholders go after this long; a download that could only be copied out
// (the move refused) is cleared sooner.
constexpr unsigned long long kKeepPlaceholderFor = 30ull * 24 * 60 * 60;
constexpr unsigned long long kKeepStagedFor = 2ull * 24 * 60 * 60;  // seconds
// The future access list holds a thousand; old copies' entries go first.
constexpr uint32_t kAccessListRoom = 800;

std::wstring Lower(std::wstring text) {
  std::transform(text.begin(), text.end(), text.begin(), ::towlower);
  return text;
}

bool UnderStaging(std::wstring const& path) {
  const std::wstring staging = DownloadStaging();
  return !staging.empty() && path.size() > staging.size() + 1 &&
         Lower(path.substr(0, staging.size() + 1)) == Lower(staging + L"\\");
}

// Future access list tokens, named after the path they are for: "x" a
// download's copy, by the staged path; "s" the file picked in a Save As, by
// the staged path handed to the engine instead.
std::wstring Token(wchar_t kind, std::wstring const& path) {
  unsigned long long hash = 14695981039346656037ull;
  for (wchar_t c : Lower(path)) {
    hash ^= static_cast<unsigned long long>(c);
    hash *= 1099511628211ull;
  }
  wchar_t text[24];
  swprintf_s(text, L"%c%016llx", kind, hash);
  return text;
}

void Remember(std::wstring const& token, IStorageItem const& item) {
  auto list = StorageApplicationPermissions::FutureAccessList();
  try {
    if (list.Entries().Size() >= kAccessListRoom) {
      // Oldest first, by the time kept as each entry's metadata.
      std::vector<std::pair<unsigned long long, winrt::hstring>> copies;
      for (auto const& entry : list.Entries()) {
        if (!entry.Token.empty() && entry.Token[0] == L'x') {
          copies.emplace_back(wcstoull(entry.Metadata.c_str(), nullptr, 10),
                              entry.Token);
        }
      }
      std::sort(copies.begin(), copies.end());
      for (size_t i = 0; i < copies.size() && i < 100; ++i) {
        list.Remove(copies[i].second);
      }
    }
    list.AddOrReplace(
        winrt::hstring(token), item,
        winrt::hstring(std::to_wstring(
            static_cast<unsigned long long>(_time64(nullptr)))));
  } catch (winrt::hresult_error const& error) {
    Log::Write(L"files: could not remember " + token,
               std::wstring(error.message()));
  }
}

bool Remembered(std::wstring const& token) {
  try {
    return StorageApplicationPermissions::FutureAccessList().ContainsItem(
        winrt::hstring(token));
  } catch (...) {
    return false;
  }
}

void Forget(std::wstring const& token) {
  try {
    StorageApplicationPermissions::FutureAccessList().Remove(
        winrt::hstring(token));
  } catch (...) {
  }
}

// Whether the engine (Win32, by path) may write |path|: CreateFile2 is the
// app container's CreateFile.
bool Win32CanWrite(std::wstring const& path) {
  HANDLE h = ::CreateFile2(path.c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           OPEN_ALWAYS, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  ::CloseHandle(h);
  return true;
}

// The launcher wants the UI thread; the storage calls before it complete on
// a pool thread.
void LaunchFile(StorageFile const& file, std::wstring const& what) {
  winrt::Windows::ApplicationModel::Core::CoreApplication::MainView()
      .Dispatcher()
      .RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                [file, what]() {
                  try {
                    Launcher::LaunchFileAsync(file);
                    Log::Write(L"open: asked the system to open " + what);
                  } catch (winrt::hresult_error const& error) {
                    Log::Write(L"open: the system refused " + what,
                               std::wstring(error.message()));
                  }
                });
}

void LaunchFolder(StorageFolder const& folder, std::wstring const& what) {
  winrt::Windows::ApplicationModel::Core::CoreApplication::MainView()
      .Dispatcher()
      .RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                [folder, what]() {
                  try {
                    Launcher::LaunchFolderAsync(folder);
                    Log::Write(L"open: showing the folder " + what);
                  } catch (winrt::hresult_error const& error) {
                    Log::Write(L"open: the system would not show " + what,
                               std::wstring(error.message()));
                  }
                });
}

unsigned long long AgeSeconds(FILETIME const& written) {
  FILETIME now_ft;
  ::GetSystemTimeAsFileTime(&now_ft);
  ULARGE_INTEGER now, then;
  now.LowPart = now_ft.dwLowDateTime;
  now.HighPart = now_ft.dwHighDateTime;
  then.LowPart = written.dwLowDateTime;
  then.HighPart = written.dwHighDateTime;
  return now.QuadPart > then.QuadPart
             ? (now.QuadPart - then.QuadPart) / 10000000ull
             : 0;
}

void CleanUp(std::wstring const& dir, bool nested) {
  WIN32_FIND_DATAW found;
  HANDLE find = ::FindFirstFileW((dir + L"\\*").c_str(), &found);
  if (find == INVALID_HANDLE_VALUE) {
    return;
  }
  do {
    const std::wstring name(found.cFileName);
    if (name == L"." || name == L"..") {
      continue;
    }
    const std::wstring path = dir + L"\\" + name;
    if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (!nested && name.rfind(L"saved-", 0) == 0) {
        CleanUp(path, true);
        ::RemoveDirectoryW(path.c_str());  // only once it is empty
      }
      continue;
    }
    const bool placeholder =
        found.nFileSizeHigh == 0 && found.nFileSizeLow == 0;
    if (AgeSeconds(found.ftLastWriteTime) <
        (placeholder ? kKeepPlaceholderFor : kKeepStagedFor)) {
      continue;
    }
    if (!Remembered(Token(L'x', path))) {
      continue;  // never copied out: the only one there is
    }
    if (::DeleteFileW(path.c_str())) {
      Log::Write(L"files: cleared " + path);
    }
  } while (::FindNextFileW(find, &found));
  ::FindClose(find);
}

}  // namespace

std::wstring DownloadStaging() {
  wchar_t buffer[MAX_PATH] = {};
  const DWORD n = ::GetEnvironmentVariableW(L"VENETIUM_DOWNLOAD_STAGING",
                                            buffer, MAX_PATH);
  return n && n < MAX_PATH ? std::wstring(buffer, n) : std::wstring();
}

void ExportDownload(std::wstring path,
                    std::function<void(std::wstring where,
                                       std::wstring error)> done) {
  std::thread([path = std::move(path), done = std::move(done)]() {
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    if (!UnderStaging(path)) {
      // Downloaded where the user finds it already.
      done(path, L"");
      return;
    }
    std::wstring step = L"opening the staged file";
    try {
      StorageFile staged =
          StorageFile::GetFileFromPathAsync(winrt::hstring(path)).get();
      StorageFile target{nullptr};
      const std::wstring save_as = Token(L's', path);
      const bool saved_as = Remembered(save_as);
      if (saved_as) {
        // The file picked in a Save As.
        step = L"opening the file picked to save into";
        target = StorageApplicationPermissions::FutureAccessList()
                     .GetFileAsync(winrt::hstring(save_as))
                     .get();
      } else {
        // The phone's Downloads: the system puts an app's files in a folder
        // named after it, Downloads\Venetium.
        step = L"creating the file in Downloads";
        target = DownloadsFolder::CreateFileAsync(
                     staged.Name(), CreationCollisionOption::GenerateUniqueName)
                     .get();
      }
      // Moved, not copied: no second copy of a big download in the app's
      // folder (on the same volume a move is a rename; onto a memory card
      // the system copies and deletes). Copied only if the move is refused.
      bool moved = false;
      try {
        step = L"moving it there";
        staged.MoveAndReplaceAsync(target).get();
        moved = true;
      } catch (winrt::hresult_error const& error) {
        Log::Write(L"files: could not move " + path + L", copying it",
                   std::wstring(error.message()));
        step = L"copying it there";
        staged.CopyAndReplaceAsync(target).get();
      }
      if (saved_as) {
        Forget(save_as);
      }
      StorageFile copy = target;
      // Known before the placeholder exists: opening the download goes to
      // the moved file from then on (OpenDownloadCopy).
      Remember(Token(L'x', path), copy);
      if (moved) {
        // A placeholder under the old name: Chrome still finds a file there.
        HANDLE h = ::CreateFile2(path.c_str(), GENERIC_WRITE, 0, CREATE_NEW,
                                 nullptr);
        if (h != INVALID_HANDLE_VALUE) {
          ::CloseHandle(h);
        }
      }
      const std::wstring where(copy.Path());
      Log::Write(L"files: " + path + (moved ? L" moved to " : L" copied to ") +
                 (where.empty() ? std::wstring(copy.Name()) : where));
      done(where.empty() ? std::wstring(copy.Name()) : where, L"");
    } catch (winrt::hresult_error const& error) {
      wchar_t text[512];
      swprintf_s(text, L"%s: %s (0x%08x)", step.c_str(),
                 error.message().c_str(), static_cast<unsigned>(error.code()));
      Log::Write(L"files: could not copy " + path + L" out", text);
      done(L"", text);
    }
  }).detach();
}

bool OpenDownloadCopy(std::wstring const& path, bool reveal) {
  const std::wstring token = Token(L'x', path);
  if (!Remembered(token)) {
    return false;
  }
  StorageApplicationPermissions::FutureAccessList()
      .GetFileAsync(winrt::hstring(token))
      .Completed([path, reveal](IAsyncOperation<StorageFile> const& op,
                                AsyncStatus status) {
        if (status != AsyncStatus::Completed) {
          Log::Write(L"open: the copy of " + path + L" is gone");
          return;
        }
        StorageFile copy = op.GetResults();
        const std::wstring where(copy.Path());
        if (!reveal) {
          LaunchFile(copy, where);
          return;
        }
        // Its folder, when the app may see it; else the file itself.
        copy.GetParentAsync().Completed(
            [copy, where](IAsyncOperation<StorageFolder> const& parent_op,
                          AsyncStatus parent_status) {
              StorageFolder parent = parent_status == AsyncStatus::Completed
                                         ? parent_op.GetResults()
                                         : nullptr;
              if (parent) {
                LaunchFolder(parent, std::wstring(parent.Path()));
              } else {
                LaunchFile(copy, where);
              }
            });
      });
  return true;
}

std::wstring PathToSaveInto(StorageFile const& picked) {
  const std::wstring path(picked.Path());
  const std::wstring staging = DownloadStaging();
  if (staging.empty() || (!path.empty() && Win32CanWrite(path))) {
    return path;
  }
  // The engine may not write there: a file in the staging folder instead, in
  // a folder of its own so the name stays the one chosen, copied into the
  // picked file when the download finishes (ExportDownload).
  const std::wstring folder =
      staging + L"\\saved-" + std::to_wstring(::GetTickCount64());
  ::CreateDirectoryW(folder.c_str(), nullptr);
  const std::wstring staged = folder + L"\\" + std::wstring(picked.Name());
  Remember(Token(L's', staged), picked);
  Log::Write(L"files: the engine may not write " + path + L", it saves to " +
             staged + L" and the shell copies it");
  return staged;
}

void CleanUpStagedDownloads() {
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
  } catch (...) {
  }
  const std::wstring staging = DownloadStaging();
  if (!staging.empty()) {
    CleanUp(staging, false);
  }
}

}  // namespace venetium_w10m::client
