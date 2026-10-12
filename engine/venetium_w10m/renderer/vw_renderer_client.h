// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#ifndef VENETIUM_W10M_RENDERER_VW_RENDERER_CLIENT_H_
#define VENETIUM_W10M_RENDERER_VW_RENDERER_CLIENT_H_

#include <string>

#include "base/functional/callback.h"
#include "content/public/renderer/content_renderer_client.h"

namespace venetium_w10m {

// The renderer runs on a thread of the same process (single-process mode);
// what it needs from the embedder is an error page, and which video codecs
// pages are told it plays.
class VwContentRendererClient : public content::ContentRendererClient {
 public:
  VwContentRendererClient();
  VwContentRendererClient(const VwContentRendererClient&) = delete;
  VwContentRendererClient& operator=(const VwContentRendererClient&) = delete;
  ~VwContentRendererClient() override;

  // content::ContentRendererClient:
  void RenderThreadStarted() override;
  void PrepareErrorPage(content::RenderFrame* render_frame,
                        const blink::WebURLError& error,
                        const std::string& http_method,
                        content::mojom::AlternativeErrorPageOverrideInfoPtr
                            alternative_error_page_info,
                        std::string* error_html) override;
  bool IsDecoderSupportedVideoType(const media::VideoType& type) override;
};

// Any thread. Has the renderer's thread collect the JavaScript heaps in
// full, at once, and then runs |done| on the calling sequence with the
// milliseconds that took. False, and |done| is dropped, while there is no
// renderer thread yet.
bool CollectRendererGarbage(base::OnceCallback<void(int)> done);

// Any thread. How many background tabs the browser could unload now (0 with
// --vw-discard=off). The JavaScript heap's limit is raised when it is
// reached only while this is not 0: a higher limit frees nothing by itself.
void SetUnloadableTabs(int count);

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_RENDERER_VW_RENDERER_CLIENT_H_
