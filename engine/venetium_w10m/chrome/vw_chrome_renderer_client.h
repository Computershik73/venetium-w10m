// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Chrome's renderer client with the engine's: the video codec policy, and
// the renderer thread's memory hooks (vw_renderer_client.cc).

#ifndef VENETIUM_W10M_CHROME_VW_CHROME_RENDERER_CLIENT_H_
#define VENETIUM_W10M_CHROME_VW_CHROME_RENDERER_CLIENT_H_

#include "chrome/renderer/chrome_content_renderer_client.h"
#include "venetium_w10m/renderer/vw_renderer_client.h"

namespace venetium_w10m {

class VwChromeRendererClient : public ChromeContentRendererClient {
 public:
  VwChromeRendererClient();
  VwChromeRendererClient(const VwChromeRendererClient&) = delete;
  VwChromeRendererClient& operator=(const VwChromeRendererClient&) = delete;
  ~VwChromeRendererClient() override;

  // ChromeContentRendererClient:
  void RenderThreadStarted() override;
  bool IsDecoderSupportedVideoType(const media::VideoType& type) override;

 private:
  // What the engine's own renderer client does, asked for the parts above.
  VwContentRendererClient engine_;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_CHROME_RENDERER_CLIENT_H_
