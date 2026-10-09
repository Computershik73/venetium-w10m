// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_engine.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "venetium_w10m/browser/vw_browser.h"

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
  for (auto& task : pending) {
    std::move(task).Run(browser);
  }
}

void VwEngine::BrowserStopping() {
  running_.store(false);
  g_browser = nullptr;
  base::AutoLock hold(lock_);
  ui_ = nullptr;
}

void VwEngine::SetSearchTemplate(std::string url_template) {
  base::AutoLock hold(lock_);
  search_template_ = std::move(url_template);
}

std::string VwEngine::search_template() {
  base::AutoLock hold(lock_);
  return search_template_;
}

void VwEngine::SetPhoneClipboard(std::string text) {
  base::AutoLock hold(lock_);
  phone_clipboard_ = std::move(text);
}

std::string VwEngine::phone_clipboard() {
  base::AutoLock hold(lock_);
  return phone_clipboard_;
}

}  // namespace venetium_w10m
