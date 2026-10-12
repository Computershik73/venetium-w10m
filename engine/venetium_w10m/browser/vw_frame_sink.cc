// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_frame_sink.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_3.h>
#include <string.h>
#include <wrl/client.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/viz/service/display/software_output_device.h"
#include "components/viz/service/display/w10m_d3d11_target.h"
#include "components/viz/service/display_embedder/w10m_software_output_device.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "ui/gl/w10m_panel.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_panel.h"
#include "venetium_w10m/browser/vw_present_guard.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

using Microsoft::WRL::ComPtr;

// A swap chain that could not be had is asked for again after a wait that
// doubles, from the first to the longest. Failures that were not for want of
// memory end it after this many in a row.
constexpr base::TimeDelta kFirstBackoff = base::Seconds(1);
constexpr base::TimeDelta kMaxBackoff = base::Seconds(30);
constexpr int kStrikes = 3;

// Frames of the Direct3D 11 compositor's that could not be presented, or
// that lost the device before they could be, in a row, before it is given
// up: the device going once is a device going, twice with none of its
// frames shown in between is its doing. Fewer than kStrikes, so that what
// only its frames ask of the driver cannot cost software frames their swap
// chain.
constexpr int kGpuStrikes = 2;
static_assert(kGpuStrikes < kStrikes);

// From the app being told it is suspended to its being frozen: the shell
// holds that off for a second and a half (its App.cpp), and frames are
// still made meanwhile. What is given back at the start of it is not made
// again before it is over, and a giving back that only gets its turn after
// it -- when the app is back -- is not done at all.
constexpr base::TimeDelta kSuspendWindow = base::Seconds(3);

// Every device of the run has a number of its own: viz's renderer keeps
// what it made on a device by it (viz::W10mD3D11Target). Viz thread.
uint32_t g_devices = 0;

// Software frames presented by the GPU: a Direct3D 11 swap chain made for
// composition and shown by the shell's SwapChainPanel. Only what changed is
// uploaded; the system compositor shows it, in step with the display. Works
// at every feature level from 9_3 up, and is what everything faster falls
// back to. Its swap chain is gecko-w10m's proven one: BGRA, flip-sequential,
// stretched, opaque.
//
// With the Direct3D 11 compositor (|compositor|) the same chain also takes
// frames viz draws into its back buffer itself, on this device; the two kinds
// follow each other in any order. A frame drawn there is always the whole
// buffer, so the chain's other buffers have missed everything when one is
// presented, and the next software frame -- which viz draws whole after one
// of those -- is uploaded whole to each of them in turn.
class D3D11Target {
 public:
  D3D11Target(int buffers, bool compositor, bool level_9_3)
      : buffers_(std::clamp(buffers, 2, 3)),
        compositor_(compositor),
        level_9_3_(level_9_3) {}
  D3D11Target(const D3D11Target&) = delete;
  D3D11Target& operator=(const D3D11Target&) = delete;
  ~D3D11Target() { Release(); }

  // There cannot be a chain in this run: the caller falls back to the
  // bitmap for good.
  bool failed() const { return failed_; }

  // Viz thread: a chain of |size| in the panel. False while there is none:
  // the caller publishes to the bitmap and, unless failed(), asks again with
  // the next frame. A chain that could not be made is not tried again before
  // its back-off is over.
  bool EnsureSize(const gfx::Size& size) {
    if (failed_ || size.IsEmpty()) {
      return false;
    }
    if (chain_ && size == size_) {
      return true;
    }
    // Nothing may hold a buffer across a resize.
    if (gpu_view_) {
      EndGpuFrame();
    }
    if (!chain_ && !retry_at_.is_null() &&
        base::TimeTicks::Now() < retry_at_) {
      return false;
    }
    HRESULT hr = S_OK;
    if (chain_) {
      // The room changed (the keyboard, a system bar, the phone turned): the
      // same chain, with buffers of the new size.
      hr = chain_->ResizeBuffers(0, size.width(), size.height(),
                                 DXGI_FORMAT_UNKNOWN, 0);
      if (FAILED(hr) && !DeviceGone(hr)) {
        // Refused. On these phones that is address space, each buffer being
        // wanted in one piece; gecko-w10m met it on an HP Elite x3 and found
        // the second try taken, the old buffers having gone with the first.
        NoteF("d3d11: ResizeBuffers to %dx%d failed, 0x%08lx; once more",
              size.width(), size.height(), static_cast<unsigned long>(hr));
        context_->ClearState();
        context_->Flush();
        hr = chain_->ResizeBuffers(0, size.width(), size.height(),
                                   DXGI_FORMAT_UNKNOWN, 0);
      }
      if (FAILED(hr)) {
        // Not this chain, then. It leaves the panel before another is made,
        // or the new buffers would be wanted beside the old ones.
        NoteF("d3d11: ResizeBuffers to %dx%d failed, 0x%08lx: a new swap "
              "chain",
              size.width(), size.height(), static_cast<unsigned long>(hr));
        DropChain(DeviceGone(hr));
      }
    }
    if (!device_ && !CreateDevice()) {
      return Later("D3D11CreateDevice", E_FAIL);
    }
    if (!chain_) {
      ComPtr<IDXGIDevice1> dxgi_device;
      ComPtr<IDXGIAdapter> adapter;
      ComPtr<IDXGIFactory2> factory;
      hr = device_.As(&dxgi_device);
      if (SUCCEEDED(hr)) hr = dxgi_device->GetAdapter(&adapter);
      if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
      if (FAILED(hr)) {
        return Later("the DXGI factory", hr);
      }
      DXGI_SWAP_CHAIN_DESC1 desc = {};
      desc.Width = size.width();
      desc.Height = size.height();
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      desc.BufferCount = buffers_;
      desc.Scaling = DXGI_SCALING_STRETCH;
      desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
      desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
      hr = factory->CreateSwapChainForComposition(device_.Get(), &desc,
                                                  nullptr, &chain_);
      if (FAILED(hr)) {
        return Later("CreateSwapChainForComposition", hr);
      }
      dxgi_device->SetMaximumFrameLatency(1);
      generation_ = PanelHost::Get()->Attach(chain_.Get(), PresentKind::kD3D11);
      if (!generation_) {
        return Later("giving the swap chain to the panel", E_FAIL);
      }
      kind_ = PresentKind::kD3D11;
    }
    size_ = size;
    // Every buffer holds nothing yet: whole frames until each has had one.
    // Counted, and not left to the frames' damage: a chain made again
    // between two frames (after a back-off) starts with a frame whose damage
    // is only what changed, and its last buffer would never be filled.
    history_.assign(buffers_ - 1, gfx::Rect(size));
    blank_buffers_ = buffers_;
    NoteF("d3d11: composition swap chain %dx%d, %d buffers", size.width(),
          size.height(), buffers_);
    return true;
  }

  // Viz thread: |frame| with |damage| changed since the last one.
  bool Present(const SkPixmap& frame, const gfx::Rect& damage) {
    if (failed_ || !chain_) {
      return false;
    }
    // The back buffer holds the frame of |buffers_| presents ago: it needs
    // this frame's damage and that of the frames it missed.
    gfx::Rect upload = damage;
    for (const gfx::Rect& missed : history_) {
      upload.Union(missed);
    }
    if (blank_buffers_ > 0) {
      --blank_buffers_;
      upload = gfx::Rect(size_);
    }
    upload.Intersect(gfx::Rect(size_));
    history_.push_front(damage);
    history_.resize(buffers_ - 1);
    if (!upload.IsEmpty()) {
      ComPtr<ID3D11Texture2D> back;
      HRESULT hr = chain_->GetBuffer(0, IID_PPV_ARGS(&back));
      if (FAILED(hr)) {
        return Later("GetBuffer", hr);
      }
      const D3D11_BOX box = {static_cast<UINT>(upload.x()),
                             static_cast<UINT>(upload.y()), 0,
                             static_cast<UINT>(upload.right()),
                             static_cast<UINT>(upload.bottom()), 1};
      const uint8_t* src = static_cast<const uint8_t*>(frame.addr()) +
                           upload.y() * frame.rowBytes() + upload.x() * 4;
      context_->UpdateSubresource(back.Get(), 0, &box, src,
                                  static_cast<UINT>(frame.rowBytes()), 0);
    }
    const base::TimeTicks before = base::TimeTicks::Now();
    const HRESULT hr = chain_->Present(1, 0);
    if (FAILED(hr)) {
      return Later("Present", hr);
    }
    NotePace(before);
    strikes_ = 0;
    backoff_ = base::TimeDelta();
    retry_at_ = base::TimeTicks();
    if (!presented_) {
      presented_ = true;
      VwEngine::Get()->HardwarePresentWorked(PresentKind::kD3D11);
    }
    PanelHost::Get()->Presented(generation_);
    return true;
  }

  // --- The Direct3D 11 compositor: viz::W10mD3D11Target, by way of the
  // output device. Viz thread. ---

  // A frame viz draws itself: a view of the back buffer of a chain of
  // |size|.
  viz::W10mD3D11Target::Begin BeginGpuFrame(const gfx::Size& size,
                                            ID3D11RenderTargetView** view) {
    using Begin = viz::W10mD3D11Target::Begin;
    *view = nullptr;
    if (gpu_view_) {
      // A frame begun and never ended: nothing more is drawn into it.
      EndGpuFrame();
    }
    if (!compositor_ || failed_) {
      return Begin::kNever;
    }
    // No view could be had a moment ago, for want of memory, or the app is
    // on its way out: software frames need none, and go on through this
    // chain meanwhile.
    if (!gpu_retry_at_.is_null() && base::TimeTicks::Now() < gpu_retry_at_) {
      return Begin::kNotNow;
    }
    if (!EnsureSize(size)) {
      return failed_ ? Begin::kNever : Begin::kNotNow;
    }
    // From here the driver is asked for what software frames never needed:
    // the run counts against the compositor (vw_present_guard.h).
    PresentGuard::Get()->Entered(PresentKind::kCompositor);
    // Made for the frame and gone with it (gecko-w10m does the same on a
    // Lumia 650): nothing holds the buffer when the chain is resized.
    ComPtr<ID3D11Texture2D> back;
    HRESULT hr = chain_->GetBuffer(0, IID_PPV_ARGS(&back));
    if (SUCCEEDED(hr)) {
      hr = device_->CreateRenderTargetView(back.Get(), nullptr, &gpu_view_);
    }
    if (SUCCEEDED(hr) && !gpu_view_) {
      hr = E_FAIL;
    }
    if (FAILED(hr)) {
      gpu_view_.Reset();
      // Nothing of the chain is held when it is dropped below.
      back.Reset();
      if (DeviceGone(hr) || FAILED(device_->GetDeviceRemovedReason())) {
        // The device went: the chain goes the usual way, and is asked for
        // again. Software frames never ask for a view, so twice running
        // this is the compositor's.
        Later("a view of the back buffer", hr);
        if (DeviceWentInGpuFrame()) {
          return Begin::kNever;
        }
        return failed_ ? Begin::kNever : Begin::kNotNow;
      }
      if (hr == E_OUTOFMEMORY) {
        // The chain is good, and stays: without it frames would go to the
        // bitmap, which takes more memory, not less.
        gpu_backoff_ = gpu_backoff_.is_zero()
                           ? kFirstBackoff
                           : std::min(gpu_backoff_ * 2, kMaxBackoff);
        gpu_retry_at_ = base::TimeTicks::Now() + gpu_backoff_;
        NoteF("d3d11: no view of the back buffer for want of memory, "
              "0x%08lx: software frames for %d s",
              static_cast<unsigned long>(hr),
              static_cast<int>(gpu_backoff_.InSeconds()));
        return Begin::kNotNow;
      }
      // The chain is good for software frames, and stays.
      NoteF("d3d11: no render target view of the back buffer, 0x%08lx",
            static_cast<unsigned long>(hr));
      StopCompositor("the swap chain's back buffer cannot be drawn into",
                     /*for_good=*/true);
      return Begin::kNever;
    }
    *view = gpu_view_.Get();
    return Begin::kReady;
  }

  // The frame begun is drawn: on screen with it. False if it did not get
  // there; viz has the CPU draw it then.
  bool PresentGpuFrame() {
    const bool begun = gpu_view_.Get() != nullptr;
    EndGpuFrame();
    if (!begun || failed_ || !chain_) {
      return false;
    }
    const base::TimeTicks before = base::TimeTicks::Now();
    const HRESULT hr = chain_->Present(1, 0);
    if (FAILED(hr)) {
      Later("Present of a frame drawn by Direct3D 11", hr);
      // Memory comes and goes; anything else twice running is the frames.
      if (hr != E_OUTOFMEMORY && ++gpu_strikes_ >= kGpuStrikes) {
        StopCompositor("frames drawn by Direct3D 11 could not be presented",
                       /*for_good=*/true);
      }
      return false;
    }
    // A whole frame: this buffer is blank no longer, and every other buffer
    // has missed all of it, whatever the software frames before it changed.
    if (blank_buffers_ > 0) {
      --blank_buffers_;
    }
    history_.assign(buffers_ - 1, gfx::Rect(size_));
    NotePace(before);
    strikes_ = 0;
    gpu_strikes_ = 0;
    backoff_ = base::TimeDelta();
    retry_at_ = base::TimeTicks();
    gpu_backoff_ = base::TimeDelta();
    gpu_retry_at_ = base::TimeTicks();
    if (kind_ != PresentKind::kCompositor) {
      kind_ = PresentKind::kCompositor;
      PanelHost::Get()->SetKind(generation_, kind_);
    }
    if (!gpu_presented_) {
      gpu_presented_ = true;
      VwEngine::Get()->HardwarePresentWorked(PresentKind::kCompositor);
    }
    PanelHost::Get()->Presented(generation_);
    return true;
  }

  // The frame begun is not presented; nothing was drawn, or a whole software
  // frame follows.
  void AbandonGpuFrame() {
    EndGpuFrame();
    // If what failed on viz's side was the device going, the chain goes the
    // usual way, and comes back the usual way.
    if (device_ && chain_) {
      const HRESULT reason = device_->GetDeviceRemovedReason();
      if (FAILED(reason)) {
        Later("the device", reason);
        DeviceWentInGpuFrame();
      }
    }
  }

  // No more frames drawn by viz in this run; software frames as before, on
  // the same chain.
  void StopCompositor(const char* reason, bool for_good) {
    compositor_ = false;
    EndGpuFrame();
    VwEngine::Get()->CompositorFailed(reason, for_good);
    if (kind_ == PresentKind::kCompositor) {
      kind_ = PresentKind::kD3D11;
      PanelHost::Get()->SetKind(generation_, kind_);
    }
  }

  // Viz thread, between frames, the app being suspended: what the driver
  // keeps for speed goes back to the system, as Direct3D asks of an app
  // that is suspended. Neither side keeps anything bound between frames.
  void Trim(bool driver_too) {
    // The frames still made on the way out are the CPU's: the textures viz
    // has just let go of are not made again for a suspended app to hold.
    gpu_retry_at_ =
        std::max(gpu_retry_at_, base::TimeTicks::Now() + kSuspendWindow);
    if (!driver_too || !device_ || !context_) {
      return;
    }
    ComPtr<IDXGIDevice3> dxgi_device;
    if (FAILED(device_.As(&dxgi_device))) {
      return;
    }
    if (!trimmed_) {
      // Said once, before the first: no driver here has been asked this
      // yet, and a log that ends with this line says which could not.
      trimmed_ = true;
      Note("d3d11: trimming the device, the app being suspended");
    }
    context_->ClearState();
    context_->Flush();
    dxgi_device->Trim();
  }

  // Not AddRef'd; null and 0 while there is no device.
  ID3D11Device* device() const { return device_.Get(); }
  ID3D11DeviceContext* context() const { return context_.Get(); }
  uint32_t device_generation() const { return device_generation_; }

  void Release() {
    if (generation_) {
      PanelHost::Get()->Detach(generation_);
      generation_ = 0;
    }
    gpu_view_.Reset();
    chain_.Reset();
    context_.Reset();
    device_.Reset();
    device_generation_ = 0;
  }

 private:
  // The device went while a frame of viz's own was being made. Counted with
  // the frames that could not be presented; true if that ended the
  // compositor.
  bool DeviceWentInGpuFrame() {
    if (++gpu_strikes_ < kGpuStrikes) {
      return false;
    }
    StopCompositor("the device was lost twice running while Direct3D 11 was "
                   "to draw a frame",
                   /*for_good=*/true);
    return true;
  }

  // The view of a frame viz drew, or did not: unbound and let go, so that
  // nothing holds the back buffer between frames.
  void EndGpuFrame() {
    if (context_) {
      context_->OMSetRenderTargets(0, nullptr, nullptr);
    }
    gpu_view_.Reset();
  }

  bool CreateDevice() {
    static const D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3};
    D3D_FEATURE_LEVEL level = {};
    if (level_9_3_) {
      // --vw-gpu=d3d11: the device a 9_3 phone has, whatever this one could
      // do, so that the compositor is tested within a 9_3 phone's limits.
      static const D3D_FEATURE_LEVEL kLevel93 = D3D_FEATURE_LEVEL_9_3;
      const HRESULT only = D3D11CreateDevice(
          nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
          D3D11_CREATE_DEVICE_BGRA_SUPPORT, &kLevel93, 1, D3D11_SDK_VERSION,
          &device_, &level, &context_);
      if (SUCCEEDED(only)) {
        device_generation_ = ++g_devices;
        NoteF("d3d11: device %u at feature level 0x%x (no better was asked "
              "for)",
              device_generation_, static_cast<unsigned>(level));
        return true;
      }
      // This driver has no 9_3 device to give. The compositor is the same
      // code at any level: tried at the phone's own, and said so.
      NoteF("d3d11: no device at feature level 9_3 alone, 0x%08lx: the "
            "phone's best level instead (not a test of 9_3)",
            static_cast<unsigned long>(only));
    }
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels, std::size(kLevels),
        D3D11_SDK_VERSION, &device_, &level, &context_);
    if (hr == E_INVALIDARG) {  // a runtime without 11_1
      hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels + 1,
                             std::size(kLevels) - 1, D3D11_SDK_VERSION,
                             &device_, &level, &context_);
    }
    if (FAILED(hr)) {
      NoteF("d3d11: D3D11CreateDevice failed, 0x%08lx",
            static_cast<unsigned long>(hr));
      return false;
    }
    device_generation_ = ++g_devices;
    NoteF("d3d11: device %u at feature level 0x%x", device_generation_,
          static_cast<unsigned>(level));
    return true;
  }

  // Every ten seconds of presenting, how even the frames were (as
  // gecko-w10m's notes, which its scrolling was tuned by). Gaps inside a run
  // of frames only: a page standing still presents nothing, and that is not
  // a late frame. A summary, never a line a frame: a line in the shell's log
  // is a write and a flush.
  void NotePace(base::TimeTicks before) {
    const base::TimeTicks now = base::TimeTicks::Now();
    if (pace_start_.is_null()) {
      pace_start_ = now;
    }
    if (!pace_last_.is_null() && now - pace_last_ < base::Milliseconds(100)) {
      const base::TimeDelta gap = now - pace_last_;
      ++pace_frames_;
      pace_longest_ = std::max(pace_longest_, gap);
      if (gap > base::Milliseconds(25)) {
        ++pace_late_;
      }
      // A present that stood: the frame before it had not been shown yet.
      if (now - before > base::Milliseconds(4)) {
        ++pace_waited_;
      }
    }
    pace_last_ = now;
    if (now - pace_start_ < base::Seconds(10)) {
      return;
    }
    if (pace_frames_ >= 100) {
      NoteF("frames: %d in runs over the last 10 s; %d came more than 25 ms "
            "after the one before, the longest gap %.0f ms; Present waited "
            "over 4 ms for %d",
            pace_frames_, pace_late_, pace_longest_.InMillisecondsF(),
            pace_waited_);
    }
    pace_start_ = now;
    pace_frames_ = pace_late_ = pace_waited_ = 0;
    pace_longest_ = base::TimeDelta();
  }

  static bool DeviceGone(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG ||
           hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
  }

  // Lets go of the chain: out of the panel first, and waited for, so that
  // whatever replaces it is not made beside it. Of the device too if it is
  // gone.
  void DropChain(bool device_too) {
    if (generation_) {
      PanelHost::Get()->DetachToReplace(generation_);
      generation_ = 0;
    }
    gpu_view_.Reset();
    chain_.Reset();
    if (context_) {
      context_->ClearState();
      context_->Flush();
    }
    if (device_too) {
      context_.Reset();
      device_.Reset();
      // What viz made on it is viz's to drop: it sees the number gone.
      device_generation_ = 0;
    }
  }

  // |what| failed: no chain for now, and the bitmap meanwhile. Asked for
  // again after a wait that doubles -- for as long as it takes when it was
  // memory that was refused, which comes and goes on a phone; anything else
  // kStrikes times in a row, and then no more.
  bool Later(const char* what, HRESULT hr) {
    HRESULT reason = S_OK;
    if (device_) {
      reason = device_->GetDeviceRemovedReason();
    }
    DropChain(FAILED(reason) || DeviceGone(hr));
    // If it was a chain being replaced, nothing came of it.
    PanelHost::Get()->NotReplaced();
    if (hr != E_OUTOFMEMORY && ++strikes_ >= kStrikes) {
      NoteF("d3d11: %s failed, 0x%08lx (device 0x%08lx): back to the bitmap",
            what, static_cast<unsigned long>(hr),
            static_cast<unsigned long>(reason));
      failed_ = true;
      Release();
      return false;
    }
    backoff_ = backoff_.is_zero() ? kFirstBackoff
                                  : std::min(backoff_ * 2, kMaxBackoff);
    retry_at_ = base::TimeTicks::Now() + backoff_;
    NoteF("d3d11: %s failed, 0x%08lx (device 0x%08lx): the bitmap for now, "
          "next try in %d s",
          what, static_cast<unsigned long>(hr),
          static_cast<unsigned long>(reason),
          static_cast<int>(backoff_.InSeconds()));
    return false;
  }

  const int buffers_;
  // Viz may draw frames into the back buffer itself, until that is given
  // up; the device is asked for at feature level 9_3 and no better.
  bool compositor_;
  const bool level_9_3_;
  // Of a frame viz is drawing: between BeginGpuFrame and its end.
  ComPtr<ID3D11RenderTargetView> gpu_view_;
  // |device_|'s number among the run's devices; 0 without one.
  uint32_t device_generation_ = 0;
  // What the panel has been told the chain shows.
  PresentKind kind_ = PresentKind::kD3D11;
  bool gpu_presented_ = false;
  // Frames viz drew that could not be presented, in a row.
  int gpu_strikes_ = 0;
  // A view of the back buffer that memory was refused for is asked for
  // again after a wait that doubles; the chain stays. No frame is begun
  // before |gpu_retry_at_|, which Trim() sets too.
  base::TimeDelta gpu_backoff_;
  base::TimeTicks gpu_retry_at_;
  bool trimmed_ = false;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGISwapChain1> chain_;
  uint32_t generation_ = 0;
  gfx::Size size_;
  // The damage of the frames each other buffer has not seen, newest first.
  std::deque<gfx::Rect> history_;
  bool presented_ = false;
  bool failed_ = false;
  // Failures in a row that were not for want of memory; the wait before a
  // chain that could not be had is asked for again, and when it is over.
  int strikes_ = 0;
  base::TimeDelta backoff_;
  base::TimeTicks retry_at_;
  // Buffers of the chain that have not had a whole frame yet.
  int blank_buffers_ = 0;
  // NotePace's ten seconds.
  base::TimeTicks pace_start_;
  base::TimeTicks pace_last_;
  base::TimeDelta pace_longest_;
  int pace_frames_ = 0;
  int pace_late_ = 0;
  int pace_waited_ = 0;
};

class VwSoftwareOutputDevice;

// The output device, for ReleaseGraphicsMemory(): the thread it lives on
// (viz's) and itself. The device is made and destroyed on that thread, and
// only used on it.
struct TheDevice {
  static TheDevice* Get() {
    static base::NoDestructor<TheDevice> the_device;
    return the_device.get();
  }

  base::Lock lock;
  scoped_refptr<base::SequencedTaskRunner> runner;
  raw_ptr<VwSoftwareOutputDevice> device = nullptr;
};

// The base class keeps an N32 raster surface the compositor draws into; on
// Windows N32 is BGRA, which is what both a WriteableBitmap and the swap
// chain hold, so a frame is a plain row copy either way.
//
// With the Direct3D 11 compositor it is also what viz's renderer draws its
// own frames through (viz::W10mD3D11Target): the target may go for good in
// the middle of a run, and this stays to say so.
class VwSoftwareOutputDevice : public viz::SoftwareOutputDevice,
                               public viz::W10mD3D11Target {
 public:
  VwSoftwareOutputDevice() {
    const EngineConfig config = VwEngine::Get()->config();
    if (config.hardware_present && PanelHost::Get()->has_panel()) {
      // Given up earlier in the run (by a device before this one) is given
      // up for the run.
      compositor_ =
          config.d3d_compositing && !VwEngine::Get()->compositor_failed();
      target_ = std::make_unique<D3D11Target>(
          config.present_buffers, compositor_, config.d3d_level_9_3);
    }
    // IDXGIDevice3::Trim is new to every driver here, and one that cannot
    // take it would end the app at every suspension with nothing
    // remembered: "--vw-trim=off" among the engine switches leaves it out.
    trim_driver_ =
        config.switches.find("--vw-trim=off") == std::string::npos;
    TheDevice* the_device = TheDevice::Get();
    base::AutoLock hold(the_device->lock);
    the_device->runner = task_runner_;
    the_device->device = this;
  }
  ~VwSoftwareOutputDevice() override {
    TheDevice* the_device = TheDevice::Get();
    base::AutoLock hold(the_device->lock);
    if (the_device->device == this) {
      the_device->device = nullptr;
    }
  }

  // Viz thread, between frames: see ReleaseGraphicsMemory().
  void ReleaseMemory(bool trim) {
    if (d3d11_client_) {
      d3d11_client_->ReleaseD3D11Textures();
    }
    if (trim && target_) {
      target_->Trim(trim_driver_);
    }
  }

  // viz::SoftwareOutputDevice: null leaves viz with its plain software
  // renderer, as in every run without the compositor.
  viz::W10mD3D11Target* GetW10mD3D11Target() override {
    return compositor_ ? this : nullptr;
  }

  // viz::W10mD3D11Target:
  void SetD3D11Client(Client* client) override { d3d11_client_ = client; }
  Begin BeginGpuFrame(const gfx::Size& size,
                      ID3D11RenderTargetView** view) override {
    *view = nullptr;
    if (!target_) {
      return Begin::kNever;
    }
    const Begin begin = target_->BeginGpuFrame(size, view);
    if (begin != Begin::kReady) {
      // No frame of the compositor's is in the driver (vw_present_guard.h).
      PresentGuard::Get()->Left(PresentKind::kCompositor);
    }
    DropTargetIfFailed();
    return begin;
  }
  ID3D11Device* GetD3D11Device() override {
    return target_ ? target_->device() : nullptr;
  }
  ID3D11DeviceContext* GetD3D11Context() override {
    return target_ ? target_->context() : nullptr;
  }
  uint32_t GetD3D11DeviceGeneration() override {
    return target_ ? target_->device_generation() : 0;
  }
  bool PresentGpuFrame() override {
    if (!target_) {
      return false;
    }
    const bool presented = target_->PresentGpuFrame();
    // The driver has answered for the whole frame, its present included.
    PresentGuard::Get()->Left(PresentKind::kCompositor);
    if (!presented) {
      DropTargetIfFailed();
      return false;
    }
    if (to_bitmap_) {
      // Back in the panel, as in EndPaint.
      to_bitmap_ = false;
      FrameBuffer::Get()->Reset();
    }
    return true;
  }
  void AbandonGpuFrame() override {
    if (target_) {
      target_->AbandonGpuFrame();
      PresentGuard::Get()->Left(PresentKind::kCompositor);
      DropTargetIfFailed();
    }
  }
  void OnGpuFramesGivenUp(const char* reason, bool for_good) override {
    if (target_) {
      target_->StopCompositor(reason, for_good);
    } else {
      VwEngine::Get()->CompositorFailed(reason, for_good);
    }
  }

  void Resize(const gfx::Size& pixel_size, float scale_factor) override {
    if (pixel_size != viewport_pixel_size_) {
      NoteF("frames: output %dx%d at scale %.2f", pixel_size.width(),
            pixel_size.height(), scale_factor);
    }
    viz::SoftwareOutputDevice::Resize(pixel_size, scale_factor);
    if (target_) {
      // No chain of this size now is not the end of it: EndPaint asks again
      // with every frame, and publishes to the bitmap meanwhile.
      target_->EnsureSize(pixel_size);
      DropTargetIfFailed();
    }
  }

  void EndPaint() override {
    // The end of a frame the CPU drew. If the compositor had begun it and
    // left it to the CPU, all it asked of the driver has been answered and
    // the frame is out: a run suspended from here did not die of that
    // (vw_present_guard.h).
    struct Leave {
      ~Leave() {
        if (compositor) {
          PresentGuard::Get()->Left(PresentKind::kCompositor);
        }
      }
      bool compositor;
    } leave{compositor_ && !VwEngine::Get()->compositor_failed()};
    if (!surface_) {
      return;
    }
    SkPixmap pixmap;
    if (!surface_->peekPixels(&pixmap)) {
      return;
    }
    gfx::Rect damage = damage_rect_;
    damage.Intersect(gfx::Rect(viewport_pixel_size_));
    if (target_) {
      if (target_->EnsureSize(viewport_pixel_size_) &&
          target_->Present(pixmap, damage)) {
        if (to_bitmap_) {
          // Back in the panel: the shell has stopped copying, and the
          // bitmap's pixels are memory again.
          to_bitmap_ = false;
          FrameBuffer::Get()->Reset();
        }
        return;
      }
      DropTargetIfFailed();
      if (!to_bitmap_) {
        // The shell shows the bitmap again: a whole frame, not just damage
        // (after Reset the next Publish is one).
        to_bitmap_ = true;
        FrameBuffer::Get()->Reset();
      }
    }
    FrameBuffer::Get()->Publish(pixmap, damage);
  }

 private:
  void DropTargetIfFailed() {
    if (target_ && target_->failed()) {
      target_.reset();
      if (compositor_) {
        // Not its doing, and nothing of its to remember: there is no swap
        // chain for it to draw into.
        VwEngine::Get()->CompositorFailed("no swap chain can be had",
                                          /*for_good=*/false);
      }
    }
  }

  std::unique_ptr<D3D11Target> target_;
  // This run composites with Direct3D 11 where it can (EngineConfig).
  bool compositor_ = false;
  // The driver is asked to trim when the app is suspended.
  bool trim_driver_ = true;
  // Viz's renderer, while there is one that draws with Direct3D 11.
  raw_ptr<viz::W10mD3D11Target::Client> d3d11_client_ = nullptr;
  // Frames are going to the bitmap although there is, or was, a target.
  bool to_bitmap_ = false;
};

// Chromium's GPU side giving GL up (ui/gl/w10m_panel.h): the GPU thread or
// viz's.
void GpuGivenUp(const char* reason, bool for_good) {
  VwEngine::Get()->GpuCompositingFailed(reason, for_good);
}

std::unique_ptr<viz::SoftwareOutputDevice> MakeDevice(gpu::SurfaceHandle) {
  // In a run that set out to composite on the GPU, viz asks for a software
  // surface only once the browser has turned GPU compositing off, whatever
  // told it to. Whoever knew why has said so already; this is the net under
  // them, and it is here before these frames can count as a success. Said
  // through Chromium's own call (it comes back to GpuGivenUp above), which
  // its hardware video decoder goes by too: its frames are textures, and
  // nobody is left to draw them.
  if (VwEngine::Get()->config().gpu_compositing) {
    gl::W10mGpuGivenUp("the browser turned GPU compositing off",
                       /*for_good=*/false);
  }
  return std::make_unique<VwSoftwareOutputDevice>();
}

}  // namespace

// static
FrameBuffer* FrameBuffer::Get() {
  static base::NoDestructor<FrameBuffer> buffer;
  return buffer.get();
}

void FrameBuffer::Publish(const SkPixmap& frame, const gfx::Rect& damage) {
  const int32_t w = frame.width();
  const int32_t h = frame.height();
  if (w <= 0 || h <= 0 || frame.info().bytesPerPixel() != 4) {
    return;
  }
  const size_t row = static_cast<size_t>(w) * 4;
  base::AutoLock hold(lock_);
  gfx::Rect copy = damage;
  if (w != width_ || h != height_) {
    pixels_.assign(row * h, 0);
    width_ = w;
    height_ = h;
    copy = gfx::Rect(w, h);
  }
  copy.Intersect(gfx::Rect(w, h));
  if (copy.IsEmpty()) {
    return;
  }
  const uint8_t* src = static_cast<const uint8_t*>(frame.addr());
  const size_t src_stride = frame.rowBytes();
  const size_t span = static_cast<size_t>(copy.width()) * 4;
  for (int y = copy.y(); y < copy.bottom(); ++y) {
    memcpy(pixels_.data() + y * row + copy.x() * 4,
           src + y * src_stride + copy.x() * 4, span);
  }
  ++serial_;
}

int32_t FrameBuffer::Copy(void* dest,
                          int32_t capacity,
                          int32_t* width,
                          int32_t* height,
                          uint64_t* serial) {
  base::AutoLock hold(lock_);
  const bool shape_matches =
      width && height && *width == width_ && *height == height_;
  const bool newer = !serial || *serial != serial_;
  if (!dest || !shape_matches || !newer || serial_ == 0 ||
      capacity < width_ * height_ * 4) {
    if (width) {
      *width = width_;
    }
    if (height) {
      *height = height_;
    }
    return 0;
  }
  memcpy(dest, pixels_.data(), static_cast<size_t>(width_) * height_ * 4);
  if (serial) {
    *serial = serial_;
  }
  return 1;
}

void FrameBuffer::Reset() {
  base::AutoLock hold(lock_);
  std::vector<uint8_t>().swap(pixels_);
  width_ = 0;
  height_ = 0;
}

uint64_t FrameBuffer::serial() {
  base::AutoLock hold(lock_);
  return serial_;
}

namespace {

void ReleaseOnVizThread(bool trim, base::TimeTicks asked) {
  const base::TimeDelta waited = base::TimeTicks::Now() - asked;
  if (trim && waited > kSuspendWindow) {
    // The app was frozen before this had its turn, and is back: what would
    // be given away is wanted again now.
    NoteF("d3d11: the app was suspended before its graphics memory could be "
          "given back (%d s ago)",
          static_cast<int>(waited.InSeconds()));
    return;
  }
  VwSoftwareOutputDevice* device = nullptr;
  {
    TheDevice* the_device = TheDevice::Get();
    base::AutoLock hold(the_device->lock);
    device = the_device->device;
  }
  // Made, used and destroyed on this thread: it cannot go meanwhile.
  if (device) {
    device->ReleaseMemory(trim);
  }
}

}  // namespace

void ReleaseGraphicsMemory(bool trim) {
  scoped_refptr<base::SequencedTaskRunner> runner;
  {
    TheDevice* the_device = TheDevice::Get();
    base::AutoLock hold(the_device->lock);
    runner = the_device->runner;
  }
  if (runner) {
    runner->PostTask(FROM_HERE, base::BindOnce(&ReleaseOnVizThread, trim,
                                               base::TimeTicks::Now()));
  }
}

void InstallFrameSink() {
  viz::SetW10mSoftwareOutputDeviceFactory(&MakeDevice);
  gl::SetW10mGpuGivenUpHandler(&GpuGivenUp);
}

}  // namespace venetium_w10m
