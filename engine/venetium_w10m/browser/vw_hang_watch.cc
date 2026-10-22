// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_hang_watch.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/pending_task.h"
#include "base/synchronization/lock.h"
#include "base/task/current_thread.h"
#include "base/task/task_observer.h"
#include "base/threading/platform_thread.h"
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

// Looks at the watched threads every half second for a task still running.
class WatchThread : public base::PlatformThread::Delegate {
 public:
  void ThreadMain() override {
    base::PlatformThread::SetName("VwHangWatch");
    for (;;) {
      base::PlatformThread::Sleep(base::Milliseconds(500));
      const int count = g_count.load(std::memory_order_acquire);
      const int64_t now = NowUs();
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

}  // namespace

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
  static const bool watching = [] {
    return base::PlatformThread::CreateNonJoinable(0, new WatchThread());
  }();
  NoteF("hang: watching the %s thread%s", name,
        watching ? "" : " (no watch thread: only tasks that end are said)");
}

}  // namespace venetium_w10m::hang_watch
