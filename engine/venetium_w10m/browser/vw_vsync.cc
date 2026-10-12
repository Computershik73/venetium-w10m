// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_vsync.h"

#include <windows.h>

#include <dxgi.h>
#include <wrl/client.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

using Microsoft::WRL::ComPtr;

// The adapter's first output: on a phone, the panel.
ComPtr<IDXGIOutput> FirstOutput() {
  ComPtr<IDXGIFactory1> factory;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIOutput> output;
  if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
      FAILED(factory->EnumAdapters(0, &adapter)) ||
      FAILED(adapter->EnumOutputs(0, &output))) {
    return nullptr;
  }
  return output;
}

}  // namespace

VwVsync::VwVsync(Report report)
    : report_(std::move(report)),
      origin_(base::SingleThreadTaskRunner::GetCurrentDefault()) {
  thread_ = std::make_unique<base::DelegateSimpleThread>(this, "VwVSync");
  thread_->Start();
}

VwVsync::~VwVsync() {
  stop_.store(true);
  thread_->Join();
}

void VwVsync::Run() {
  ComPtr<IDXGIOutput> output = FirstOutput();
  if (!output) {
    Note("vsync: no display output to wait on; the compositor keeps its "
         "own 60 Hz timer");
    return;
  }
  base::TimeTicks last;
  // Intervals between consecutive blanks, averaged: one wait can be late.
  base::TimeDelta sum;
  int count = 0;
  int reported = 0;
  while (!stop_.load()) {
    if (!active_.load()) {
      ::Sleep(100);
      last = base::TimeTicks();
      continue;
    }
    const HRESULT hr = output->WaitForVBlank();
    const base::TimeTicks now = base::TimeTicks::Now();
    if (FAILED(hr)) {
      // The display is off, or the output went: no beat to follow for now.
      ::Sleep(100);
      last = base::TimeTicks();
      continue;
    }
    if (!last.is_null()) {
      const base::TimeDelta gap = now - last;
      // A real refresh, not a wait that returned at once or slept through
      // several: between 240 Hz and 24 Hz.
      if (gap > base::Milliseconds(4) && gap < base::Milliseconds(42)) {
        sum += gap;
        ++count;
      }
    }
    last = now;
    // Twice a second is plenty: the timer in viz runs on its own in between.
    if (count >= 30) {
      const base::TimeDelta interval = sum / count;
      sum = base::TimeDelta();
      count = 0;
      if (reported++ == 0) {
        NoteF("vsync: the display refreshes every %.2f ms",
              interval.InMillisecondsF());
      }
      origin_->PostTask(FROM_HERE, base::BindOnce(report_, now, interval));
    }
  }
}

}  // namespace venetium_w10m
