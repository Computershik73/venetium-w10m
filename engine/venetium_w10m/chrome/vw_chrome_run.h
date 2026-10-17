// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// venetium_w10m_run with Chrome's own interface: what chrome.exe and
// chrome_elf.dll put in place before chrome.dll's ChromeMain, and ChromeMain
// itself, as far as a DLL in a UWP app on a phone needs them.

#ifndef VENETIUM_W10M_CHROME_VW_CHROME_RUN_H_
#define VENETIUM_W10M_CHROME_VW_CHROME_RUN_H_

#include <windows.h>

namespace base {
class CommandLine;
}  // namespace base

namespace venetium_w10m {

struct EngineConfig;

// Chrome's switches on top of the engine's, before anything reads the
// command line: the profile's place, no first-run, sync, component updates
// or background fetches, the touch interface, and the features a phone in
// one process does without.
void AddChromeSwitches(const EngineConfig& config,
                       base::CommandLine* command_line);

// The directories Chrome would ask the shell namespace for.
void SetUpChromePaths(const EngineConfig& config);

// Runs Chrome on this thread until it ends; returns ContentMain's code.
int RunChrome(HINSTANCE instance);

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_CHROME_RUN_H_
