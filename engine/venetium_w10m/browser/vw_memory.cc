// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_memory.h"

#include <algorithm>
#include <string>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/task/single_thread_task_runner.h"
#include "partition_alloc/page_allocator.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"
#include "venetium_w10m/browser/vw_browser.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_frame_sink.h"
#include "venetium_w10m/common/vw_log.h"
#include "venetium_w10m/renderer/vw_renderer_client.h"

namespace venetium_w10m {

namespace {

// The marks of the address space, megabytes: its largest free piece, and
// all of it that is free. A level is entered below its mark and left only
// at one and a half times it, as gecko-w10m leaves its one mark: low under
// 96 MB of largest piece, back above 144 (GeckoW10mMemoryPressure.cpp).
// With 27 MB the largest piece, gecko-w10m saw a swap chain refuse to
// resize: three buffers of 14 MB. The whole has marks of its own, which
// gecko-w10m does without, because most of what Chromium takes it takes in
// small pieces -- 2 MB of allocator pages, 1 MB of shared memory to a tile
// or a decoded image -- and the largest piece can stand still while they
// go: 128 MB of largest piece from 459 MB free down to 245 in one log
// (222.17), in which a page being loaded took 131 MB between two looks two
// seconds apart.
struct Marks {
  int largest;
  int free;
};
constexpr Marks kMarks[] = {{0, 0}, {96, 256}, {64, 160}, {32, 80}};

// Each of the two has a level of its own, left at one and a half times its
// own mark only: the largest piece stood at 124-128 MB for the whole of the
// 222.17 log, with 558 MB free as with 245, and is not what ran short there.
int MarkLevel(int value, int Marks::* mark, int held) {
  for (int level = 3; level >= 1; --level) {
    const int k = held >= level ? 3 : 2;  // halves: left at 1.5 times
    if (value * 2 < kMarks[level].*mark * k) {
      return level;
    }
  }
  return 0;
}

// The app's memory against what the phone allows it, by the room left: a
// quarter of the limit and a tenth of it -- where the phone itself says
// High, at nine tenths -- but never less than 128 and 64 MB, since a page
// being loaded took 60 MB between two looks (the same log) and a phone with
// 1 GB allows the app 390 MB in all (gecko-w10m's Lumia 650): there the
// levels start at 262 and 326 MB, on a phone that allows 2048 at 1536 and
// 1844. Level 3 is the last twentieth before the limit (32 MB at least):
// at the limit itself the phone ends the app, as it did gecko-w10m's on a
// Lumia 650.
int CommitLevel(const MemorySample& sample, int held) {
  if (sample.limit <= 0) {
    return 0;
  }
  const int room = sample.limit - sample.app;
  const int last = std::max(sample.limit / 20, 32);
  if (room <= 0 || room * 2 < last * (held >= 3 ? 3 : 2)) {
    return 3;
  }
  const int critical = std::max(sample.limit / 10, 64);
  if (room * 2 < critical * (held >= 2 ? 3 : 2)) {
    return 2;
  }
  const int low = std::max(sample.limit / 4, 128);
  if (room * 2 < low * (held >= 1 ? 3 : 2)) {
    return 1;
  }
  return 0;
}

// A measurement that waited this long behind something else on the UI thread
// says nothing of now; the next is two seconds behind it.
constexpr base::TimeDelta kStale = base::Seconds(5);
// From one step to the next, and from a step to the look at what it freed:
// three of the shell's measurements. What an unloaded page held comes back
// only when the renderer has taken it apart and collected, which is not at
// once: the look is also at least kSettle after the collection has ended,
// and given up on kStepTimeout after the step if it never reports.
constexpr base::TimeDelta kStepGap = base::Seconds(6);
constexpr base::TimeDelta kSettle = base::Seconds(2);
constexpr base::TimeDelta kStepTimeout = base::Seconds(20);
// From destroying a tab's WebContents to asking for the collection: its
// frames are detached by tasks on the renderer's thread, and only then is
// the page garbage.
constexpr base::TimeDelta kTeardown = base::Seconds(1);
// Steps that only trim are rationed, as gecko-w10m rations its own: every
// 60 s at level 1 and every 30 s above it, twice as long each time one
// frees nothing, to five minutes; and a full collection never sooner than
// nine times what the last one took, a tenth of the renderer's time at
// most. (A full collection every 30 s whatever it freed cost gecko-w10m
// 51 s of five minutes on a Lumia 650.)
constexpr base::TimeDelta kSoftGap = base::Seconds(60);
constexpr base::TimeDelta kHardGap = base::Seconds(30);
constexpr base::TimeDelta kMaxGap = base::Seconds(300);
constexpr int kCollectionDuty = 9;
// A full collection follows every step that unloads, and stops the page on
// screen for as long as it takes: under half a second on an Elite x3, twelve
// on a Lumia 650 by gecko-w10m's notes. Where it takes this long the tabs
// that can go all go in one step, for one collection, and the next such
// step waits out the same duty as trimming does.
constexpr int kSlowCollectionMs = 3000;
// A step freed something when the free address space or the app's memory
// moved by this much, or the largest free piece by half of it.
constexpr int kGainMb = 16;
// Chromium is told the pressure is over only after this long of nothing
// being short: the phone's levels have no slack in them, and every end
// would make the next beginning a new one.
constexpr base::TimeDelta kCalm = base::Seconds(30);
// The phone's word holds until the shell's next measurements have had time
// to arrive and say the same, or not.
constexpr base::TimeDelta kPhoneHold = base::Seconds(5);
// The phone freezes a suspended app within seconds of saying so (the shell
// holds it for a second and a half): a measurement later than this was made
// after the app came back.
constexpr base::TimeDelta kFrozenAfter = base::Seconds(10);
// An alarm is level 3 for the one step that answers it, and the next alarm
// is heard only this long after: a heap at its limit calls again at every
// collection.
constexpr base::TimeDelta kAlarmGap = base::Seconds(30);
// A tab that has only just left the screen is left alone: the person may be
// flipping between two, and what was typed into its forms reaches the
// browser's copy of the page's state within 5 s of its being hidden
// (content/renderer/render_frame_impl.cc).
constexpr base::TimeDelta kSpare = base::Seconds(10);
constexpr base::TimeDelta kSpareUrgent = base::Seconds(6);
// PartitionAlloc's last resort, which Blink sets aside at start
// (blink_initializer.cc): given up to the first allocation or mapping that
// fails. It is set aside again only where that leaves a largest piece that
// is not itself a reason for pressure.
constexpr int kReserveMb = 96;
constexpr int kReserveLeaveMb = 144;

std::string Numbers(const MemorySample& sample) {
  return absl::StrFormat(
      "largest free piece %d MB, free %d MB, app %d of %d MB", sample.largest,
      sample.free, sample.app, sample.limit);
}

}  // namespace

VwMemory::VwMemory(VwBrowser* browser)
    : browser_(browser), hard_gap_(kHardGap), soft_gap_(kSoftGap) {
  unload_ = VwEngine::Get()->config().switches.find("--vw-discard=off") ==
            std::string::npos;
  if (!unload_) {
    Note("memory: background tabs stay loaded whatever is short "
         "(--vw-discard=off)");
  }
}

VwMemory::~VwMemory() = default;

void VwMemory::OnSample(const MemorySample& sample) {
  const base::TimeTicks now = base::TimeTicks::Now();
  if (now - sample.taken > kStale) {
    return;
  }
  if (!suspended_at_.is_null() && now - suspended_at_ > kFrozenAfter) {
    // The shell's measuring thread was ahead of its word that the app is
    // back.
    Resumed(now);
  }
  last_ = sample;
  measured_ = true;
  largest_level_ =
      MarkLevel(sample.largest, &Marks::largest, largest_level_);
  free_level_ = MarkLevel(sample.free, &Marks::free, free_level_);
  address_level_ = std::max(largest_level_, free_level_);
  commit_level_ = CommitLevel(sample, commit_level_);
  // For the JavaScript heap's limit (vw_renderer_client.cc): it is raised
  // only while there is a tab to unload for it.
  SetUnloadableTabs(unload_ ? browser_->UnloadableTabs(kSpareUrgent) : 0);
  WatchReserve(now);
  Evaluate(now, /*fresh=*/true);
}

void VwMemory::OnPhoneLevel(int level) {
  const base::TimeTicks now = base::TimeTicks::Now();
  phone_level_ = std::clamp(level, 0, 2);
  phone_until_ = now + kPhoneHold;
  Evaluate(now, /*fresh=*/false);
}

void VwMemory::OnAlarm(const char* what) {
  const base::TimeTicks now = base::TimeTicks::Now();
  if (RaiseAlarm(now, what)) {
    Evaluate(now, /*fresh=*/false);
  }
}

void VwMemory::OnAppState(bool suspended) {
  if (!suspended) {
    Resumed(base::TimeTicks::Now());
    // Whatever is short now, the next measurements say; until then
    // Chromium's caches are not held at nothing for a trim made in passing
    // (it keeps the level it was last told).
    if (!stepping_ && level_ == 0 &&
        told_ != base::MEMORY_PRESSURE_LEVEL_NONE) {
      Tell(base::MEMORY_PRESSURE_LEVEL_NONE);
    } else if (!stepping_ && level_ == 1 &&
               told_ == base::MEMORY_PRESSURE_LEVEL_CRITICAL) {
      Tell(base::MEMORY_PRESSURE_LEVEL_MODERATE);
    }
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  suspended_at_ = now;
  if (stepping_ || now < hard_due_) {
    return;  // a trim is under way, or was made a moment ago
  }
  Note("memory: trimming for the suspension");
  Tell(base::MEMORY_PRESSURE_LEVEL_CRITICAL);
  CollectRendererGarbage(base::BindOnce(&VwMemory::OnCollected,
                                        weak_factory_.GetWeakPtr(), step_));
  hard_due_ = now + hard_gap_;
  soft_due_ = now + soft_gap_;
}

void VwMemory::Resumed(base::TimeTicks now) {
  if (suspended_at_.is_null()) {
    return;
  }
  // Nothing was taken and nothing could be given back while the app stood
  // frozen. Otherwise the first measurements after a long suspension find a
  // trim due and make it as the person comes back, on top of the one made on
  // the way out.
  const base::TimeDelta away = now - suspended_at_;
  suspended_at_ = base::TimeTicks();
  if (!hard_due_.is_null()) {
    hard_due_ += away;
  }
  if (!soft_due_.is_null()) {
    soft_due_ += away;
  }
  if (!unload_due_.is_null()) {
    unload_due_ += away;
  }
}

void VwMemory::WatchReserve(base::TimeTicks now) {
  if (partition_alloc::HasReservationForTesting()) {
    reserve_seen_ = true;
    reserve_used_ = false;
    return;
  }
  if (!reserve_seen_) {
    return;  // Blink has not set it aside yet
  }
  if (!reserve_used_) {
    reserve_used_ = true;
    if (std::max(address_level_, commit_level_) > 0) {
      // An allocation of pages, a mapping of shared memory or V8 has failed
      // once and was given the reserve; the next to fail ends the process.
      RaiseAlarm(now, "an allocation failed and took the reserve");
    } else {
      // Nothing is short by this very measurement, which has the reserve in
      // it: what failed asked for more than there is in one piece -- a
      // page's oversized buffer, say -- and no tab's page stood in its way.
      NoteF("memory: an allocation failed and took the reserve with nothing "
            "short (%s): it asked for more than one piece holds",
            Numbers(last_));
    }
    return;
  }
  if (stepping_ || address_level_ > 0) {
    return;
  }
  // Not so much that setting it aside is itself what makes memory short.
  const int size = std::min({kReserveMb,
                             (last_.largest - kReserveLeaveMb) / 16 * 16,
                             (last_.free - kMarks[1].free * 3 / 2) / 16 * 16});
  if (size >= 32 && partition_alloc::ReserveAddressSpace(
                        static_cast<size_t>(size) * 1024 * 1024)) {
    reserve_used_ = false;
    NoteF("memory: %d MB of address space set aside again as the last "
          "resort",
          size);
  }
}

bool VwMemory::RaiseAlarm(base::TimeTicks now, const char* what) {
  // Heard again this soon only when there is a tab to unload for it.
  if (!alarm_at_.is_null() && now - alarm_at_ < kAlarmGap &&
      !(unload_ && browser_->UnloadableTabs(kSpareUrgent) > 0)) {
    return false;
  }
  alarm_at_ = now;
  alarm_ = what;
  alarm_live_ = true;
  // The step under way is not waited for, and no clock holds this one back.
  if (stepping_) {
    stepping_ = false;
    ++step_;
  }
  last_step_at_ = base::TimeTicks();
  hard_due_ = base::TimeTicks();
  return true;
}

void VwMemory::Evaluate(base::TimeTicks now, bool fresh) {
  int level = 0;
  const char* cause = "";
  if (measured_) {
    level = std::max(address_level_, commit_level_);
    cause = address_level_ >= commit_level_ ? "address space"
                                            : "the phone's limit";
  }
  if (now < phone_until_ && phone_level_ > level) {
    level = phone_level_;
    cause = "the phone says so";
  }
  if (alarm_live_) {
    level = 3;
    cause = alarm_;
  }
  const int was = level_;
  level_ = level;
  cause_ = cause;

  if (level >= 3 && was < 3) {
    // About to fail: the gap from the last step is not waited out.
    last_step_at_ = base::TimeTicks();
  }
  if (stepping_ && level >= 2 && level > step_level_ &&
      ((step_level_ < 2 && now >= hard_due_) ||
       (unload_ &&
        browser_->UnloadableTabs(level >= 3 ? kSpareUrgent : kSpare) > 0))) {
    // Worse than what the step under way was for, and there is a tab to
    // unload, or that step only trimmed and a hard trim is due: it is not
    // waited for. (Its own trim, if still to come, is made by the one that
    // takes its place.)
    stepping_ = false;
    ++step_;
    last_step_at_ = base::TimeTicks();
  }
  if (stepping_) {
    if (!fresh || now < JudgeAt()) {
      if (level != was) {
        NoteLevel(was);
      }
      return;
    }
    Judge(now);
  }
  bool stepped = false;
  if (level_ > 0) {
    calm_since_ = base::TimeTicks();
    stepped = MaybeStep(now);
  } else {
    MaybeEnd(now);
  }
  if (!stepped && level != was) {
    NoteLevel(was);
  }
}

bool VwMemory::MaybeStep(base::TimeTicks now) {
  if (!last_step_at_.is_null() && now - last_step_at_ < kStepGap) {
    return false;
  }
  if (level_ >= 2) {
    const base::TimeDelta spare = level_ >= 3 ? kSpareUrgent : kSpare;
    // Unloading is never held back: while the level holds, the next tab
    // goes as soon as the last step has been looked at.
    const int ready = unload_ ? browser_->UnloadableTabs(spare) : 0;
    if (ready > 0 && (level_ >= 3 || now >= unload_due_)) {
      const bool slow = last_collect_ms_ >= kSlowCollectionMs;
      Step(now, level_ >= 3 || slow ? ready : 1, spare);
      return true;
    }
    if (now >= hard_due_) {
      Step(now, 0, spare);
      return true;
    }
    return false;
  }
  if (now >= soft_due_) {
    Step(now, 0, base::TimeDelta());
    return true;
  }
  return false;
}

void VwMemory::Step(base::TimeTicks now, int unload, base::TimeDelta spare) {
  stepping_ = true;
  ++step_;
  settled_ = false;
  step_at_ = now;
  last_step_at_ = now;
  step_level_ = level_;
  step_unloaded_ = 0;
  step_collect_ms_ = -1;
  before_ = last_;
  // This step is the answer to an alarm, if there was one; after it the
  // level is what is measured again.
  alarm_live_ = false;
  const bool hard = level_ >= 2;
  // Until the look at what this step freed sets them again: a step that
  // takes this one's place before then does not trim a second time.
  soft_due_ = now + soft_gap_;
  if (hard) {
    hard_due_ = now + hard_gap_;
  }
  const std::string doing =
      unload > 1    ? absl::StrFormat("unloading %d background tabs", unload)
      : unload == 1 ? std::string("unloading a background tab")
      : hard        ? std::string("trimming hard")
                    : std::string("trimming");
  NoteF("memory: pressure %d (%s): %s; %s", level_, cause_,
        measured_ ? Numbers(last_) : std::string("nothing measured yet"),
        doing);
  if (!hard) {
    Tell(base::MEMORY_PRESSURE_LEVEL_MODERATE);
    Settled(step_, -1);
    return;
  }
  if (unload > 0) {
    step_unloaded_ = browser_->UnloadBackgroundTabs(unload, spare, cause_);
    SetUnloadableTabs(browser_->UnloadableTabs(kSpareUrgent));
  } else if (unload_) {
    browser_->NoteKeptTabs(spare);
  }
  if (step_unloaded_ > 0) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&VwMemory::TrimHard, weak_factory_.GetWeakPtr(), step_),
        kTeardown);
  } else {
    TrimHard(step_);
  }
}

void VwMemory::TrimHard(int step) {
  if (step != step_ || !stepping_) {
    return;
  }
  // The Direct3D 11 compositor's textures are copies of tiles viz holds
  // anyway: they go at every hard trim, and are made again from the tiles
  // when next drawn.
  ReleaseGraphicsMemory(/*trim=*/false);
  Tell(base::MEMORY_PRESSURE_LEVEL_CRITICAL);
  // Chromium's own answer to critical pressure, for a renderer with a page
  // on screen, is only the start of an incremental collection
  // (content/renderer/blink_isolates_pressure_listener.cc); here the one
  // renderer has a page on screen for as long as the app runs.
  if (!CollectRendererGarbage(base::BindOnce(
          &VwMemory::OnCollected, weak_factory_.GetWeakPtr(), step))) {
    Settled(step, -1);
  }
}

void VwMemory::OnCollected(int step, int milliseconds) {
  NoteF("memory: trimmed; JavaScript collected in %d ms", milliseconds);
  Settled(step, milliseconds);
}

void VwMemory::Settled(int step, int collect_ms) {
  if (step != step_ || !stepping_) {
    return;
  }
  settled_ = true;
  settled_at_ = base::TimeTicks::Now();
  step_collect_ms_ = collect_ms;
  if (collect_ms >= 0) {
    last_collect_ms_ = collect_ms;
    unload_due_ =
        collect_ms >= kSlowCollectionMs
            ? settled_at_ + std::min(base::Milliseconds(collect_ms) *
                                         kCollectionDuty,
                                     kMaxGap)
            : base::TimeTicks();
  }
}

base::TimeTicks VwMemory::JudgeAt() const {
  if (!settled_) {
    return step_at_ + kStepTimeout;
  }
  return std::max(step_at_ + kStepGap, settled_at_ + kSettle);
}

void VwMemory::Judge(base::TimeTicks now) {
  stepping_ = false;
  const int largest = last_.largest - before_.largest;
  const int free = last_.free - before_.free;
  const int app = last_.app - before_.app;
  const bool gain =
      free >= kGainMb || largest * 2 >= kGainMb || app <= -kGainMb;
  std::string next;
  if (step_level_ >= 2) {
    hard_gap_ = (step_unloaded_ > 0 || gain) ? kHardGap
                                             : std::min(hard_gap_ * 2, kMaxGap);
    base::TimeDelta wait = hard_gap_;
    if (step_collect_ms_ > 0) {
      wait = std::max(
          wait, base::Milliseconds(step_collect_ms_) * kCollectionDuty);
    }
    wait = std::min(wait, kMaxGap);
    hard_due_ = now + wait;
    soft_due_ = now + soft_gap_;
    if (step_unloaded_ == 0) {
      next = absl::StrFormat("; no trimming alone for %d s",
                             static_cast<int>(wait.InSeconds()));
    }
  } else {
    soft_gap_ = gain ? kSoftGap : std::min(soft_gap_ * 2, kMaxGap);
    soft_due_ = now + soft_gap_;
    next = absl::StrFormat("; no trimming for %d s",
                           static_cast<int>(soft_gap_.InSeconds()));
  }
  NoteF("memory: %d s later: largest free piece %d MB (%+d), free %d MB "
        "(%+d), app %d MB (%+d)%s%s",
        static_cast<int>((now - step_at_).InSeconds()), last_.largest, largest,
        last_.free, free, last_.app, app,
        settled_ ? "" : "; the renderer has not answered", next);
}

void VwMemory::MaybeEnd(base::TimeTicks now) {
  if (told_ == base::MEMORY_PRESSURE_LEVEL_NONE) {
    return;
  }
  if (calm_since_.is_null()) {
    calm_since_ = now;
  }
  if (now - calm_since_ < kCalm) {
    return;
  }
  // Chromium keeps the level it was last told -- every cache at the size
  // that level allows, whatever is made later starting out under it -- so
  // the end is told as well.
  Tell(base::MEMORY_PRESSURE_LEVEL_NONE);
  hard_gap_ = kHardGap;
  soft_gap_ = kSoftGap;
  hard_due_ = std::min(hard_due_, now + kHardGap);
  soft_due_ = std::min(soft_due_, now + kSoftGap);
  Note("memory: nothing short for 30 s; Chromium is told the pressure is "
       "over");
}

void VwMemory::Tell(base::MemoryPressureLevel level) {
  told_ = level;
  base::MemoryPressureListener::NotifyMemoryPressure(level);
}

void VwMemory::NoteLevel(int was) {
  NoteF("memory: pressure %d (was %d%s%s): %s", level_, was,
        level_ ? "; " : "", level_ ? cause_ : "",
        measured_ ? Numbers(last_) : std::string("nothing measured yet"));
}

}  // namespace venetium_w10m
