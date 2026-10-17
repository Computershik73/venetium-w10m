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
#include "venetium_w10m/browser/vw_panel.h"
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
  // Frames go to the shell's SwapChainPanel through Direct3D 11 (otherwise
  // through frame_copy), with this many buffers.
  bool hardware_present = false;
  int present_buffers = 2;
  // The compositor itself runs on the GPU (ANGLE on Direct3D 11) and
  // presents into the panel; the two above are what it falls back to.
  bool gpu_compositing = false;
  // Viz draws the frames it can with Direct3D 11 itself, into the swap chain
  // software frames are presented through (the Direct3D 11 compositor, for
  // GPUs that stop at feature level 9_3). The frames it cannot draw are
  // software compositing's, presented as above.
  bool d3d_compositing = false;
  // The device is asked for at feature level 9_3 and no better
  // (--vw-gpu=d3d11: the compositor as a 9_3 phone runs it, on any phone).
  bool d3d_level_9_3 = false;
  // H.264 goes to the GPU's own decoder (Chromium's D3D11VideoDecoder, on
  // ANGLE's device); only with gpu_compositing, whose compositor takes the
  // decoded frames as textures.
  bool hardware_video = false;
  // A file that exists while a hardware run has not shown a frame yet.
  base::FilePath present_guard;
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
  // when it starts; tasks that arrive after it is gone, or after the engine
  // has ended, are dropped.
  using BrowserTask = base::OnceCallback<void(VwBrowser*)>;
  void PostToBrowser(BrowserTask task);

  // UI thread: the browser has started / is about to go.
  void BrowserStarted(VwBrowser* browser,
                      scoped_refptr<base::SingleThreadTaskRunner> ui);
  void BrowserStopping();
  bool browser_running() const { return running_.load(); }
  // UI thread: the browser between BrowserStarted and BrowserStopping, else
  // null.
  VwBrowser* browser();
  // From the start of shutdown on (ContentBrowserClient::IsShuttingDown).
  // Not "not running yet": during startup Chromium asks this before creating
  // the network service, and a yes there leaves it never created.
  bool shutting_down() const { return stopping_.load(); }

  // Any thread: venetium_w10m_run has returned, whether the browser ever
  // started or not.
  void EngineExited();

  // Any thread: |kind| (PresentKind::kD3D11, kAngle or kCompositor) put a
  // frame on screen; it is no longer counted against, at once or when the
  // run has gone on for a little (vw_present_guard.h).
  void HardwarePresentWorked(PresentKind kind);

  // Any thread: GPU compositing cannot go on in this run (GL did not start,
  // its surface could not be made, its context is lost); the browser
  // carries on with software compositing. Logged. |for_good| is proof that
  // it cannot work on this phone: later runs leave it alone, unless it had
  // shown a frame in this one. Otherwise the run only counts against it, as
  // one that died would.
  void GpuCompositingFailed(const char* reason, bool for_good);
  bool gpu_compositing_failed() const { return gpu_failed_.load(); }

  // Any thread: the Direct3D 11 compositor draws no more frames in this run;
  // software compositing draws them all, presented as before. Logged once.
  // |for_good| is proof that it cannot work on this phone, as above.
  // Otherwise the run is not counted against it: unlike a run that dies, it
  // has shown that the compositor can be tried without harm.
  void CompositorFailed(const char* reason, bool for_good);
  bool compositor_failed() const { return compositor_failed_.load(); }

  // Tab ids are handed out here so that venetium_w10m_tab_create can return
  // one at once, before the tab exists. Never reused within a run.
  int32_t NextTabId() { return next_tab_id_.fetch_add(1); }

  // Search template ("...%s...") and the desktop-site switch.
  void SetSearchTemplate(std::string url_template);
  std::string search_template();
  std::atomic<bool> desktop_mode{false};

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
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> gpu_failed_{false};
  std::atomic<bool> compositor_failed_{false};
  // No more tasks are taken (the browser is going or the engine has ended).
  bool stopped_ = false;  // under lock_
  std::atomic<int32_t> next_tab_id_{1};
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_ENGINE_H_
