// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Memory pressure, arranged as gecko-w10m has it
// (GeckoW10mMemoryPressure.cpp): what is known about memory comes in from
// wherever it is known, and one arbiter on the browser's UI thread decides
// what the engine is told to let go of, and how often.
//
// What runs out first in this one 32-bit process is address space -- its
// largest free piece, then all of it -- and the phone's own limit on the
// app's memory only on phones with 1 GB. The shell measures both every two
// seconds, in the walk its crash log's heartbeat makes anyway, and passes
// the numbers in (venetium_w10m_memory_status); the phone's own word
// (Windows.System.MemoryManager) comes at once when it changes
// (venetium_w10m_memory_pressure). From them, a level:
//
//   0  nothing is short
//   1  short: Chromium is told of moderate memory pressure -- caches cut to
//      half, the allocator's free pages given back, an incremental
//      collection of the JavaScript heaps
//   2  critical: the page of the background tab seen longest ago is
//      unloaded, one a step, then Chromium is told of critical pressure and
//      the JavaScript heaps are collected in full
//   3  about to fail: every background tab that can be unloaded is, at once
//
// A step is followed, a few seconds later, by a look at what it freed.
// Unloading goes on for as long as the level holds and there is a tab to
// unload; what frees nothing is asked for less and less often. An unloaded
// tab stays a tab, with its history, and loads its page again when it is
// selected (VwTab::Discard).

#ifndef VENETIUM_W10M_BROWSER_VW_MEMORY_H_
#define VENETIUM_W10M_BROWSER_VW_MEMORY_H_

#include "base/memory/memory_pressure_level.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"

namespace venetium_w10m {

class VwBrowser;

// What the shell measured, in megabytes.
struct MemorySample {
  int largest = 0;  // the largest free piece of the address space
  int free = 0;     // all of the address space that is free
  int app = 0;      // the app's memory use, as the phone counts it
  int limit = 0;    // what the phone allows the app (0: not known)
  base::TimeTicks taken;
};

// UI thread.
class VwMemory {
 public:
  explicit VwMemory(VwBrowser* browser);
  VwMemory(const VwMemory&) = delete;
  VwMemory& operator=(const VwMemory&) = delete;
  ~VwMemory();

  // The shell's measurements, every couple of seconds.
  void OnSample(const MemorySample& sample);
  // The phone's own word: 1 moderate, 2 critical.
  void OnPhoneLevel(int level);
  // Something gave way where an allocation would otherwise have ended the
  // process (|what|, a literal, says which): everything that can go, goes.
  void OnAlarm(const char* what);
  // Suspended: the phone ends suspended apps as it needs their memory, so
  // what can be let go of without unloading anything is, as gecko-w10m does
  // on its way out. Resumed: Chromium's caches may grow again.
  void OnAppState(bool suspended);

 private:
  void WatchReserve(base::TimeTicks now);
  bool RaiseAlarm(base::TimeTicks now, const char* what);
  // Works out the level and does what is due at it. |fresh|: the shell's
  // numbers are of this moment.
  void Evaluate(base::TimeTicks now, bool fresh);
  bool MaybeStep(base::TimeTicks now);
  void Step(base::TimeTicks now, int unload, base::TimeDelta spare);
  void TrimHard(int step);
  void OnCollected(int step, int milliseconds);
  void Settled(int step, int collect_ms);
  base::TimeTicks JudgeAt() const;
  void Judge(base::TimeTicks now);
  void MaybeEnd(base::TimeTicks now);
  void Tell(base::MemoryPressureLevel level);
  void NoteLevel(int was);
  // The clocks that ration trimming and unloading go on from where they
  // stood when the app was suspended.
  void Resumed(base::TimeTicks now);

  raw_ptr<VwBrowser> browser_;
  // False with --vw-discard=off among the shell's switches: caches are
  // trimmed, tabs stay loaded.
  bool unload_ = true;

  // The inputs.
  MemorySample last_;
  bool measured_ = false;
  int largest_level_ = 0;
  int free_level_ = 0;
  int address_level_ = 0;
  int commit_level_ = 0;
  int phone_level_ = 0;
  base::TimeTicks phone_until_;
  const char* alarm_ = "";
  bool alarm_live_ = false;
  base::TimeTicks alarm_at_;
  bool reserve_seen_ = false;
  bool reserve_used_ = false;

  // The level they add up to, and which of them it comes from.
  int level_ = 0;
  const char* cause_ = "";

  // The step under way, until it is judged.
  bool stepping_ = false;
  int step_ = 0;
  int step_level_ = 0;
  int step_unloaded_ = 0;
  int step_collect_ms_ = -1;
  bool settled_ = false;
  base::TimeTicks step_at_;
  base::TimeTicks settled_at_;
  MemorySample before_;

  // When the next may start: any step; one that only trims hard; one that
  // only trims. The two gaps grow while trimming frees nothing.
  base::TimeTicks last_step_at_;
  base::TimeTicks hard_due_;
  base::TimeTicks soft_due_;
  // How long the last full collection took (-1: none yet), and when the
  // next step that unloads below level 3 may start for it.
  int last_collect_ms_ = -1;
  base::TimeTicks unload_due_;
  base::TimeDelta hard_gap_;
  base::TimeDelta soft_gap_;

  // What Chromium was last told, and since when nothing has been short.
  base::MemoryPressureLevel told_ = base::MEMORY_PRESSURE_LEVEL_NONE;
  base::TimeTicks calm_since_;
  // When the app was last told it is suspended; null once it is back.
  base::TimeTicks suspended_at_;

  base::WeakPtrFactory<VwMemory> weak_factory_{this};
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_MEMORY_H_
