// Log.cpp
#include "pch.h"

#include "client/Log.h"

#include <mutex>

namespace venetium_w10m::client {
namespace {

constexpr size_t kRingCapacity = 400;

std::mutex g_mutex;
HANDLE g_file = INVALID_HANDLE_VALUE;
std::wstring g_dir;
std::wstring g_path;
std::vector<std::wstring> g_ring;
std::function<void(std::wstring)> g_handler;

std::wstring Timestamp() {
  SYSTEMTIME st{};
  ::GetLocalTime(&st);
  wchar_t buf[32];
  swprintf_s(buf, L"%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds);
  return buf;
}

std::string ToUtf8(std::wstring_view s) {
  if (s.empty()) return {};
  int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr,
                                0, nullptr, nullptr);
  std::string out(n, '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n,
                        nullptr, nullptr);
  return out;
}

std::wstring FromUtf8(const char* s) {
  if (!s || !*s) return {};
  int n = ::MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (n <= 1) return {};
  std::wstring out(static_cast<size_t>(n - 1), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
  return out;
}

// The runs before this one: venetium-previous.log the last, then
// venetium-previous-2.log and so on.
constexpr int kKeptRuns = 4;

std::wstring RunLogName(int run) {
  return run == 1 ? std::wstring(L"venetium-previous.log")
                  : L"venetium-previous-" + std::to_wstring(run) + L".log";
}

}  // namespace

void Log::Init(std::wstring_view localStatePath) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file != INVALID_HANDLE_VALUE) return;
  g_dir.assign(localStatePath);
  if (!g_dir.empty() && g_dir.back() != L'\\') g_dir += L'\\';
  g_path = g_dir + L"venetium.log";

  // Keep four previous runs beside this one: the run that went wrong is the
  // one a report needs, and it is not always the last but one (a check on
  // the phone takes a few launches). The next launch would otherwise append
  // to it forever.
  for (int run = kKeptRuns; run >= 1; --run) {
    const std::wstring older = g_dir + RunLogName(run);
    if (run == kKeptRuns) {
      ::DeleteFileW(older.c_str());
    } else {
      ::MoveFileExW(older.c_str(), (g_dir + RunLogName(run + 1)).c_str(),
                    MOVEFILE_REPLACE_EXISTING);
    }
  }
  ::MoveFileExW(g_path.c_str(), (g_dir + RunLogName(1)).c_str(),
                MOVEFILE_REPLACE_EXISTING);

  CREATEFILE2_EXTENDED_PARAMETERS params{};
  params.dwSize = sizeof(params);
  params.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
  g_file = ::CreateFile2(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                         OPEN_ALWAYS, &params);
  if (g_file == INVALID_HANDLE_VALUE) {
    g_path.clear();
    return;
  }
  const char* banner = "==== Venetium session start ====\r\n";
  DWORD written = 0;
  ::WriteFile(g_file, banner, (DWORD)strlen(banner), &written, nullptr);
}

void Log::Write(std::wstring_view line) {
  std::wstring stamped = Timestamp() + L"  " + std::wstring(line);
  std::function<void(std::wstring)> handler;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) {
      std::string utf8 = ToUtf8(stamped) + "\r\n";
      DWORD written = 0;
      // Not flushed: what WriteFile took is the system's, written to the
      // file even if the app ends the next moment. A flush per line waited
      // for the phone's flash on whichever thread logged -- the GPU's, the
      // renderer's -- and only a crash of the system loses unflushed lines.
      ::WriteFile(g_file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    }
    g_ring.push_back(stamped);
    if (g_ring.size() > kRingCapacity) {
      g_ring.erase(g_ring.begin(),
                   g_ring.begin() + (g_ring.size() - kRingCapacity));
    }
    handler = g_handler;
  }
  ::OutputDebugStringW((stamped + L"\r\n").c_str());
  if (handler) handler(stamped);
}

void Log::WriteFromFault(std::wstring_view line) {
  HANDLE file = g_file;
  if (file == INVALID_HANDLE_VALUE) return;
  std::string utf8 =
      ToUtf8(Timestamp() + L"  " + std::wstring(line)) + "\r\n";
  DWORD written = 0;
  ::WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
  ::FlushFileBuffers(file);
}

void Log::Write(std::wstring_view label, std::wstring_view value) {
  Write(std::wstring(label) + L": " + std::wstring(value));
}

void Log::WriteNum(std::wstring_view label, long long value) {
  Write(std::wstring(label) + L": " + std::to_wstring(value));
}

void Log::WriteUtf8(const char* line) {
  Write(L"engine  " + FromUtf8(line));
}

std::vector<std::wstring> Log::Recent() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ring;
}

void Log::OnLine(std::function<void(std::wstring)> handler) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_handler = std::move(handler);
}

std::wstring Log::Path() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_path;
}

void Log::Mirror() {
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
  } catch (...) {
  }
  try {
    using namespace winrt::Windows::Storage;
    auto folder = KnownFolders::PicturesLibrary()
                      .CreateFolderAsync(L"Venetium logs",
                                         CreationCollisionOption::OpenIfExists)
                      .get();
    std::wstring dir;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_file != INVALID_HANDLE_VALUE) ::FlushFileBuffers(g_file);
      dir = g_dir;
    }
    int copied = 0;
    std::vector<std::wstring> names = {L"venetium.log", L"chromium.log"};
    for (int run = 1; run <= kKeptRuns; ++run) {
      names.push_back(RunLogName(run));
    }
    for (const std::wstring& name : names) {
      const std::wstring full = dir + name;
      if (::GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) {
        continue;
      }
      auto file = StorageFile::GetFileFromPathAsync(full).get();
      file.CopyAsync(folder, name, NameCollisionOption::ReplaceExisting).get();
      ++copied;
    }
    Write(L"mirror: " + std::to_wstring(copied) +
          L" logs copied to Pictures\\Venetium logs");
  } catch (winrt::hresult_error const& e) {
    Write(L"mirror: failed", std::wstring(e.message()));
  } catch (...) {
    Write(L"mirror: failed");
  }
}

}  // namespace venetium_w10m::client
