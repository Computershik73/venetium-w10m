// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Which thread was busy, with what, when the phone stopped answering: a
// watched thread's tasks of half a second and more are said in the log when
// they end, with where they were posted from, and a task still running after
// three seconds is said while it runs (a watch thread looks every half
// second). A task's place of posting is often enough to know what to look
// at; for the renderer thread, where pages' scripts run, its program counter
// is also sampled (SampleCurrentThread).

// All of it only with --vw-diagnostics among the engine switches (the
// sampler: --vw-sample): every line is a write to the phone's flash.

#ifndef VENETIUM_W10M_BROWSER_VW_HANG_WATCH_H_
#define VENETIUM_W10M_BROWSER_VW_HANG_WATCH_H_

namespace venetium_w10m::hang_watch {

// Watches the calling thread from now on, under |name| (a string that lives
// as long as the process). The thread must run tasks (base::CurrentThread);
// a second call for the same thread does nothing.
void WatchCurrentThread(const char* name);

// From now on, every thread Chromium names (base::PlatformThread::SetName) is
// counted, and every 10 s the log says which used the processor most: the
// share of one core each thread, or each kind of thread, took. Called once,
// before the browser starts its threads.
void StartCpuAccounting();

// With --vw-sample: from now on, the calling thread (watched already:
// WatchCurrentThread) is sampled while it runs a task, some 30 to 100 times a
// second: it is
// suspended for as long as reading its program counter takes. Every 10 s the
// log says where its time went (code V8 compiled, V8's builtins and
// interpreter, the engine's own code, each system DLL), and every minute the
// engine's hottest code since the start, as engine+offset addresses for
// llvm-symbolizer --relative-address; the whole count since the start, in
// 16-byte pieces of the engine, goes to renderer-profile.txt beside the log
// (LocalState) every minute, for tools/hot-functions.py.
void SampleCurrentThread();

}  // namespace venetium_w10m::hang_watch

#endif  // VENETIUM_W10M_BROWSER_VW_HANG_WATCH_H_
