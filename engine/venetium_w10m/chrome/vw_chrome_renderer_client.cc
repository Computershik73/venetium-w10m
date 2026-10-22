// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_chrome_renderer_client.h"

#include "venetium_w10m/browser/vw_hang_watch.h"

namespace venetium_w10m {

VwChromeRendererClient::VwChromeRendererClient() = default;
VwChromeRendererClient::~VwChromeRendererClient() = default;

void VwChromeRendererClient::RenderThreadStarted() {
  hang_watch::WatchCurrentThread("renderer");
  ChromeContentRendererClient::RenderThreadStarted();
  engine_.RenderThreadStarted();
}

bool VwChromeRendererClient::IsDecoderSupportedVideoType(
    const media::VideoType& type) {
  return engine_.IsDecoderSupportedVideoType(type);
}

}  // namespace venetium_w10m
