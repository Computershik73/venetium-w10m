// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Which thread was busy, with what, when the phone stopped answering: a
// watched thread's tasks of half a second and more are said in the log when
// they end, with where they were posted from, and a task still running after
// three seconds is said while it runs (a watch thread looks every half
// second). Nothing is suspended or sampled: a task's place of posting is all
// it tells, which is often enough to know what to look at.

#ifndef VENETIUM_W10M_BROWSER_VW_HANG_WATCH_H_
#define VENETIUM_W10M_BROWSER_VW_HANG_WATCH_H_

namespace venetium_w10m::hang_watch {

// Watches the calling thread from now on, under |name| (a string that lives
// as long as the process). The thread must run tasks (base::CurrentThread);
// a second call for the same thread does nothing.
void WatchCurrentThread(const char* name);

}  // namespace venetium_w10m::hang_watch

#endif  // VENETIUM_W10M_BROWSER_VW_HANG_WATCH_H_
