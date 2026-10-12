// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_browser_main_parts.h"

#include <windows.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/result_codes.h"
#include "venetium_w10m/browser/vw_browser.h"
#include "venetium_w10m/browser/vw_browser_context.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_frame_sink.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// venetium_w10m_quit reaches the main parts through this. UI thread.
VwBrowserMainParts* g_parts = nullptr;

// What a 1 ms sleep really takes once the 1 ms timer period is asked for:
// between 1 and 2 ms where winmm's timeBeginPeriod does its work, the
// system's own tick (as much as 15.6 ms) where it does not. Pool thread.
void NoteTimerGrain() {
  base::Time::ActivateHighResolutionTimer(true);
  ::Sleep(1);  // The new period starts with the next tick.
  constexpr int kSleeps = 8;
  const base::TimeTicks start = base::TimeTicks::Now();
  for (int i = 0; i < kSleeps; ++i) {
    ::Sleep(1);
  }
  const base::TimeDelta each = (base::TimeTicks::Now() - start) / kSleeps;
  const bool asked = base::Time::IsHighResolutionTimerInUse();
  base::Time::ActivateHighResolutionTimer(false);
  SYSTEM_POWER_STATUS power = {};
  const int ac = ::GetSystemPowerStatus(&power) ? power.ACLineStatus : -1;
  NoteF("timer: a 1 ms sleep takes %.2f ms (1 ms period %s; AC line "
        "status %d)",
        each.InMillisecondsF(), asked ? "asked for" : "NOT asked for", ac);
}

}  // namespace

VwBrowserMainParts::VwBrowserMainParts() {
  g_parts = this;
}

VwBrowserMainParts::~VwBrowserMainParts() {
  g_parts = nullptr;
}

int VwBrowserMainParts::PreEarlyInitialization() {
  // Before viz's thread exists: every root surface gets the shell's output.
  InstallFrameSink();
  return content::RESULT_CODE_NORMAL_EXIT;
}

int VwBrowserMainParts::PreMainMessageLoopRun() {
  NoteF("browser: main loop starting");
  // Timers to the millisecond on a battery too. base asks winmm for a 1 ms
  // timer period only on external power and for 8 ms on a battery
  // (base/time/time_win.cc), which is what a phone runs on -- and every
  // frame deadline in cc and viz is such a timer: 8 ms is half a frame.
  // BrowserMainLoop's HighResolutionTimerManager made its choice before this
  // call, from the power source at start-up; it would only choose again on a
  // power-change window message, and there are no window messages here, so
  // this holds for the whole run. It costs power only while a precise timer
  // is pending.
  base::Time::EnableHighResolutionTimer(true);
  // Measured a little later, when start-up no longer has every core busy.
  base::ThreadPool::PostDelayedTask(FROM_HERE, {base::MayBlock()},
                                    base::BindOnce(&NoteTimerGrain),
                                    base::Seconds(5));
  VwEngine* engine = VwEngine::Get();
  const EngineConfig config = engine->config();
  browser_context_ =
      std::make_unique<VwBrowserContext>(config.profile_dir, config.cache_dir);
  browser_ = std::make_unique<VwBrowser>(browser_context_.get(), config);

  // The first tab, before the shell's queued requests run: an "open this
  // URL" that arrived while the engine was starting then goes into a tab of
  // its own.
  const GURL first = config.initial_url.empty()
                         ? GURL("about:blank")
                         : VwBrowser::ResolveText(config.initial_url);
  browser_->CreateTab(engine->NextTabId(), first, true, 0);

  engine->BrowserStarted(browser_.get(),
                         base::SingleThreadTaskRunner::GetCurrentDefault());
  NoteF("browser: ready");
  return content::RESULT_CODE_NORMAL_EXIT;
}

void VwBrowserMainParts::WillRunMainMessageLoop(
    std::unique_ptr<base::RunLoop>& run_loop) {
  quit_ = run_loop->QuitWhenIdleClosure();
  if (quit_requested_) {
    std::move(quit_).Run();
  }
}

void VwBrowserMainParts::PostMainMessageLoopRun() {
  NoteF("browser: main loop ended");
  VwEngine::Get()->BrowserStopping();
  browser_.reset();
  browser_context_.reset();
}

void VwBrowserMainParts::Quit() {
  NoteF("browser: quitting");
  // Queued and later shell requests are dropped from here on, so none makes
  // a new tab.
  VwEngine::Get()->BrowserStopping();
  browser_.reset();
  if (quit_) {
    std::move(quit_).Run();
  } else {
    quit_requested_ = true;
  }
}

void QuitBrowserMainLoop() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (g_parts) {
    g_parts->Quit();
  }
}

}  // namespace venetium_w10m
