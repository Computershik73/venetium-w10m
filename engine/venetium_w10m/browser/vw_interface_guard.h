// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Whether Chrome's own interface can run on this phone, remembered between
// runs, as vw_present_guard.h remembers the graphics driver.
//
// A run with Chrome's interface first writes down that it is trying. If it
// ends -- dies, or hangs and is closed -- before Chrome's window has been on
// screen for a while, the note stays; after kTries such runs the shell's
// own interface is used instead, whose menu has the log and the engine
// switches. A new build of the engine tries Chrome's again.

#ifndef VENETIUM_W10M_BROWSER_VW_INTERFACE_GUARD_H_
#define VENETIUM_W10M_BROWSER_VW_INTERFACE_GUARD_H_

#include "base/files/file_path.h"

namespace venetium_w10m::interface_guard {

constexpr int kTries = 2;

// Any thread. Whether runs with Chrome's interface have ended before it
// showed kTries times in a row, with this build.
bool GaveUp(const base::FilePath& profile_dir);

// Before ContentMain: this run tries Chrome's interface, and counts against
// it until Proven().
void Trying(const base::FilePath& profile_dir);

// Any thread: Chrome's window has been on screen for a while, or the app
// is being suspended with it there. Nothing unless Trying() came first.
void Proven();

}  // namespace venetium_w10m::interface_guard

#endif  // VENETIUM_W10M_BROWSER_VW_INTERFACE_GUARD_H_
