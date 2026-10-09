// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_browser_context.h"

#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/zoom_level_delegate.h"
#include "venetium_w10m/browser/vw_download_delegate.h"
#include "venetium_w10m/browser/vw_permission_manager.h"

namespace venetium_w10m {

VwBrowserContext::VwBrowserContext(const base::FilePath& path,
                                   const base::FilePath& cache)
    : path_(path),
      cache_(cache.empty() ? path.Append(FILE_PATH_LITERAL("Cache")) : cache),
      permissions_(std::make_unique<VwPermissionManager>()),
      downloads_(std::make_unique<VwDownloadDelegate>(
          path.DirName().Append(FILE_PATH_LITERAL("Downloads")))) {
  BrowserContextDependencyManager::GetInstance()->MarkBrowserContextLive(this);
}

VwBrowserContext::~VwBrowserContext() {
  NotifyWillBeDestroyed();
  // The DownloadManager outlives this destructor (BrowserContext's own
  // destructor shuts it down), but its delegate does not: shut it down while
  // the delegate is still here. Only if it exists -- asking for it would make
  // one, and with it the storage partition being shut down below.
  if (download_manager_created_) {
    GetDownloadManager()->Shutdown();
  }
  // Single process: the renderer runs on a thread of this process and is
  // shut down with the only browser context there is.
  if (content::RenderProcessHost::run_renderer_in_process()) {
    content::RenderProcessHost::ShutDownInProcessRenderer();
  }
  ShutdownStoragePartitions();
  BrowserContextDependencyManager::GetInstance()->DestroyBrowserContextServices(
      this);
}

std::unique_ptr<content::ZoomLevelDelegate>
VwBrowserContext::CreateZoomLevelDelegate(const base::FilePath&) {
  return nullptr;
}

base::FilePath VwBrowserContext::GetPath() const {
  return path_;
}

bool VwBrowserContext::IsOffTheRecord() {
  return false;
}

content::DownloadManagerDelegate*
VwBrowserContext::GetDownloadManagerDelegate() {
  download_manager_created_ = true;
  return downloads_.get();
}

content::BrowserPluginGuestManager* VwBrowserContext::GetGuestManager() {
  return nullptr;
}

storage::SpecialStoragePolicy* VwBrowserContext::GetSpecialStoragePolicy() {
  return nullptr;
}

content::PlatformNotificationService*
VwBrowserContext::GetPlatformNotificationService() {
  return nullptr;
}

content::PushMessagingService* VwBrowserContext::GetPushMessagingService() {
  return nullptr;
}

content::StorageNotificationService*
VwBrowserContext::GetStorageNotificationService() {
  return nullptr;
}

content::SSLHostStateDelegate* VwBrowserContext::GetSSLHostStateDelegate() {
  return nullptr;
}

content::PermissionControllerDelegate*
VwBrowserContext::GetPermissionControllerDelegate() {
  return permissions_.get();
}

content::ReduceAcceptLanguageControllerDelegate*
VwBrowserContext::GetReduceAcceptLanguageControllerDelegate() {
  return nullptr;
}

content::ClientHintsControllerDelegate*
VwBrowserContext::GetClientHintsControllerDelegate() {
  return nullptr;
}

content::BackgroundFetchDelegate*
VwBrowserContext::GetBackgroundFetchDelegate() {
  return nullptr;
}

content::BackgroundSyncController*
VwBrowserContext::GetBackgroundSyncController() {
  return nullptr;
}

content::BrowsingDataRemoverDelegate*
VwBrowserContext::GetBrowsingDataRemoverDelegate() {
  return nullptr;
}

}  // namespace venetium_w10m
