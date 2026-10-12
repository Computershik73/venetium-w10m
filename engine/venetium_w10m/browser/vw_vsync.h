// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The display's own beat, for the compositor to keep time with.
//
// Without a window Chromium has nothing to ask about vertical sync: viz
// falls back on a 60 Hz timer that starts whenever it starts and drifts
// against the panel, while Present waits for the real thing. Frames then
// land a little early or a little late in turn, and a scroll that should
// move the same distance every refresh stutters. A thread here waits on the
// adapter's first output for each vertical blank (as gecko-w10m does) and
// tells the compositor when the last one was and how far apart they are;
// viz's timer then ticks in step with the display.

#ifndef VENETIUM_W10M_BROWSER_VW_VSYNC_H_
#define VENETIUM_W10M_BROWSER_VW_VSYNC_H_

#include <atomic>
#include <memory>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/simple_thread.h"
#include "base/time/time.h"

namespace venetium_w10m {

class VwVsync : public base::DelegateSimpleThread::Delegate {
 public:
  // |report| runs on the creating thread: the time of a recent vertical
  // blank and the interval between two.
  using Report =
      base::RepeatingCallback<void(base::TimeTicks, base::TimeDelta)>;

  explicit VwVsync(Report report);
  VwVsync(const VwVsync&) = delete;
  VwVsync& operator=(const VwVsync&) = delete;
  ~VwVsync() override;

  // While the app is suspended the display may be off: no waiting then.
  void SetActive(bool active) { active_.store(active); }

 private:
  // base::DelegateSimpleThread::Delegate:
  void Run() override;

  Report report_;
  scoped_refptr<base::SingleThreadTaskRunner> origin_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> active_{true};
  std::unique_ptr<base::DelegateSimpleThread> thread_;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_VSYNC_H_
