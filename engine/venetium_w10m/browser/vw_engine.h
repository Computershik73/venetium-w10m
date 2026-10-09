// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The engine as the C boundary sees it: the configuration the shell started
// it with, the shell's event sink, and a way to get work onto Chromium's
// browser UI thread from the XAML thread -- queued until that thread exists.
// A few answers the shell needs synchronously (is a text field focused, will
// the engine take the back button) are kept here as atomics, written on the
// UI thread and read from any.

#ifndef VENETIUM_W10M_BROWSER_VW_ENGINE_H_
#define VENETIUM_W10M_BROWSER_VW_ENGINE_H_

#include <atomic>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/gfx/geometry/size.h"
#include "venetium_w10m/capi/venetium_w10m_capi.h"

namespace venetium_w10m {

class VwBrowser;

struct EngineConfig {
  base::FilePath install_dir;
  base::FilePath profile_dir;
  base::FilePath cache_dir;
  base::FilePath temp_dir;
  gfx::Size size;            // content area, device pixels
  gfx::Size screen;          // whole display, device pixels
  double scale = 1.0;        // device pixels per DIP
  bool jit = false;
  std::string switches;
  std::string initial_url;
};

class VwEngine {
 public:
  // Made on first use, from any thread, and never destroyed (the process
  // ends first): the shell may report the screen before it starts the engine.
  static VwEngine* Get();

  // venetium_w10m_run's configuration. A screen size the shell reported
  // before the run is kept when the configuration has none.
  void Configure(const EngineConfig& config);

  // The configuration; geometry in it is kept current until the browser
  // starts, and the browser reads it once.
  EngineConfig config();
  void UpdateSize(const gfx::Size& size);
  void UpdateScreen(const gfx::Size& screen);
  void UpdateScale(double scale);

  // The shell's callbacks, copied. Missing members are null.
  void SetEventSink(const venetium_w10m_events* sink);
  venetium_w10m_events events();

  // Runs |task| on the browser UI thread with the browser, as soon as there
  // is one. Any thread. Tasks posted before the browser exists run in order
  // when it starts; tasks that arrive after it is gone are dropped.
  using BrowserTask = base::OnceCallback<void(VwBrowser*)>;
  void PostToBrowser(BrowserTask task);

  // UI thread: the browser has started / is about to go.
  void BrowserStarted(VwBrowser* browser,
                      scoped_refptr<base::SingleThreadTaskRunner> ui);
  void BrowserStopping();
  bool browser_running() const { return running_.load(); }

  // Tab ids are handed out here so that venetium_w10m_tab_create can return
  // one at once, before the tab exists. Never reused within a run.
  int32_t NextTabId() { return next_tab_id_.fetch_add(1); }

  // Search template ("...%s...") and the desktop-site switch.
  void SetSearchTemplate(std::string url_template);
  std::string search_template();
  std::atomic<bool> desktop_mode{false};

  // The phone's clipboard as the shell last saw it (for a paste).
  void SetPhoneClipboard(std::string text);
  std::string phone_clipboard();

  // Read synchronously by the shell; written on the UI thread.
  std::atomic<int32_t> text_input_wanted{0};
  std::atomic<uint32_t> text_input_state{0};
  std::atomic<bool> fullscreen{false};
  std::atomic<bool> selected_can_go_back{false};
  std::atomic<int32_t> selected_tab{0};

 private:
  VwEngine();
  friend class base::NoDestructor<VwEngine>;

  static void RunTask(BrowserTask task);

  base::Lock lock_;
  EngineConfig config_;
  venetium_w10m_events events_{};
  std::vector<BrowserTask> pending_;
  scoped_refptr<base::SingleThreadTaskRunner> ui_;
  std::string search_template_ = "https://www.google.com/search?q=%s";
  std::string phone_clipboard_;
  std::atomic<bool> running_{false};
  std::atomic<int32_t> next_tab_id_{1};
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_ENGINE_H_
