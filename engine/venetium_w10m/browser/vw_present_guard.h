// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// What this phone's graphics driver could not do, remembered between runs.
//
// A run that tries a hardware way of showing frames first writes down that
// it does. If it dies -- or hangs and is closed -- before that way showed a
// frame, the note stays, and after kTries such runs the way is left alone:
// a driver that cannot do it would otherwise end every launch. A way that
// fails without taking the process down (GL does not start) is given up at
// once. Each way has its own count: frames shown through plain Direct3D 11
// say nothing about ANGLE, so they do not clear its count.
//
// The way back: a different build of the engine, or the user switching
// Hardware graphics off and on (--vw-gpu=retry), forgets all of it.

#ifndef VENETIUM_W10M_BROWSER_VW_PRESENT_GUARD_H_
#define VENETIUM_W10M_BROWSER_VW_PRESENT_GUARD_H_

#include "base/files/file_path.h"
#include "base/synchronization/lock.h"
#include "venetium_w10m/browser/vw_panel.h"

namespace venetium_w10m {

class PresentGuard {
 public:
  // Runs a way may end before it is left alone.
  static constexpr int kTries = 2;

  static PresentGuard* Get();

  // Before the browser starts: reads |file|; |forget| drops what it says.
  void Load(const base::FilePath& file, bool forget);

  // Whether this run may try |kind| (kAngle or kD3D11), and how many runs
  // with it ended before a frame.
  bool Allowed(PresentKind kind);
  int Failures(PresentKind kind);

  // This run tries |kind|: counted against it until Worked(kind).
  void Trying(PresentKind kind);

  // Any thread: |kind| put a frame on screen. True the first time.
  bool Worked(PresentKind kind);

  // Any thread: |kind| failed in this run. Unless it showed a frame first,
  // later runs leave it alone.
  void GaveUp(PresentKind kind);

 private:
  int* Count(PresentKind kind);
  void Write();

  base::Lock lock_;
  base::FilePath file_;
  int angle_ = 0;
  int d3d11_ = 0;
  bool angle_worked_ = false;
  bool d3d11_worked_ = false;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_PRESENT_GUARD_H_
