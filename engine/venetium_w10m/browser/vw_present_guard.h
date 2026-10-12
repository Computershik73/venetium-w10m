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
// The way back: a different package (a new build) or other engine switches,
// or the user switching Hardware graphics off and on (--vw-gpu=retry),
// forgets all of it.
//
// Hardware video decoding is remembered the same way and forgotten with the
// rest, but it is tried when a page first plays a video, not at launch (see
// VideoTrying()), and its count has a file of its own beside this one's.

#ifndef VENETIUM_W10M_BROWSER_VW_PRESENT_GUARD_H_
#define VENETIUM_W10M_BROWSER_VW_PRESENT_GUARD_H_

#include <stdint.h>

#include <string>

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
  // |made_of| is what the run is known by besides this DLL -- the package's
  // name, which carries the appx version (ANGLE's DLLs and the shell are
  // built apart from the engine's), and the engine switches: a file written
  // under another is dropped too.
  void Load(const base::FilePath& file,
            const std::string& made_of,
            bool forget);

  // Whether this run may try |kind| (kAngle or kD3D11), and how many runs
  // with it ended before a frame.
  bool Allowed(PresentKind kind);
  int Failures(PresentKind kind);

  // This run tries |kind|: counted against it until Worked(kind).
  void Trying(PresentKind kind);

  // Any thread: |kind| put a frame on screen. True the first time. Frames
  // through Direct3D 11 are no longer counted against from here. GPU
  // compositing is only known to have started: a driver that dies a moment
  // after its first frame would otherwise end every launch, with no way to
  // the menu. Its count goes when Proven() says the run went on.
  bool Worked(PresentKind kind);

  // Any thread: the run has gone on for a while with |kind| on screen, or
  // is being suspended with it there. Nothing unless Worked(kind) came first.
  void Proven(PresentKind kind);

  // Any thread: |kind| failed in this run. Unless it showed a frame first,
  // later runs leave it alone.
  void GaveUp(PresentKind kind);

  // Hardware video decoding (Chromium's D3D11VideoDecoder on ANGLE's
  // device), any thread. Whether this run may try it, and how many runs
  // ended while it was first at work.
  bool VideoAllowed();
  int VideoFailures();
  // The driver is about to be asked for it: the run counts against it from
  // here, until VideoWorked() -- frames have come out of a decoder for a few
  // seconds, and this run counts no more, whatever it tries -- or
  // VideoEnded() -- what was being tried is over and the run did not die of
  // it: the driver's answers are in, the video went, or the decoder failed
  // and the software one took over.
  void VideoTrying();
  void VideoWorked();
  void VideoEnded();
  // The app is suspended or resumed. A suspended run counts for nothing --
  // the phone ends suspended apps without another word -- whether the video
  // started before or after; one resumed with a decoder still untried
  // counts again.
  void VideoSuspended(bool suspended);

 private:
  int* Count(PresentKind kind);
  void Write();

  base::Lock lock_;
  base::FilePath file_;
  // BuildStamp() and Load's |made_of|, as written into the file.
  uint32_t stamp_ = 0;
  // Runs that ended between VideoTrying() and what follows it; this run is
  // among them while video_trying_. video_watched_ is the same as the GPU
  // side sees it, suspended or not.
  int video_ = 0;
  bool video_trying_ = false;
  bool video_watched_ = false;
  bool video_worked_ = false;
  bool video_suspended_ = false;
  int angle_ = 0;
  int d3d11_ = 0;
  bool angle_worked_ = false;
  bool d3d11_worked_ = false;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_PRESENT_GUARD_H_
