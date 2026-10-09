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
// a failure value -- 0 (FALSE, NULL, no handle) for the Win32 family,
// E_NOTIMPL for the COM-style libraries whose functions return an HRESULT.
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

bool ReturnsHresult(const char* dll) {
  static const char* const kHresultDlls[] = {
      "d3d", "dxgi", "dcomp", "dxva2", "mf", "dwrite", "propsys",
      "uiautomationcore", "ole32", "oleaut32", "combase", "shcore",
  };
  for (const char* prefix : kHresultDlls) {
    if (_strnicmp(dll, prefix, strlen(prefix)) == 0) {
      return true;
    }
  }
  return false;
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
    return ReturnsHresult(dll) ? reinterpret_cast<FARPROC>(&ReturnNotImplemented)
                               : reinterpret_cast<FARPROC>(&ReturnZero);
  }
  return nullptr;
}

}  // namespace

extern "C" const PfnDliHook __pfnDliFailureHook2 = OnDelayLoadFailure;
