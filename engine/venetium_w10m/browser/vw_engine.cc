// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_engine.h"

#include <windows.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "venetium_w10m/browser/vw_browser.h"
#include "venetium_w10m/browser/vw_present_guard.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// Only ever touched on the browser UI thread.
VwBrowser* g_browser = nullptr;

}  // namespace

// static
VwEngine* VwEngine::Get() {
  static base::NoDestructor<VwEngine> engine;
  return engine.get();
}

VwEngine::VwEngine() = default;

void VwEngine::Configure(const EngineConfig& config) {
  base::AutoLock hold(lock_);
  const gfx::Size screen = config_.screen;
  const gfx::Size size = config_.size;
  config_ = config;
  if (config_.screen.IsEmpty()) {
    config_.screen = screen;
  }
  if (config_.size.IsEmpty()) {
    config_.size = size;
  }
}

EngineConfig VwEngine::config() {
  base::AutoLock hold(lock_);
  return config_;
}

void VwEngine::UpdateSize(const gfx::Size& size) {
  base::AutoLock hold(lock_);
  config_.size = size;
}

void VwEngine::UpdateScreen(const gfx::Size& screen) {
  base::AutoLock hold(lock_);
  config_.screen = screen;
}

void VwEngine::UpdateScale(double scale) {
  base::AutoLock hold(lock_);
  config_.scale = scale;
}

void VwEngine::SetEventSink(const venetium_w10m_events* sink) {
  base::AutoLock hold(lock_);
  events_ = sink ? *sink : venetium_w10m_events{};
}

venetium_w10m_events VwEngine::events() {
  base::AutoLock hold(lock_);
  return events_;
}

void VwEngine::PostToBrowser(BrowserTask task) {
  base::AutoLock hold(lock_);
  if (stopped_) {
    return;
  }
  if (!ui_) {
    pending_.push_back(std::move(task));
    return;
  }
  ui_->PostTask(FROM_HERE, base::BindOnce(&VwEngine::RunTask, std::move(task)));
}

// static
void VwEngine::RunTask(BrowserTask task) {
  if (g_browser) {
    std::move(task).Run(g_browser);
  }
}

void VwEngine::BrowserStarted(VwBrowser* browser,
                              scoped_refptr<base::SingleThreadTaskRunner> ui) {
  g_browser = browser;
  std::vector<BrowserTask> pending;
  {
    base::AutoLock hold(lock_);
    ui_ = std::move(ui);
    pending.swap(pending_);
  }
  running_.store(true);
  // Through RunTask: a queued quit ends the browser part way through these.
  for (auto& task : pending) {
    RunTask(std::move(task));
  }
}

void VwEngine::BrowserStopping() {
  stopping_.store(true);
  running_.store(false);
  g_browser = nullptr;
  std::vector<BrowserTask> dropped;
  base::AutoLock hold(lock_);
  stopped_ = true;
  ui_ = nullptr;
  dropped.swap(pending_);
}

void VwEngine::EngineExited() {
  stopping_.store(true);
  running_.store(false);
  std::vector<BrowserTask> dropped;
  base::AutoLock hold(lock_);
  stopped_ = true;
  ui_ = nullptr;
  dropped.swap(pending_);
}

void VwEngine::HardwarePresentWorked(PresentKind kind) {
  if (PresentGuard::Get()->Worked(kind)) {
    NoteF("gpu: first frame on screen through %s",
          kind == PresentKind::kAngle ? "GPU compositing (ANGLE)"
                                      : "Direct3D 11");
  }
}

void VwEngine::GpuCompositingFailed(const char* reason, bool for_good) {
  if (for_good) {
    PresentGuard::Get()->GaveUp(PresentKind::kAngle);
  }
  if (gpu_failed_.exchange(true)) {
    return;
  }
  NoteF("gpu: GPU compositing given up, software compositing from here: %s",
        reason ? reason : "no reason given");
}

void VwEngine::SetSearchTemplate(std::string url_template) {
  base::AutoLock hold(lock_);
  search_template_ = std::move(url_template);
}

std::string VwEngine::search_template() {
  base::AutoLock hold(lock_);
  return search_template_;
}


}  // namespace venetium_w10m
