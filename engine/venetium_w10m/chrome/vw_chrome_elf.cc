// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// What chrome_elf.dll gives chrome.dll, given by the engine.
//
// On the desktop chrome.exe loads chrome_elf.dll first: it finds the user
// data directory, starts Crashpad, blocks third-party DLLs, and chrome.dll
// imports a few answers from it. None of that has a place in a UWP app on a
// phone -- there is one DLL, the shell keeps the crash log, and the package
// decides what loads -- but Chrome's code still asks. These are the answers
// (the same as chrome/chrome_elf/chrome_elf_test_stubs.cc and
// components/crash/core/app/crash_export_stubs.cc give in tests, which are
// test-only targets).

#include <windows.h>

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <wchar.h>

#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/files/file_path.h"
#include "chrome/common/chrome_switches.h"
#include "venetium_w10m/common/vw_log.h"

namespace crash_reporter {
struct ProductInfo;
struct Report;
}  // namespace crash_reporter

extern "C" {

// chrome/chrome_elf/chrome_elf_main.h

// What DUMP_WILL_BE_CHECK and base::debug::DumpWithoutCrashing come to: no
// crash report to send, so the stack goes into the log.
void DumpProcessWithoutCrash() {
  venetium_w10m::NoteF("dump without crashing: %s",
                       base::debug::StackTrace().ToString().c_str());
}

// The engine puts --user-data-dir on the command line (app/vw_main.cc).
bool GetUserDataDirectoryThunk(wchar_t* user_data_dir,
                               size_t user_data_dir_length,
                               wchar_t* invalid_user_data_dir,
                               size_t invalid_user_data_dir_length) {
  base::FilePath path =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          switches::kUserDataDir);
  if (!path.empty() && path.EndsWithSeparator()) {
    path = path.StripTrailingSeparators();
  }
  wcsncpy_s(user_data_dir, user_data_dir_length, path.value().c_str(),
            _TRUNCATE);
  wcsncpy_s(invalid_user_data_dir, invalid_user_data_dir_length, L"",
            _TRUNCATE);
  return !path.empty();
}

bool IsTemporaryUserDataDirectoryCreatedForHeadless() {
  return false;
}

void SignalInitializeCrashReporting() {}

void SignalChromeElf() {}

void SetMetricsClientId(const char* client_id) {}

bool IsBrowserProcess() {
  return true;
}

bool IsExtensionPointDisableSet() {
  return false;
}

// chrome/chrome_elf/third_party_dlls/public_api.h: nothing is blocked.

bool IsThirdPartyInitialized() {
  return false;
}

uint32_t DrainLog(uint8_t* buffer, uint32_t buffer_size,
                  uint32_t* log_remaining) {
  if (log_remaining) {
    *log_remaining = 0;
  }
  return 0;
}

bool RegisterLogNotification(HANDLE event_handle) {
  return false;
}

uint32_t GetBlockedModulesCount() {
  return 0;
}

uint32_t GetUniqueBlockedModulesCount() {
  return 0;
}

void DisableHook() {}

int32_t GetApplyHookResult() {
  return 0;
}

// components/crash/core/app/crash_export_thunks.h: no Crashpad.

void RequestSingleCrashUpload_ExportThunk(const char* local_id) {}

size_t GetCrashReports_ExportThunk(crash_reporter::Report* reports,
                                   size_t reports_size) {
  return 0;
}

int CrashForException_ExportThunk(EXCEPTION_POINTERS* info) {
  return EXCEPTION_CONTINUE_SEARCH;
}

void SetUploadConsent_ExportThunk(bool consent) {}

bool GetUploadConsent_ExportThunk() {
  return false;
}

void GetProductInfo_ExportThunk(crash_reporter::ProductInfo* product_info) {}

HANDLE InjectDumpForHungInput_ExportThunk(HANDLE process) {
  return nullptr;
}

const wchar_t* GetCrashpadDatabasePath_ExportThunk() {
  return nullptr;
}

void ClearReportsBetween_ExportThunk(time_t begin, time_t end) {}

bool DumpHungProcessWithPtype_ExportThunk(HANDLE process, const char* ptype) {
  return false;
}

}  // extern "C"
