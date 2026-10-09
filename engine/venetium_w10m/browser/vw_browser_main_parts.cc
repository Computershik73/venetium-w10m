// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_browser_main_parts.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/result_codes.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_non_backed.h"
#include "venetium_w10m/browser/vw_browser.h"
#include "venetium_w10m/browser/vw_browser_context.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_frame_sink.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// venetium_w10m_quit ends the main loop through this.
base::OnceClosure* g_quit = nullptr;

}  // namespace

VwBrowserMainParts::VwBrowserMainParts() = default;

VwBrowserMainParts::~VwBrowserMainParts() = default;

int VwBrowserMainParts::PreEarlyInitialization() {
  // Before viz's thread exists: every root surface gets the shell's output.
  InstallFrameSink();
  return content::RESULT_CODE_NORMAL_EXIT;
}

int VwBrowserMainParts::PreMainMessageLoopRun() {
  NoteF("browser: main loop starting");
  // The desktop clipboard is a window's (OpenClipboard needs an HWND);
  // this one lives in memory and the shell mirrors the phone's into it.
  ui::Clipboard::SetClipboardForCurrentThread(
      std::make_unique<ui::ClipboardNonBacked>());

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
  g_quit = new base::OnceClosure(run_loop->QuitWhenIdleClosure());
}

void VwBrowserMainParts::PostMainMessageLoopRun() {
  NoteF("browser: main loop ended");
  VwEngine::Get()->BrowserStopping();
  browser_.reset();
  browser_context_.reset();
  delete g_quit;
  g_quit = nullptr;
}

void QuitBrowserMainLoop() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (g_quit && *g_quit) {
    std::move(*g_quit).Run();
  }
}

}  // namespace venetium_w10m
