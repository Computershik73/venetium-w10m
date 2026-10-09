// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_frame_sink.h"

#include <string.h>

#include <algorithm>
#include <memory>

#include "base/no_destructor.h"
#include "components/viz/service/display/software_output_device.h"
#include "components/viz/service/display_embedder/w10m_software_output_device.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// The base class keeps an N32 raster surface the compositor draws into; on
// Windows N32 is BGRA, which is what a WriteableBitmap holds, so a frame is
// a plain row copy.
class VwSoftwareOutputDevice : public viz::SoftwareOutputDevice {
 public:
  VwSoftwareOutputDevice() = default;
  ~VwSoftwareOutputDevice() override = default;

  void Resize(const gfx::Size& pixel_size, float scale_factor) override {
    if (pixel_size != viewport_pixel_size_) {
      NoteF("frames: output %dx%d at scale %.2f", pixel_size.width(),
            pixel_size.height(), scale_factor);
    }
    viz::SoftwareOutputDevice::Resize(pixel_size, scale_factor);
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
    FrameBuffer::Get()->Publish(pixmap, damage);
  }
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

uint64_t FrameBuffer::serial() {
  base::AutoLock hold(lock_);
  return serial_;
}

void InstallFrameSink() {
  viz::SetW10mSoftwareOutputDeviceFactory(&MakeDevice);
}

}  // namespace venetium_w10m
