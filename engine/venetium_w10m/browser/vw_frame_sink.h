// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Where composited frames go on the software path. Viz draws each frame of
// the browser window into a raster surface (VwSoftwareOutputDevice); at the
// end of the frame the damaged part is copied into the one frame buffer the
// shell reads with venetium_w10m_frame_copy -- on its own thread, once per
// display refresh, into a XAML WriteableBitmap. The same contract as the
// gecko-w10m shell has with Gecko.

#ifndef VENETIUM_W10M_BROWSER_VW_FRAME_SINK_H_
#define VENETIUM_W10M_BROWSER_VW_FRAME_SINK_H_

#include <stdint.h>

#include <vector>

#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "ui/gfx/geometry/rect.h"

class SkPixmap;

namespace venetium_w10m {

class FrameBuffer {
 public:
  static FrameBuffer* Get();

  // Viz thread: |damage| of |frame| changed (pixel coordinates). A frame of
  // another size replaces the buffer whole.
  void Publish(const SkPixmap& frame, const gfx::Rect& damage);

  // Any thread: see venetium_w10m_frame_copy.
  int32_t Copy(void* dest,
               int32_t capacity,
               int32_t* width,
               int32_t* height,
               uint64_t* serial);

  // Frames published so far.
  uint64_t serial();

  // Drops the pixels; the next frame is published whole.
  void Reset();

 private:
  friend class base::NoDestructor<FrameBuffer>;
  FrameBuffer() = default;

  base::Lock lock_;
  std::vector<uint8_t> pixels_;
  int32_t width_ = 0;
  int32_t height_ = 0;
  uint64_t serial_ = 0;
};

// Makes viz give every root surface a VwSoftwareOutputDevice. Before the GPU
// thread starts.
void InstallFrameSink();

// Any thread: memory is wanted back from the graphics side. The Direct3D 11
// compositor lets go of its textures (they are made again when next drawn);
// with |trim|, the app being suspended, the driver is also asked for what it
// keeps for speed. Done on viz's thread, a moment later; nothing if frames
// do not go through Direct3D 11.
void ReleaseGraphicsMemory(bool trim);

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_FRAME_SINK_H_
