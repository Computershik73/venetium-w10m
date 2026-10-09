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
//   E_NOTIMPL                   COM-style functions (an HRESULT)
//   ERROR_CALL_NOT_IMPLEMENTED  functions returning a Win32 error code, where
//                               0 would mean success -- the registry, ETW,
//                               iphlpapi, DNS, configuration manager, SSPI
//   0                           the rest: FALSE, NULL, no handle, no count
// Most such calls are on paths a phone never needs (the event log, printing,
// smart cards); the log says which ones were not.

#include <windows.h>

#include <delayimp.h>
#include <string.h>

#include <atomic>

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
    {"secur32", Failure::kErrorCode},    {"ncrypt", Failure::kErrorCode},
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
    // for every name, and each name comes back here as dliFailGetProc.
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
    switch (FailureFor(dll, info->dlp.fImportByName ? info->dlp.szProcName
                                                    : nullptr)) {
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
