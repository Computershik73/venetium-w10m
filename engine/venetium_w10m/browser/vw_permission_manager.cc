// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_permission_manager.h"

#include <utility>

#include "base/functional/callback.h"
#include "content/public/browser/permission_controller.h"
#include "content/public/browser/permission_request_description.h"
#include "third_party/blink/public/common/permissions/permission_utils.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace venetium_w10m {

namespace {

content::PermissionResult Ask() {
  return content::PermissionResult(blink::mojom::PermissionStatus::ASK);
}

}  // namespace

VwPermissionManager::VwPermissionManager() = default;
VwPermissionManager::~VwPermissionManager() = default;

void VwPermissionManager::ResetPermission(blink::PermissionType permission,
                                          const GURL& requesting_origin,
                                          const GURL& embedding_origin) {}

void VwPermissionManager::RequestPermissionsFromCurrentDocument(
    content::RenderFrameHost* render_frame_host,
    const content::PermissionRequestDescription& request_description,
    base::OnceCallback<void(const std::vector<content::PermissionResult>&)>
        callback) {
  std::vector<content::PermissionResult> result(
      request_description.permissions.size(), Ask());
  std::move(callback).Run(result);
}

blink::mojom::PermissionStatus VwPermissionManager::GetPermissionStatus(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    const GURL& requesting_origin,
    const GURL& embedding_origin) {
  return blink::mojom::PermissionStatus::ASK;
}

content::PermissionResult
VwPermissionManager::GetPermissionResultForOriginWithoutContext(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    const url::Origin& requesting_origin,
    const url::Origin& embedding_origin) {
  return Ask();
}

content::PermissionResult
VwPermissionManager::GetPermissionResultForCurrentDocument(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    content::RenderFrameHost* render_frame_host,
    bool should_include_device_status) {
  return Ask();
}

content::PermissionResult VwPermissionManager::GetPermissionResultForWorker(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    content::RenderProcessHost* render_process_host,
    const GURL& worker_origin) {
  return Ask();
}

content::PermissionResult
VwPermissionManager::GetPermissionResultForEmbeddedRequester(
    const blink::mojom::PermissionDescriptorPtr& permission_descriptor,
    content::RenderFrameHost* render_frame_host,
    const url::Origin& overridden_origin) {
  return Ask();
}

}  // namespace venetium_w10m
