// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The one profile: cookies, storage, history of the HTTP cache, all under the
// profile directory the shell gave (LocalState\profile). The HTTP cache goes
// to the cache directory (LocalCache), which the phone may clear when it is
// short of space -- the right place for it.

#ifndef VENETIUM_W10M_BROWSER_VW_BROWSER_CONTEXT_H_
#define VENETIUM_W10M_BROWSER_VW_BROWSER_CONTEXT_H_

#include <memory>

#include "base/files/file_path.h"
#include "content/public/browser/browser_context.h"

namespace content {
class PermissionControllerDelegate;
}

namespace venetium_w10m {

class VwDownloadDelegate;

class VwBrowserContext : public content::BrowserContext {
 public:
  VwBrowserContext(const base::FilePath& path, const base::FilePath& cache);
  VwBrowserContext(const VwBrowserContext&) = delete;
  VwBrowserContext& operator=(const VwBrowserContext&) = delete;
  ~VwBrowserContext() override;

  const base::FilePath& cache_path() const { return cache_; }

  // content::BrowserContext:
  std::unique_ptr<content::ZoomLevelDelegate> CreateZoomLevelDelegate(
      const base::FilePath& partition_path) override;
  base::FilePath GetPath() const override;
  bool IsOffTheRecord() override;
  content::DownloadManagerDelegate* GetDownloadManagerDelegate() override;
  content::BrowserPluginGuestManager* GetGuestManager() override;
  storage::SpecialStoragePolicy* GetSpecialStoragePolicy() override;
  content::PlatformNotificationService* GetPlatformNotificationService()
      override;
  content::PushMessagingService* GetPushMessagingService() override;
  content::StorageNotificationService* GetStorageNotificationService() override;
  content::SSLHostStateDelegate* GetSSLHostStateDelegate() override;
  content::PermissionControllerDelegate* GetPermissionControllerDelegate()
      override;
  content::ReduceAcceptLanguageControllerDelegate*
  GetReduceAcceptLanguageControllerDelegate() override;
  content::ClientHintsControllerDelegate* GetClientHintsControllerDelegate()
      override;
  content::BackgroundFetchDelegate* GetBackgroundFetchDelegate() override;
  content::BackgroundSyncController* GetBackgroundSyncController() override;
  content::BrowsingDataRemoverDelegate* GetBrowsingDataRemoverDelegate()
      override;

 private:
  const base::FilePath path_;
  const base::FilePath cache_;
  std::unique_ptr<content::PermissionControllerDelegate> permissions_;
  std::unique_ptr<VwDownloadDelegate> downloads_;
  // The DownloadManager exists (it asks for its delegate when it is made).
  bool download_manager_created_ = false;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_BROWSER_CONTEXT_H_
