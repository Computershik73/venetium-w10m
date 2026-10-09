// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Permissions. For now every prompt is answered as if the person dismissed
// it -- what //headless does -- so pages keep working and nothing is granted
// behind anyone's back. The shell's prompt (venetium_w10m_events.permission)
// is the next step; camera and microphone go through
// WebContentsDelegate::RequestMediaAccessPermission in vw_tab instead.

#ifndef VENETIUM_W10M_BROWSER_VW_PERMISSION_MANAGER_H_
#define VENETIUM_W10M_BROWSER_VW_PERMISSION_MANAGER_H_

#include <vector>

#include "content/public/browser/permission_controller_delegate.h"
#include "content/public/browser/permission_result.h"

namespace venetium_w10m {

class VwPermissionManager : public content::PermissionControllerDelegate {
 public:
  VwPermissionManager();
  VwPermissionManager(const VwPermissionManager&) = delete;
  VwPermissionManager& operator=(const VwPermissionManager&) = delete;
  ~VwPermissionManager() override;

  // content::PermissionControllerDelegate:
  void ResetPermission(blink::PermissionType permission,
                       const GURL& requesting_origin,
                       const GURL& embedding_origin) override;
  void RequestPermissionsFromCurrentDocument(
      content::RenderFrameHost* render_frame_host,
      const content::PermissionRequestDescription& request_description,
      base::OnceCallback<void(const std::vector<content::PermissionResult>&)>
          callback) override;
  blink::mojom::PermissionStatus GetPermissionStatus(
      const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
      const GURL& requesting_origin,
      const GURL& embedding_origin) override;
  content::PermissionResult GetPermissionResultForOriginWithoutContext(
      const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
      const url::Origin& requesting_origin,
      const url::Origin& embedding_origin) override;
  content::PermissionResult GetPermissionResultForCurrentDocument(
      const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
      content::RenderFrameHost* render_frame_host,
      bool should_include_device_status) override;
  content::PermissionResult GetPermissionResultForWorker(
      const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
      content::RenderProcessHost* render_process_host,
      const GURL& worker_origin) override;
  content::PermissionResult GetPermissionResultForEmbeddedRequester(
      const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
      content::RenderFrameHost* render_frame_host,
      const url::Origin& overridden_origin) override;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_PERMISSION_MANAGER_H_
