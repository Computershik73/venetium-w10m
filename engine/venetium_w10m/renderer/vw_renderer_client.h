// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#ifndef VENETIUM_W10M_RENDERER_VW_RENDERER_CLIENT_H_
#define VENETIUM_W10M_RENDERER_VW_RENDERER_CLIENT_H_

#include <string>

#include "content/public/renderer/content_renderer_client.h"

namespace venetium_w10m {

// The renderer runs on a thread of the same process (single-process mode);
// what it needs from the embedder is little more than an error page.
class VwContentRendererClient : public content::ContentRendererClient {
 public:
  VwContentRendererClient();
  VwContentRendererClient(const VwContentRendererClient&) = delete;
  VwContentRendererClient& operator=(const VwContentRendererClient&) = delete;
  ~VwContentRendererClient() override;

  // content::ContentRendererClient:
  void PrepareErrorPage(content::RenderFrame* render_frame,
                        const blink::WebURLError& error,
                        const std::string& http_method,
                        content::mojom::AlternativeErrorPageOverrideInfoPtr
                            alternative_error_page_info,
                        std::string* error_html) override;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_RENDERER_VW_RENDERER_CLIENT_H_
