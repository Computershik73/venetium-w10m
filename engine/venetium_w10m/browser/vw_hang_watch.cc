// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_hang_watch.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/command_line.h"
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

// V8's builtins, the interpreter's bytecode handlers among them: one blob in
// the engine's image (v8/src/execution/isolate.cc).
extern "C" const uint8_t v8_Default_embedded_blob_code_[];
extern "C" uint32_t v8_Default_embedded_blob_code_size_;

// PartitionAlloc's JIT layer for Windows 10 Mobile
// (partition_alloc/page_allocator_internals_win.h).
extern "C" void VwJitFlipCounts(uint32_t* to_writable,
                                uint32_t* to_executable);
extern "C" uint32_t VwJitMeasureFlipPair();

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
  uint64_t last = 0;         // 100 ns units of kernel and user time
  uint64_t last_kernel = 0;  // of kernel time alone
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

// A thread's kernel and user time together, and its kernel time in |kernel|.
uint64_t CpuTime(HANDLE thread, uint64_t* kernel_time = nullptr) {
  FILETIME created, exited, kernel, user;
  if (!::GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
    return 0;
  }
  const uint64_t in_kernel =
      (static_cast<uint64_t>(kernel.dwHighDateTime) << 32) |
      kernel.dwLowDateTime;
  if (kernel_time) {
    *kernel_time = in_kernel;
  }
  return in_kernel + ((static_cast<uint64_t>(user.dwHighDateTime) << 32) |
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
    uint64_t kernel = 0;
    const uint64_t total = CpuTime(handle, &kernel);
    CpuThreads().push_back({id, name, handle, total, kernel});
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

// What one write to a code page and the next run of it cost, in
// nanoseconds; 0 if unknown (measured once, as the watch thread starts).
uint32_t g_jit_pair_ns = 0;

// The JIT layer's page flips in the window, if there were enough to matter.
void ReportJit(int64_t window_us) {
  static uint32_t last_writable = 0;
  static uint32_t last_executable = 0;
  uint32_t writable = 0;
  uint32_t executable = 0;
  VwJitFlipCounts(&writable, &executable);
  const uint32_t new_writable = writable - last_writable;
  const uint32_t new_executable = executable - last_executable;
  last_writable = writable;
  last_executable = executable;
  if (new_writable + new_executable < 50) {
    return;
  }
  // A pair is one flip each way; the cost of a flip is about half of it.
  const double ms =
      (new_writable + new_executable) * (g_jit_pair_ns / 2.0) / 1e6;
  NoteF("jit: last %d s, %u code pages made writable and %u executable "
        "again%s",
        static_cast<int>((window_us + 500'000) / 1'000'000), new_writable,
        new_executable,
        g_jit_pair_ns
            ? base::StringPrintf(", about %.0f ms of the processor", ms)
                  .c_str()
            : "");
}

void ReportCpu(int64_t window_us) {
  static uint64_t last_process = 0;
  ReportJit(window_us);
  const uint64_t process = ProcessCpuTime();
  const uint64_t process_used = last_process ? process - last_process : 0;
  last_process = process;
  struct KindUse {
    std::string kind;
    uint64_t used = 0;
    uint64_t kernel = 0;
  };
  std::vector<KindUse> kinds;
  {
    base::AutoLock hold(CpuLock());
    for (CpuThread& thread : CpuThreads()) {
      uint64_t kernel_now = 0;
      const uint64_t now = CpuTime(thread.handle, &kernel_now);
      const uint64_t used = now > thread.last ? now - thread.last : 0;
      const uint64_t kernel =
          kernel_now > thread.last_kernel ? kernel_now - thread.last_kernel
                                          : 0;
      thread.last = now;
      thread.last_kernel = kernel_now;
      if (!used) {
        continue;
      }
      const std::string kind = Kind(thread.name);
      auto it = std::find_if(kinds.begin(), kinds.end(),
                             [&](const auto& k) { return k.kind == kind; });
      if (it == kinds.end()) {
        kinds.push_back({kind, used, kernel});
      } else {
        it->used += used;
        it->kernel += kernel;
      }
    }
  }
  if (!process_used) {
    return;
  }
  std::sort(kinds.begin(), kinds.end(),
            [](const auto& a, const auto& b) { return a.used > b.used; });
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
    named += kinds[i].used;
    if (i < 10 && percent(kinds[i].used) >= 2) {
      line += base::StringPrintf(" %s %d%%", kinds[i].kind.c_str(),
                                 percent(kinds[i].used));
      // Of which in the kernel (system calls, page faults), when it shows.
      if (percent(kinds[i].kernel) >= 2) {
        line += base::StringPrintf(" (kernel %d%%)", percent(kinds[i].kernel));
      }
      line += ",";
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
    // Only when asked for (--vw-jit-cost): 222.50, which always ran it and
    // the sampler, died at every start on a Lumia 650.
    if (g_cpu_accounting.load() &&
        base::CommandLine::ForCurrentProcess()->HasSwitch("vw-jit-cost")) {
      // Before the first count of flips (ReportJit): the test flips too.
      g_jit_pair_ns = VwJitMeasureFlipPair();
      if (g_jit_pair_ns) {
        NoteF("jit: here a code page written and then run costs %u us (two "
              "faults, two page flips, an instruction cache flush)",
              (g_jit_pair_ns + 500) / 1000);
      } else {
        Note("jit: what a page flip costs could not be measured");
      }
    }
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

// --- Where a watched thread's time goes, by sampling -----------------------

class SamplerThread : public base::PlatformThread::Delegate {
 public:
  SamplerThread(Watched* watched, HANDLE thread)
      : watched_(watched), thread_(thread) {}

  void ThreadMain() override {
    base::PlatformThread::SetName("VwSampler");
    // On time, whatever else runs: a late sample is a biased one.
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    const auto* image = reinterpret_cast<const uint8_t*>(&__ImageBase);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        image + __ImageBase.e_lfanew);
    image_start_ = reinterpret_cast<uintptr_t>(image);
    image_end_ = image_start_ + nt->OptionalHeader.SizeOfImage;
    blob_start_ = reinterpret_cast<uintptr_t>(v8_Default_embedded_blob_code_);
    blob_end_ = blob_start_ + v8_Default_embedded_blob_code_size_;
    int64_t window_start = NowUs();
    int64_t hot_start = window_start;
    for (;;) {
      // 10 ms, or the system timer's next tick (15.6 ms) after it.
      ::Sleep(10);
      if (watched_->started_us.load(std::memory_order_acquire)) {
        Sample();
      }
      const int64_t now = NowUs();
      if (now - window_start >= 10'000'000) {
        ReportWindow(now - window_start);
        window_start = now;
      }
      if (now - hot_start >= 60'000'000) {
        ReportHot();
        hot_start = now;
      }
    }
  }

 private:
  void Sample() {
    // While the thread is suspended nothing here may take a lock it could
    // hold: only its context is read.
    if (::SuspendThread(thread_) == static_cast<DWORD>(-1)) {
      return;
    }
    CONTEXT context;
    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL;
    const bool got = ::GetThreadContext(thread_, &context) != 0;
    ::ResumeThread(thread_);
    if (!got) {
      return;
    }
#if defined(_M_ARM) || defined(_M_ARM64)
    const uintptr_t pc = context.Pc;
#elif defined(_M_X64)
    const uintptr_t pc = context.Rip;
#else
    const uintptr_t pc = context.Eip;
#endif
    ++samples_;
    if (pc >= blob_start_ && pc < blob_end_) {
      ++builtins_;
      Hot(pc);
      return;
    }
    if (pc >= image_start_ && pc < image_end_) {
      ++engine_;
      Hot(pc);
      return;
    }
    MEMORY_BASIC_INFORMATION info;
    if (!::VirtualQuery(reinterpret_cast<const void*>(pc), &info,
                        sizeof(info))) {
      ++other_;
    } else if (info.Type == MEM_PRIVATE) {
      // Not in any image: code V8 compiled.
      ++jit_;
    } else if (info.Type == MEM_IMAGE) {
      ++modules_[reinterpret_cast<uintptr_t>(info.AllocationBase)];
    } else {
      ++other_;
    }
  }

  // The engine's code, in 64-byte pieces, counted since the start.
  void Hot(uintptr_t pc) {
    ++hot_samples_;
    const uint32_t piece =
        static_cast<uint32_t>((pc - image_start_) & ~uintptr_t{63});
    auto it = hot_.find(piece);
    if (it != hot_.end()) {
      ++it->second;
    } else if (hot_.size() < 50'000) {
      hot_.emplace(piece, 1);
    }
  }

  static std::string ModuleName(uintptr_t base) {
    wchar_t path[MAX_PATH];
    const DWORD length = ::GetModuleFileNameW(reinterpret_cast<HMODULE>(base),
                                              path, MAX_PATH);
    if (!length) {
      return "a module";
    }
    std::string name;
    for (DWORD i = 0; i < length; ++i) {
      if (path[i] == L'\\' || path[i] == L'/') {
        name.clear();
      } else {
        name.push_back(path[i] < 128 ? static_cast<char>(path[i]) : '?');
      }
    }
    return name;
  }

  void ReportWindow(int64_t window_us) {
    if (samples_ >= 30) {
      auto percent = [&](int n) {
        return static_cast<int>(n * 100.0 / samples_ + 0.5);
      };
      std::string line = base::StringPrintf(
          "sample: the %s thread, %d samples in tasks over %d s: code V8 "
          "compiled %d%%, V8's builtins and interpreter %d%%, the engine %d%%",
          watched_->name, samples_,
          static_cast<int>((window_us + 500'000) / 1'000'000), percent(jit_),
          percent(builtins_), percent(engine_));
      std::vector<std::pair<int, uintptr_t>> modules;
      for (const auto& [base, n] : modules_) {
        modules.emplace_back(n, base);
      }
      std::sort(modules.begin(), modules.end(), std::greater<>());
      int rest = other_;
      for (size_t i = 0; i < modules.size(); ++i) {
        if (i < 5 && percent(modules[i].first) >= 1) {
          line += base::StringPrintf(", %s %d%%",
                                     ModuleName(modules[i].second).c_str(),
                                     percent(modules[i].first));
        } else {
          rest += modules[i].first;
        }
      }
      if (percent(rest) >= 1) {
        line += base::StringPrintf(", elsewhere %d%%", percent(rest));
      }
      Note(line);
    }
    samples_ = jit_ = builtins_ = engine_ = other_ = 0;
    modules_.clear();
  }

  void ReportHot() {
    if (hot_samples_ < 100) {
      return;
    }
    std::vector<std::pair<uint32_t, uint32_t>> top;
    for (const auto& [piece, n] : hot_) {
      top.emplace_back(n, piece);
    }
    const size_t count = std::min<size_t>(top.size(), 24);
    std::partial_sort(top.begin(), top.begin() + count, top.end(),
                      std::greater<>());
    std::string line = base::StringPrintf(
        "sample: the %s thread's hottest engine code since the start (%d "
        "samples in the engine):",
        watched_->name, hot_samples_);
    for (size_t i = 0; i < count; ++i) {
      line += base::StringPrintf(" engine+0x%x %.1f%%,", top[i].second,
                                 top[i].first * 100.0 / hot_samples_);
    }
    line.pop_back();
    Note(line);
  }

  const raw_ptr<Watched> watched_;
  const HANDLE thread_;
  uintptr_t image_start_ = 0;
  uintptr_t image_end_ = 0;
  uintptr_t blob_start_ = 0;
  uintptr_t blob_end_ = 0;
  // This window's samples.
  int samples_ = 0;
  int jit_ = 0;
  int builtins_ = 0;
  int engine_ = 0;
  int other_ = 0;
  std::map<uintptr_t, int> modules_;
  // Since the start.
  int hot_samples_ = 0;
  std::unordered_map<uint32_t, uint32_t> hot_;
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

void SampleCurrentThread() {
  // Only when asked for (--vw-sample): 222.50, which always sampled, died at
  // every start on a Lumia 650 (an illegal instruction on the UI thread).
  if (!t_watched ||
      !base::CommandLine::ForCurrentProcess()->HasSwitch("vw-sample")) {
    return;
  }
  HANDLE thread = nullptr;
  if (!::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(),
                         ::GetCurrentProcess(), &thread,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                             THREAD_QUERY_INFORMATION,
                         FALSE, 0)) {
    NoteF("sample: the %s thread cannot be sampled (%lu)", t_watched->name,
          ::GetLastError());
    return;
  }
  if (!base::PlatformThread::CreateNonJoinable(
          0, new SamplerThread(t_watched, thread))) {
    ::CloseHandle(thread);
    return;
  }
  NoteF("sample: sampling the %s thread", t_watched->name);
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
