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

#include "base/no_destructor.h"
#include "components/viz/service/display/software_output_device.h"
#include "components/viz/service/display_embedder/w10m_software_output_device.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_panel.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

using Microsoft::WRL::ComPtr;

// Software frames presented by the GPU: a Direct3D 11 swap chain made for
// composition and shown by the shell's SwapChainPanel. Only what changed is
// uploaded; the system compositor shows it, in step with the display. Works
// at every feature level from 9_3 up, and is what everything faster falls
// back to. Its swap chain is gecko-w10m's proven one: BGRA, flip-sequential,
// stretched, opaque.
class D3D11Target {
 public:
  explicit D3D11Target(int buffers) : buffers_(std::clamp(buffers, 2, 3)) {}
  D3D11Target(const D3D11Target&) = delete;
  D3D11Target& operator=(const D3D11Target&) = delete;
  ~D3D11Target() { Release(); }

  bool failed() const { return failed_; }

  // Viz thread: a chain of |size| in the panel. False if there cannot be
  // one, for good (the caller falls back to the bitmap).
  bool EnsureSize(const gfx::Size& size) {
    if (failed_ || size.IsEmpty()) {
      return false;
    }
    if (chain_ && size == size_) {
      return true;
    }
    HRESULT hr = S_OK;
    if (!device_ && !CreateDevice()) {
      return Fail("no Direct3D 11 device", E_FAIL);
    }
    if (chain_) {
      hr = chain_->ResizeBuffers(0, size.width(), size.height(),
                                 DXGI_FORMAT_UNKNOWN, 0);
      if (FAILED(hr)) {
        return Fail("ResizeBuffers", hr);
      }
    } else {
      ComPtr<IDXGIDevice1> dxgi_device;
      ComPtr<IDXGIAdapter> adapter;
      ComPtr<IDXGIFactory2> factory;
      hr = device_.As(&dxgi_device);
      if (SUCCEEDED(hr)) hr = dxgi_device->GetAdapter(&adapter);
      if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
      if (FAILED(hr)) {
        return Fail("no DXGI factory", hr);
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
        return Fail("CreateSwapChainForComposition", hr);
      }
      dxgi_device->SetMaximumFrameLatency(1);
      generation_ = PanelHost::Get()->Attach(chain_.Get(), PresentKind::kD3D11);
      if (!generation_) {
        return Fail("the panel did not take the swap chain", E_FAIL);
      }
    }
    size_ = size;
    // Every buffer holds nothing yet: whole frames until each has had one.
    history_.assign(buffers_ - 1, gfx::Rect(size));
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
    upload.Intersect(gfx::Rect(size_));
    history_.push_front(damage);
    history_.resize(buffers_ - 1);
    if (!upload.IsEmpty()) {
      ComPtr<ID3D11Texture2D> back;
      HRESULT hr = chain_->GetBuffer(0, IID_PPV_ARGS(&back));
      if (FAILED(hr)) {
        return Fail("GetBuffer", hr);
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
    const HRESULT hr = chain_->Present(1, 0);
    if (FAILED(hr)) {
      return Fail("Present", hr);
    }
    if (!presented_) {
      presented_ = true;
      VwEngine::Get()->HardwarePresentWorked();
    }
    PanelHost::Get()->Presented(generation_);
    return true;
  }

  void Release() {
    if (generation_) {
      PanelHost::Get()->Detach(generation_);
      generation_ = 0;
    }
    chain_.Reset();
    context_.Reset();
    device_.Reset();
  }

 private:
  bool CreateDevice() {
    static const D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3};
    D3D_FEATURE_LEVEL level = {};
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
    NoteF("d3d11: device at feature level 0x%x", static_cast<unsigned>(level));
    return true;
  }

  bool Fail(const char* what, HRESULT hr) {
    HRESULT reason = S_OK;
    if (device_) {
      reason = device_->GetDeviceRemovedReason();
    }
    NoteF("d3d11: %s failed, 0x%08lx (device 0x%08lx): back to the bitmap",
          what, static_cast<unsigned long>(hr),
          static_cast<unsigned long>(reason));
    failed_ = true;
    Release();
    return false;
  }

  const int buffers_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGISwapChain1> chain_;
  uint32_t generation_ = 0;
  gfx::Size size_;
  // The damage of the frames each other buffer has not seen, newest first.
  std::deque<gfx::Rect> history_;
  bool presented_ = false;
  bool failed_ = false;
};

// The base class keeps an N32 raster surface the compositor draws into; on
// Windows N32 is BGRA, which is what both a WriteableBitmap and the swap
// chain hold, so a frame is a plain row copy either way.
class VwSoftwareOutputDevice : public viz::SoftwareOutputDevice {
 public:
  VwSoftwareOutputDevice() {
    const EngineConfig config = VwEngine::Get()->config();
    if (config.hardware_present && PanelHost::Get()->has_panel()) {
      target_ = std::make_unique<D3D11Target>(config.present_buffers);
    }
  }
  ~VwSoftwareOutputDevice() override = default;

  void Resize(const gfx::Size& pixel_size, float scale_factor) override {
    if (pixel_size != viewport_pixel_size_) {
      NoteF("frames: output %dx%d at scale %.2f", pixel_size.width(),
            pixel_size.height(), scale_factor);
    }
    viz::SoftwareOutputDevice::Resize(pixel_size, scale_factor);
    if (target_ && !target_->EnsureSize(pixel_size)) {
      FallBack();
    }
  }

  void EndPaint() override {
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
      if (target_->Present(pixmap, damage)) {
        return;
      }
      FallBack();
      damage = gfx::Rect(viewport_pixel_size_);
    }
    FrameBuffer::Get()->Publish(pixmap, damage);
  }

 private:
  void FallBack() {
    target_.reset();
    // The shell shows the bitmap again: a whole frame, not just damage.
    FrameBuffer::Get()->Reset();
  }

  std::unique_ptr<D3D11Target> target_;
};

std::unique_ptr<viz::SoftwareOutputDevice> MakeDevice(gpu::SurfaceHandle) {
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

void InstallFrameSink() {
  viz::SetW10mSoftwareOutputDeviceFactory(&MakeDevice);
}

}  // namespace venetium_w10m
