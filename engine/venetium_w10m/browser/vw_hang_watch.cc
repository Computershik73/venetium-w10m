// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_hang_watch.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/pending_task.h"
#include "base/synchronization/lock.h"
#include "base/task/current_thread.h"
#include "base/task/task_observer.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/system/sys_info.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_id_name_manager.h"
#include "base/time/time.h"
#include "venetium_w10m/common/vw_log.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace venetium_w10m::hang_watch {
namespace {

// Tasks this long are said when they end; tasks running this long, while
// they run.
constexpr int64_t kLongTaskUs = 500'000;
constexpr int64_t kStuckUs = 3'000'000;
// At most one line a thread per this, the others counted.
constexpr int64_t kNoteEveryUs = 1'000'000;
constexpr int kMaxThreads = 8;

int64_t NowUs() {
  return (base::TimeTicks::Now() - base::TimeTicks()).InMicroseconds();
}

struct Watched {
  const char* name = nullptr;
  // When the task the thread is in began; 0 between tasks.
  std::atomic<int64_t> started_us{0};
  // Where that task was posted from (static strings, or null).
  std::atomic<const char*> function{nullptr};
  std::atomic<const char*> file{nullptr};
  std::atomic<int> line{0};
  std::atomic<const void*> pc{nullptr};
  // The task the watch thread has said is stuck (its started_us).
  std::atomic<int64_t> said_stuck{0};
  // Owned by the watched thread.
  int64_t last_note_us = 0;
  int unsaid = 0;
};

Watched g_watched[kMaxThreads];
std::atomic<int> g_count{0};
constinit thread_local Watched* t_watched = nullptr;

const char* Short(const char* file) {
  if (!file) {
    return "?";
  }
  std::string_view view(file);
  while (view.starts_with("../")) {
    view.remove_prefix(3);
  }
  return file + (view.data() - file);
}

// "engine+0x1234" for an address in the engine (llvm-symbolizer
// --relative-address), else the module's name and offset.
void Where(const void* pc, char* out, size_t size) {
  if (!pc) {
    out[0] = 0;
    return;
  }
  HMODULE module = nullptr;
  if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(pc), &module)) {
    snprintf(out, size, ", at %p", pc);
    return;
  }
  const uintptr_t offset =
      reinterpret_cast<uintptr_t>(pc) - reinterpret_cast<uintptr_t>(module);
  if (module == reinterpret_cast<HMODULE>(&__ImageBase)) {
    snprintf(out, size, ", at engine+0x%lx", static_cast<unsigned long>(offset));
    return;
  }
  wchar_t path[MAX_PATH];
  const DWORD length = ::GetModuleFileNameW(module, path, MAX_PATH);
  const wchar_t* base = path;
  for (DWORD i = 0; i < length; ++i) {
    if (path[i] == L'\\' || path[i] == L'/') {
      base = path + i + 1;
    }
  }
  snprintf(out, size, ", at %ls+0x%lx", length ? base : L"?",
           static_cast<unsigned long>(offset));
}

class Observer : public base::TaskObserver {
 public:
  explicit Observer(Watched* watched) : watched_(watched) {}

  void WillProcessTask(const base::PendingTask& task,
                       bool was_blocked_or_low_priority) override {
    watched_->function.store(task.posted_from.function_name(),
                             std::memory_order_relaxed);
    watched_->file.store(task.posted_from.file_name(),
                         std::memory_order_relaxed);
    watched_->line.store(task.posted_from.line_number(),
                         std::memory_order_relaxed);
    watched_->pc.store(task.posted_from.program_counter(),
                       std::memory_order_relaxed);
    watched_->started_us.store(NowUs(), std::memory_order_release);
  }

  void DidProcessTask(const base::PendingTask& task) override {
    const int64_t started = watched_->started_us.exchange(0);
    if (!started) {
      return;
    }
    const int64_t now = NowUs();
    const int64_t took = now - started;
    if (took < kLongTaskUs) {
      return;
    }
    if (now - watched_->last_note_us < kNoteEveryUs) {
      ++watched_->unsaid;
      return;
    }
    watched_->last_note_us = now;
    char where[160];
    Where(task.posted_from.program_counter(), where, sizeof(where));
    char more[64] = "";
    if (watched_->unsaid) {
      snprintf(more, sizeof(more), " (and %d more long tasks since)",
               watched_->unsaid);
      watched_->unsaid = 0;
    }
    const char* function = task.posted_from.function_name();
    NoteF("hang: the %s thread spent %d ms in a task posted from %s "
          "(%s:%d%s)%s",
          watched_->name, static_cast<int>(took / 1000),
          function ? function : "?", Short(task.posted_from.file_name()),
          task.posted_from.line_number(), where, more);
  }

 private:
  const raw_ptr<Watched> watched_;
};

// --- Processor time, by thread ----------------------------------------------

struct CpuThread {
  DWORD id = 0;
  const char* name = nullptr;
  HANDLE handle = nullptr;
  uint64_t last = 0;  // 100 ns units of kernel and user time
};

base::Lock& CpuLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}

std::vector<CpuThread>& CpuThreads() {
  static base::NoDestructor<std::vector<CpuThread>> threads;
  return *threads;
}

std::atomic<bool> g_cpu_accounting{false};

uint64_t CpuTime(HANDLE thread) {
  FILETIME created, exited, kernel, user;
  if (!::GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
    return 0;
  }
  return ((static_cast<uint64_t>(kernel.dwHighDateTime) << 32) |
          kernel.dwLowDateTime) +
         ((static_cast<uint64_t>(user.dwHighDateTime) << 32) |
          user.dwLowDateTime);
}

uint64_t ProcessCpuTime() {
  FILETIME created, exited, kernel, user;
  if (!::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel,
                         &user)) {
    return 0;
  }
  return ((static_cast<uint64_t>(kernel.dwHighDateTime) << 32) |
          kernel.dwLowDateTime) +
         ((static_cast<uint64_t>(user.dwHighDateTime) << 32) |
          user.dwLowDateTime);
}

// Called on each thread as Chromium names it.
class NameObserver : public base::ThreadIdNameManager::Observer {
 public:
  void OnThreadNameChanged(const char* name) override {
    const DWORD id = ::GetCurrentThreadId();
    base::AutoLock hold(CpuLock());
    for (CpuThread& thread : CpuThreads()) {
      if (thread.id == id) {
        thread.name = name;
        return;
      }
    }
    HANDLE handle = nullptr;
    if (!::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(),
                           ::GetCurrentProcess(), &handle,
                           THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0)) {
      return;
    }
    CpuThreads().push_back({id, name, handle, CpuTime(handle)});
  }
};

// "CompositorTileWorker2" and "ThreadPoolForegroundWorker" counted as one
// kind each: their trailing numbers go.
std::string Kind(const char* name) {
  std::string kind = name ? name : "?";
  while (!kind.empty() && base::IsAsciiDigit(kind.back())) {
    kind.pop_back();
  }
  return kind;
}

void ReportCpu(int64_t window_us) {
  static uint64_t last_process = 0;
  const uint64_t process = ProcessCpuTime();
  const uint64_t process_used = last_process ? process - last_process : 0;
  last_process = process;
  std::vector<std::pair<std::string, uint64_t>> kinds;
  {
    base::AutoLock hold(CpuLock());
    for (CpuThread& thread : CpuThreads()) {
      const uint64_t now = CpuTime(thread.handle);
      const uint64_t used = now > thread.last ? now - thread.last : 0;
      thread.last = now;
      if (!used) {
        continue;
      }
      const std::string kind = Kind(thread.name);
      auto it = std::find_if(kinds.begin(), kinds.end(),
                             [&](const auto& k) { return k.first == kind; });
      if (it == kinds.end()) {
        kinds.emplace_back(kind, used);
      } else {
        it->second += used;
      }
    }
  }
  if (!process_used) {
    return;
  }
  std::sort(kinds.begin(), kinds.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  // 100 ns units over the window: the share of one core, in percent.
  const double window_100ns = static_cast<double>(window_us) * 10.0;
  auto percent = [&](uint64_t used) {
    return static_cast<int>(used * 100.0 / window_100ns + 0.5);
  };
  // Below 20% of the phone's processor there is nothing to say.
  const int cores = base::SysInfo::NumberOfProcessors();
  if (percent(process_used) < 20 * cores) {
    return;
  }
  std::string line = base::StringPrintf(
      "cpu: last %d s, %d%% of one core used (of %d00%%):",
      static_cast<int>((window_us + 500'000) / 1'000'000),
      percent(process_used), cores);
  uint64_t named = 0;
  for (size_t i = 0; i < kinds.size(); ++i) {
    named += kinds[i].second;
    if (i < 10 && percent(kinds[i].second) >= 2) {
      line += base::StringPrintf(" %s %d%%,", kinds[i].first.c_str(),
                                 percent(kinds[i].second));
    }
  }
  line += base::StringPrintf(" unnamed threads %d%%",
                             percent(process_used > named
                                         ? process_used - named
                                         : 0));
  Note(line);
}

// Looks at the watched threads every half second for a task still running,
// and every 10 s at where the processor time went.
class WatchThread : public base::PlatformThread::Delegate {
 public:
  void ThreadMain() override {
    base::PlatformThread::SetName("VwHangWatch");
    int64_t cpu_window_start = NowUs();
    for (;;) {
      base::PlatformThread::Sleep(base::Milliseconds(500));
      const int count = g_count.load(std::memory_order_acquire);
      const int64_t now = NowUs();
      if (g_cpu_accounting.load() && now - cpu_window_start >= 10'000'000) {
        ReportCpu(now - cpu_window_start);
        cpu_window_start = now;
      }
      for (int i = 0; i < count; ++i) {
        Watched& watched = g_watched[i];
        const int64_t started =
            watched.started_us.load(std::memory_order_acquire);
        if (!started || now - started < kStuckUs ||
            watched.said_stuck.load() == started) {
          continue;
        }
        watched.said_stuck.store(started);
        char where[160];
        Where(watched.pc.load(std::memory_order_relaxed), where,
              sizeof(where));
        const char* function = watched.function.load(std::memory_order_relaxed);
        NoteF("hang: the %s thread has been in one task for %d ms, posted "
              "from %s (%s:%d%s)",
              watched.name, static_cast<int>((now - started) / 1000),
              function ? function : "?",
              Short(watched.file.load(std::memory_order_relaxed)),
              watched.line.load(std::memory_order_relaxed), where);
      }
    }
  }
};

bool StartWatchThread() {
  static const bool started = [] {
    return base::PlatformThread::CreateNonJoinable(0, new WatchThread());
  }();
  return started;
}

}  // namespace

void StartCpuAccounting() {
  if (g_cpu_accounting.exchange(true)) {
    return;
  }
  base::ThreadIdNameManager::GetInstance()->AddObserver(new NameObserver());
  StartWatchThread();
}

void WatchCurrentThread(const char* name) {
  if (t_watched || !base::CurrentThread::IsSet()) {
    return;
  }
  static base::NoDestructor<base::Lock> lock;
  base::AutoLock hold(*lock);
  const int index = g_count.load();
  if (index >= kMaxThreads) {
    return;
  }
  Watched* watched = &g_watched[index];
  watched->name = name;
  t_watched = watched;
  base::CurrentThread::Get()->AddTaskObserver(new Observer(watched));
  g_count.store(index + 1, std::memory_order_release);
  const bool watching = StartWatchThread();
  NoteF("hang: watching the %s thread%s", name,
        watching ? "" : " (no watch thread: only tasks that end are said)");
}

}  // namespace venetium_w10m::hang_watch
