// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// What happens when a phone lacks something the engine imports.
//
// Windows 10 Mobile's system DLLs are a subset of the desktop's, function by
// function, and which functions are missing differs from one OS build to the
// next. Every system DLL the engine does not need to load is delay-loaded
// (BUILD.gn), so a missing one does not stop the engine from loading -- but
// by default the first call into it raises an exception and ends the app.
//
// This hook turns that call into a failing call instead: the DLL and function
// are written to the log (once each), and the caller gets a stub that returns
// that function's failure value. Which value depends on what the function
// returns, decided by its name and then by its DLL's family:
//   E_NOTIMPL                   COM-style functions (an HRESULT), and the
//                               ones where only a negative value fails: an
//                               NTSTATUS (LSA), a SECURITY_STATUS (CNG)
//   ERROR_CALL_NOT_IMPLEMENTED  functions returning a Win32 error code, where
//                               0 would mean success -- the registry, ETW,
//                               iphlpapi, DNS, configuration manager, SSPI
//   0                           the rest: FALSE, NULL, no handle, no count
// Most such calls are on paths a phone never needs (the event log, printing,
// smart cards); the log says which ones were not.

#include <windows.h>

#include <delayimp.h>
#include <string.h>
#include <wctype.h>

#include <atomic>
#include <iterator>

#include "venetium_w10m/common/vw_log.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

// The stubs take no parameters on purpose: on 32-bit ARM the caller cleans
// up its own arguments, so a stub called with any argument list returns
// safely.
uintptr_t WINAPI ReturnZero() {
  return 0;
}

HRESULT WINAPI ReturnNotImplemented() {
  return E_NOTIMPL;
}

LONG WINAPI ReturnCallNotImplemented() {
  return ERROR_CALL_NOT_IMPLEMENTED;
}

// For some functions no failure value will do, and gecko-w10m met each of
// these on a phone: they get a substitute that does what the call means here.

// Zero would read as "the first handle is signalled": a message loop that
// waits with it spins at full speed. There are no messages to wait for here,
// but the handles are real, and waiting on them is what the call comes to.
DWORD WINAPI SubstituteMsgWaitForMultipleObjectsEx(DWORD count,
                                                   const HANDLE* handles,
                                                   DWORD milliseconds,
                                                   DWORD wake_mask,
                                                   DWORD flags) {
  const BOOL alertable = (flags & MWMO_ALERTABLE) != 0;
  if (!count) {
    // WaitForMultipleObjectsEx rejects an empty set, and a caller with no
    // handles was only ever waiting for messages.
    ::SleepEx(milliseconds, alertable);
    return WAIT_TIMEOUT;
  }
  return ::WaitForMultipleObjectsEx(count, handles,
                                    (flags & MWMO_WAITALL) != 0,
                                    milliseconds, alertable);
}

DWORD WINAPI SubstituteMsgWaitForMultipleObjects(DWORD count,
                                                 const HANDLE* handles,
                                                 BOOL wait_all,
                                                 DWORD milliseconds,
                                                 DWORD wake_mask) {
  if (!count) {
    ::Sleep(milliseconds);
    return WAIT_TIMEOUT;
  }
  return ::WaitForMultipleObjects(count, handles, wait_all, milliseconds);
}

// The raw input calls report failure as (UINT)-1, their result through an
// out parameter: zero is "it worked", with a count never written.
UINT WINAPI ReturnFailingUint() {
  return static_cast<UINT>(-1);
}

// Zero paints every system colour black. Windows 10's light-theme defaults
// (COLORREF, 0x00BBGGRR).
DWORD WINAPI SubstituteGetSysColor(int index) {
  static const DWORD kColors[] = {
      0x00C8C8C8,  // 0  COLOR_SCROLLBAR
      0x00000000,  // 1  COLOR_BACKGROUND
      0x00D1B499,  // 2  COLOR_ACTIVECAPTION
      0x00DBCDBF,  // 3  COLOR_INACTIVECAPTION
      0x00F0F0F0,  // 4  COLOR_MENU
      0x00FFFFFF,  // 5  COLOR_WINDOW
      0x00646464,  // 6  COLOR_WINDOWFRAME
      0x00000000,  // 7  COLOR_MENUTEXT
      0x00000000,  // 8  COLOR_WINDOWTEXT
      0x00000000,  // 9  COLOR_CAPTIONTEXT
      0x00B4B4B4,  // 10 COLOR_ACTIVEBORDER
      0x00FCF7F4,  // 11 COLOR_INACTIVEBORDER
      0x00ABABAB,  // 12 COLOR_APPWORKSPACE
      0x00D77800,  // 13 COLOR_HIGHLIGHT
      0x00FFFFFF,  // 14 COLOR_HIGHLIGHTTEXT
      0x00F0F0F0,  // 15 COLOR_BTNFACE
      0x00A0A0A0,  // 16 COLOR_BTNSHADOW
      0x006D6D6D,  // 17 COLOR_GRAYTEXT
      0x00000000,  // 18 COLOR_BTNTEXT
      0x00000000,  // 19 COLOR_INACTIVECAPTIONTEXT
      0x00FFFFFF,  // 20 COLOR_BTNHIGHLIGHT
      0x00696969,  // 21 COLOR_3DDKSHADOW
      0x00E3E3E3,  // 22 COLOR_3DLIGHT
      0x00000000,  // 23 COLOR_INFOTEXT
      0x00E1FFFF,  // 24 COLOR_INFOBK
      0x00000000,  // 25 (unused)
      0x00CC6600,  // 26 COLOR_HOTLIGHT
      0x00EAD1B9,  // 27 COLOR_GRADIENTACTIVECAPTION
      0x00F2E4D7,  // 28 COLOR_GRADIENTINACTIVECAPTION
      0x00D77800,  // 29 COLOR_MENUHILIGHT
      0x00F0F0F0,  // 30 COLOR_MENUBAR
  };
  if (index < 0 || index >= static_cast<int>(std::size(kColors))) {
    return 0;
  }
  return kColors[index];
}

// Zero means "never blink", which reads as a dead field.
UINT WINAPI SubstituteGetCaretBlinkTime() {
  return 530;
}

// Upper case, in place or of one character (a pointer whose high word is
// zero is a character). Zero would end the string -- or be the character.
LPWSTR WINAPI SubstituteCharUpperW(LPWSTR text) {
  const uintptr_t value = reinterpret_cast<uintptr_t>(text);
  if ((value >> 16) == 0) {
    return reinterpret_cast<LPWSTR>(
        static_cast<uintptr_t>(towupper(static_cast<wchar_t>(value))));
  }
  for (wchar_t* c = text; *c; ++c) {
    *c = towupper(*c);
  }
  return text;
}

LPWSTR WINAPI SubstituteCharLowerW(LPWSTR text) {
  const uintptr_t value = reinterpret_cast<uintptr_t>(text);
  if ((value >> 16) == 0) {
    return reinterpret_cast<LPWSTR>(
        static_cast<uintptr_t>(towlower(static_cast<wchar_t>(value))));
  }
  for (wchar_t* c = text; *c; ++c) {
    *c = towlower(*c);
  }
  return text;
}

// The credential prompts return a Win32 error code, and zero -- "the user
// typed something" -- would send the caller on with no credentials to read.
// Chrome asks before it shows a saved password.
DWORD WINAPI ReturnCancelled() {
  return ERROR_CANCELLED;
}

// The printer list: the stubs leave the last error as it was, which Chrome
// reads to tell "no printers" from "the call failed". There are no printers.
BOOL WINAPI SubstituteEnumPrinters(DWORD flags,
                                   void* name,
                                   DWORD level,
                                   BYTE* printers,
                                   DWORD size,
                                   DWORD* needed,
                                   DWORD* returned) {
  if (needed) {
    *needed = 0;
  }
  if (returned) {
    *returned = 0;
  }
  ::SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL WINAPI SubstituteGetDefaultPrinter(void* buffer, DWORD* size) {
  ::SetLastError(ERROR_FILE_NOT_FOUND);
  return FALSE;
}

struct Substitute {
  const char* name;
  FARPROC proc;
};

const Substitute kSubstitutes[] = {
    {"MsgWaitForMultipleObjectsEx",
     reinterpret_cast<FARPROC>(&SubstituteMsgWaitForMultipleObjectsEx)},
    {"MsgWaitForMultipleObjects",
     reinterpret_cast<FARPROC>(&SubstituteMsgWaitForMultipleObjects)},
    {"GetRawInputDeviceList", reinterpret_cast<FARPROC>(&ReturnFailingUint)},
    {"GetRawInputDeviceInfoW", reinterpret_cast<FARPROC>(&ReturnFailingUint)},
    {"GetRawInputDeviceInfoA", reinterpret_cast<FARPROC>(&ReturnFailingUint)},
    {"GetRawInputData", reinterpret_cast<FARPROC>(&ReturnFailingUint)},
    {"GetRawInputBuffer", reinterpret_cast<FARPROC>(&ReturnFailingUint)},
    {"GetSysColor", reinterpret_cast<FARPROC>(&SubstituteGetSysColor)},
    {"GetCaretBlinkTime",
     reinterpret_cast<FARPROC>(&SubstituteGetCaretBlinkTime)},
    {"CharUpperW", reinterpret_cast<FARPROC>(&SubstituteCharUpperW)},
    {"CharLowerW", reinterpret_cast<FARPROC>(&SubstituteCharLowerW)},
    {"CredUIPromptForWindowsCredentialsW",
     reinterpret_cast<FARPROC>(&ReturnCancelled)},
    {"CredUIPromptForWindowsCredentialsA",
     reinterpret_cast<FARPROC>(&ReturnCancelled)},
    {"CredUIPromptForCredentialsW",
     reinterpret_cast<FARPROC>(&ReturnCancelled)},
    {"CredUIPromptForCredentialsA",
     reinterpret_cast<FARPROC>(&ReturnCancelled)},
    {"EnumPrintersW", reinterpret_cast<FARPROC>(&SubstituteEnumPrinters)},
    {"EnumPrintersA", reinterpret_cast<FARPROC>(&SubstituteEnumPrinters)},
    {"GetDefaultPrinterW",
     reinterpret_cast<FARPROC>(&SubstituteGetDefaultPrinter)},
    {"GetDefaultPrinterA",
     reinterpret_cast<FARPROC>(&SubstituteGetDefaultPrinter)},
};

FARPROC SubstituteFor(const char* name) {
  if (!name) {
    return nullptr;
  }
  for (const Substitute& substitute : kSubstitutes) {
    if (strcmp(name, substitute.name) == 0) {
      return substitute.proc;
    }
  }
  return nullptr;
}

enum class Failure { kZero, kHresult, kErrorCode };

bool HasPrefix(const char* text, const char* prefix) {
  return _strnicmp(text, prefix, strlen(prefix)) == 0;
}

// By function name, first match wins. Prefixes, so the A/W and Ex variants
// are covered too.
struct Rule {
  const char* prefix;
  Failure failure;
};
constexpr Rule kNameRules[] = {
    // In the COM libraries, but not returning an HRESULT.
    {"CoTaskMem", Failure::kZero},
    {"CoGetCurrentProcess", Failure::kZero},
    {"CoUninitialize", Failure::kZero},
    {"OleUninitialize", Failure::kZero},
    {"StringFromGUID2", Failure::kZero},
    {"Sys", Failure::kZero},  // SysAllocString, SysStringLen, ...
    {"VariantInit", Failure::kZero},
    {"SafeArrayGetDim", Failure::kZero},
    {"SafeArrayGetElemsize", Failure::kZero},
    {"OpenThemeData", Failure::kZero},
    {"GetWindowTheme", Failure::kZero},
    {"IsThemeActive", Failure::kZero},
    {"IsAppThemed", Failure::kZero},
    {"IsThemePartDefined", Failure::kZero},
    {"GetThemeSysColor", Failure::kZero},
    {"GetThemeSysSize", Failure::kZero},
    // In shell32 and shlwapi, returning an HRESULT.
    {"SHGetKnownFolder", Failure::kHresult},
    {"SHGetSpecialFolderLocation", Failure::kHresult},
    // Zero would hand Chrome a null IApplicationAssociationRegistration to
    // ask which browser is the default, at every start.
    {"SHCreateAssociationRegistration", Failure::kHresult},
    {"SHGetFolderPath", Failure::kHresult},
    {"SHCreateItem", Failure::kHresult},
    {"SHCreateShellItemArray", Failure::kHresult},
    {"SHCreateStreamOnFile", Failure::kHresult},
    {"SHGetPropertyStoreForWindow", Failure::kHresult},
    {"SHOpenFolderAndSelectItems", Failure::kHresult},
    {"SHParseDisplayName", Failure::kHresult},
    {"SHGetDesktopFolder", Failure::kHresult},
    {"SHGetIDListFromObject", Failure::kHresult},
    {"SHGetStockIconInfo", Failure::kHresult},
    {"SHQueryUserNotificationState", Failure::kHresult},
    {"SHLoadIndirectString", Failure::kHresult},
    {"SHStrDup", Failure::kHresult},
    {"SHAutoComplete", Failure::kHresult},
    {"SetCurrentProcessExplicitAppUserModelID", Failure::kHresult},
    {"GetCurrentProcessExplicitAppUserModelID", Failure::kHresult},
    {"UrlCreateFromPath", Failure::kHresult},
    {"PathCreateFromUrl", Failure::kHresult},
    {"UrlEscape", Failure::kHresult},
    {"UrlUnescape", Failure::kHresult},
    {"UrlCanonicalize", Failure::kHresult},
    {"UrlCombine", Failure::kHresult},
    {"AssocQueryString", Failure::kHresult},
    {"HashData", Failure::kHresult},
    {"IUnknown_", Failure::kHresult},
    // The print dialog (comdlg32): zero is S_OK.
    {"PrintDlgEx", Failure::kHresult},
    // In secur32, but returning a BOOLEAN: the error code would read as TRUE.
    {"GetUserNameEx", Failure::kZero},
    {"GetComputerObjectName", Failure::kZero},
    {"TranslateName", Failure::kZero},
    // LSA (secur32, advapi32): an NTSTATUS, failing only when negative.
    {"LsaNtStatusToWinError", Failure::kErrorCode},
    {"Lsa", Failure::kHresult},
    // The registry and ETW (advapi32, shlwapi): there 0 is ERROR_SUCCESS.
    {"RegisterEventSource", Failure::kZero},  // a HANDLE; before "Reg"
    {"RegisterServiceCtrlHandler", Failure::kZero},
    {"RegisterTraceGuids", Failure::kErrorCode},
    {"Reg", Failure::kErrorCode},
    {"SHReg", Failure::kErrorCode},
    {"SHGetValue", Failure::kErrorCode},
    {"SHSetValue", Failure::kErrorCode},
    {"SHDeleteKey", Failure::kErrorCode},
    {"SHDeleteValue", Failure::kErrorCode},
    {"SHQueryValue", Failure::kErrorCode},
    {"SHQueryInfoKey", Failure::kErrorCode},
    {"SHEnumKey", Failure::kErrorCode},
    {"SHEnumValue", Failure::kErrorCode},
    {"SHCopyKey", Failure::kErrorCode},
    {"Event", Failure::kErrorCode},  // EventRegister, EventWrite, ...
    {"StartTrace", Failure::kErrorCode},
    {"ControlTrace", Failure::kErrorCode},
    {"EnableTrace", Failure::kErrorCode},
    {"EnumerateTraceGuids", Failure::kErrorCode},
    {"GetNamedSecurityInfo", Failure::kErrorCode},
    {"SetNamedSecurityInfo", Failure::kErrorCode},
    {"GetSecurityInfo", Failure::kErrorCode},
    {"SetSecurityInfo", Failure::kErrorCode},
    {"SetEntriesInAcl", Failure::kErrorCode},
    {"WNet", Failure::kErrorCode},
    // In the error-code libraries, but not returning one.
    {"if_nametoindex", Failure::kZero},
    {"if_indextoname", Failure::kZero},
    {"FreeMibTable", Failure::kZero},
    {"DnsFree", Failure::kZero},
    {"DnsRecordListFree", Failure::kZero},
};

// By DLL, for the names no rule above matches.
constexpr Rule kDllRules[] = {
    {"d3d", Failure::kHresult},          {"dxgi", Failure::kHresult},
    {"dcomp", Failure::kHresult},        {"dxva2", Failure::kHresult},
    {"dwmapi", Failure::kHresult},       {"mf", Failure::kHresult},
    {"dwrite", Failure::kHresult},       {"propsys", Failure::kHresult},
    {"uiautomationcore", Failure::kHresult},
    {"ole32", Failure::kHresult},        {"oleaut32", Failure::kHresult},
    {"oleacc", Failure::kHresult},       {"combase", Failure::kHresult},
    {"shcore", Failure::kHresult},       {"uxtheme", Failure::kHresult},
    {"urlmon", Failure::kHresult},       {"wer", Failure::kHresult},
    {"iphlpapi", Failure::kErrorCode},   {"dnsapi", Failure::kErrorCode},
    {"cfgmgr32", Failure::kErrorCode},   {"pdh", Failure::kErrorCode},
    {"secur32", Failure::kErrorCode},    {"ncrypt", Failure::kHresult},
    {"powrprof", Failure::kErrorCode},   {"dhcpcsvc", Failure::kErrorCode},
    {"msi", Failure::kErrorCode},        {"rpcrt4", Failure::kErrorCode},
    {"netapi32", Failure::kErrorCode},   {"rstrtmgr", Failure::kErrorCode},
};

Failure FailureFor(const char* dll, const char* name) {
  if (name) {
    for (const Rule& rule : kNameRules) {
      if (HasPrefix(name, rule.prefix)) {
        return rule.failure;
      }
    }
  }
  for (const Rule& rule : kDllRules) {
    if (HasPrefix(dll, rule.prefix)) {
      return rule.failure;
    }
  }
  return Failure::kZero;
}

FARPROC WINAPI OnDelayLoadFailure(unsigned reason, PDelayLoadInfo info) {
  const char* dll = info && info->szDll ? info->szDll : "?";
  if (reason == dliFailLoadLib) {
    venetium_w10m::NoteF("delay-load: %s is not on this phone, its calls fail",
                         dll);
    // Any module will do as the DLL's stand-in: GetProcAddress on it fails
    // for every name, and each name comes back here as dliFailGetProc. This
    // one, pinned first: when two threads load the same DLL at once, the
    // delay-load helper keeps one handle and FreeLibrary()s the other --
    // which, the engine being the stand-in, unloaded the engine under every
    // thread running it (two threads asking for pdh.dll did, on Chrome's
    // first menu). A pinned module is never unloaded.
    static const bool pinned = [] {
      HMODULE self = nullptr;
      return ::GetModuleHandleExW(
                 GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                     GET_MODULE_HANDLE_EX_FLAG_PIN,
                 reinterpret_cast<LPCWSTR>(&__ImageBase), &self) != 0;
    }();
    if (!pinned) {
      venetium_w10m::Note("delay-load: the engine could not be pinned");
    }
    return reinterpret_cast<FARPROC>(&__ImageBase);
  }
  if (reason == dliFailGetProc) {
    if (info->dlp.fImportByName && info->dlp.szProcName) {
      venetium_w10m::NoteF("delay-load: %s!%s is not on this phone, stubbed",
                           dll, info->dlp.szProcName);
    } else {
      venetium_w10m::NoteF("delay-load: %s!#%u is not on this phone, stubbed",
                           dll, static_cast<unsigned>(info->dlp.dwOrdinal));
    }
    const char* name =
        info->dlp.fImportByName ? info->dlp.szProcName : nullptr;
    if (FARPROC substitute = SubstituteFor(name)) {
      return substitute;
    }
    switch (FailureFor(dll, name)) {
      case Failure::kHresult:
        return reinterpret_cast<FARPROC>(&ReturnNotImplemented);
      case Failure::kErrorCode:
        return reinterpret_cast<FARPROC>(&ReturnCallNotImplemented);
      case Failure::kZero:
        return reinterpret_cast<FARPROC>(&ReturnZero);
    }
  }
  return nullptr;
}

}  // namespace

extern "C" const PfnDliHook __pfnDliFailureHook2 = OnDelayLoadFailure;
