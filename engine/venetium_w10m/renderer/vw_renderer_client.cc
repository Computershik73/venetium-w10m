// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/renderer/vw_renderer_client.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/synchronization/lock.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "content/public/common/alternative_error_page_override_info.mojom.h"
#include "media/base/media_types.h"
#include "media/base/video_codecs.h"
#include "media/base/video_codecs_w10m.h"
#include "net/base/net_errors.h"
#include "partition_alloc/memory_reclaimer.h"
#include "third_party/blink/public/platform/web_url_error.h"
#include "third_party/blink/public/web/blink.h"
#include "url/gurl.h"
#include "v8/include/v8-isolate.h"
#include "venetium_w10m/browser/vw_browser.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// The renderer's main thread, for the browser's threads to reach: in one
// process there is no other way to it than its task runner.
struct RendererThread {
  base::Lock lock;
  scoped_refptr<base::SingleThreadTaskRunner> runner;
};

RendererThread& TheRendererThread() {
  static base::NoDestructor<RendererThread> thread;
  return *thread;
}

// What VwMemory last counted (SetUnloadableTabs).
std::atomic<int> g_unloadable_tabs{0};

// Renderer thread. Returns the milliseconds the collection took.
int CollectGarbageNow() {
  const base::TimeTicks start = base::TimeTicks::Now();
  blink::MemoryPressureNotificationToAllIsolates(
      v8::MemoryPressureLevel::kCritical);
  const int took =
      static_cast<int>((base::TimeTicks::Now() - start).InMilliseconds());
  // What the collection freed goes back to the system with the allocator's
  // free pages, whose own turn comes only every few seconds. Here and not
  // on the browser's thread: this one has just stood still for the
  // collection anyway, and that one passes the touches on.
  partition_alloc::MemoryReclaimer::Instance()->ReclaimAll();
  return took;
}

// Renderer thread, called by V8 when collecting has not brought the heap
// back under its limit and the next thing it would do is end the process
// (v8::NearHeapLimitCallback). The one heap holds the scripts of every tab,
// so while the browser has background tabs to unload there is something to
// give: the limit is raised by a quarter of what it started at -- room for
// the script that is running to go on -- and the browser unloads them, for
// the next collection to find their pages. With nothing to unload a higher
// limit would free nothing and only move the end, so none is given. Not
// past twice the first limit either: from there V8 ends the process as it
// would have.
size_t NearHeapLimit(void* data,
                     size_t current_heap_limit,
                     size_t initial_heap_limit) {
  if (g_unloadable_tabs.load(std::memory_order_relaxed) <= 0) {
    // V8 may ask again at every collection from here to its end.
    static base::TimeTicks noted;
    const base::TimeTicks now = base::TimeTicks::Now();
    if (noted.is_null() || now - noted > base::Seconds(10)) {
      noted = now;
      NoteF("memory: the JavaScript heap is at its limit of %d MB and there "
            "is no background tab to unload; the limit stays",
            static_cast<int>(current_heap_limit >> 20));
    }
    return current_heap_limit;
  }
  const size_t ceiling =
      initial_heap_limit +
      std::min(initial_heap_limit,
               std::numeric_limits<size_t>::max() - initial_heap_limit);
  if (current_heap_limit >= ceiling) {
    NoteF("memory: the JavaScript heap is at %d MB, twice its limit; V8 ends "
          "the process",
          static_cast<int>(current_heap_limit >> 20));
    return current_heap_limit;
  }
  const size_t more = initial_heap_limit / 4;
  const size_t raised = ceiling - current_heap_limit < more
                            ? ceiling
                            : current_heap_limit + more;
  NoteF("memory: the JavaScript heap reached its limit of %d MB; raised to "
        "%d MB",
        static_cast<int>(current_heap_limit >> 20),
        static_cast<int>(raised >> 20));
  VwEngine::Get()->PostToBrowser(base::BindOnce([](VwBrowser* browser) {
    browser->memory()->OnAlarm("the JavaScript heap reached its limit");
  }));
  return raised;
}

// Plain, readable on a small screen, and in the two languages the shell
// speaks; the error's own name says what went wrong to anyone who asks.
constexpr char kErrorPage[] = R"page(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%TITLE%</title>
<style>
body { font-family: "Segoe UI", sans-serif; margin: 24px; color: #222;
       background: #fafafa; }
h1 { font-size: 20px; font-weight: 600; margin: 32px 0 12px; }
p { font-size: 15px; line-height: 1.4; color: #555; word-break: break-all; }
code { font-size: 13px; color: #888; }
button { margin-top: 20px; font-size: 16px; padding: 10px 24px;
         border: 0; border-radius: 4px; background: #2b6ed2; color: #fff; }
@media (prefers-color-scheme: dark) {
  body { background: #1c1b22; color: #eee; } p { color: #bbb; } }
</style></head><body>
<h1>Страница недоступна · This page can't be reached</h1>
<p>%URL%</p>
<p><code>%ERROR%</code></p>
<button onclick="location.reload()">Обновить · Reload</button>
</body></html>)page";

void Replace(std::string* text, std::string_view key, std::string_view value) {
  for (size_t at = text->find(key); at != std::string::npos;
       at = text->find(key, at + value.size())) {
    text->replace(at, key.size(), value);
  }
}

}  // namespace

VwContentRendererClient::VwContentRendererClient() {
  // Made inside ContentMain, after the shell's switches are on the command
  // line: one line in the log says which way this run answers.
  Note(media::W10mEveryVideoCodec()
           ? "codecs: every video codec the build decodes (--vw-codecs=all)"
           : "codecs: pages are told H.264 and VP8; VP9, AV1 and HEVC are "
             "refused (--vw-codecs=all offers them again)");
}
VwContentRendererClient::~VwContentRendererClient() = default;

void VwContentRendererClient::RenderThreadStarted() {
  {
    RendererThread& thread = TheRendererThread();
    base::AutoLock hold(thread.lock);
    thread.runner = base::SingleThreadTaskRunner::GetCurrentDefault();
  }
  // Blink has made the main thread's isolate and entered it by now
  // (RenderThreadImpl::Init). Workers have isolates of their own, which
  // this does not reach.
  if (v8::Isolate* isolate = v8::Isolate::TryGetCurrent()) {
    isolate->AddNearHeapLimitCallback(&NearHeapLimit, nullptr);
    // And back to the first limit once the heap is under half of it.
    isolate->AutomaticallyRestoreInitialHeapLimit(0.5);
  }
}

void SetUnloadableTabs(int count) {
  g_unloadable_tabs.store(count, std::memory_order_relaxed);
}

bool CollectRendererGarbage(base::OnceCallback<void(int)> done) {
  scoped_refptr<base::SingleThreadTaskRunner> runner;
  {
    RendererThread& thread = TheRendererThread();
    base::AutoLock hold(thread.lock);
    runner = thread.runner;
  }
  return runner && runner->PostTaskAndReplyWithResult(
                       FROM_HERE, base::BindOnce(&CollectGarbageNow),
                       std::move(done));
}

void VwContentRendererClient::PrepareErrorPage(
    content::RenderFrame* render_frame,
    const blink::WebURLError& error,
    const std::string& http_method,
    content::mojom::AlternativeErrorPageOverrideInfoPtr
        alternative_error_page_info,
    std::string* error_html) {
  if (!error_html) {
    return;
  }
  const GURL url = error.url();
  const std::string escaped_url =
      base::EscapeForHTML(url.possibly_invalid_spec());
  std::string page = kErrorPage;
  Replace(&page, "%TITLE%", base::EscapeForHTML(url.host()));
  Replace(&page, "%URL%", escaped_url);
  Replace(&page, "%ERROR%", net::ErrorToShortString(error.reason()));
  *error_html = std::move(page);
}

// A site that is told VP9 or AV1 plays sends that: YouTube does. Both are
// decoded on the CPU here, four cores of a 32-bit process, and that is more
// than a phone decodes in time; H.264 is the codec these phones are built
// to play. Told that only H.264 and VP8 play, sites send H.264 (YouTube up
// to 1080p). This one
// answer is behind canPlayType and <source type>, MediaSource,
// MediaCapabilities, Encrypted Media, the HLS player's choice of variant,
// WebCodecs and the demuxer of a file played by its address: a WebM whose
// picture is VP9 is not played either, as in Safari before it had VP9. An
// allow-list, the same two codecs calls are held in (blink's
// video_codec_factory.cc), so HEVC stays refused whatever a driver comes to
// report. VP8 stays for old WebM files; audio is not asked about here.
// "--vw-codecs=all" among the shell's switches gives Chromium's answers
// back, for comparing on a device.
bool VwContentRendererClient::IsDecoderSupportedVideoType(
    const media::VideoType& type) {
  // Any thread: canPlayType on the main one, the demuxer on the media
  // thread, WebCodecs in a worker.
  if (!media::W10mEveryVideoCodec() &&
      type.codec != media::VideoCodec::kH264 &&
      type.codec != media::VideoCodec::kVP8) {
    return false;
  }
  // What is allowed still has to be decodable.
  return content::ContentRendererClient::IsDecoderSupportedVideoType(type);
}

}  // namespace venetium_w10m
