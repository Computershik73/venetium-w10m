// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Downloads go straight into a Downloads folder beside the profile, under the
// name the server suggests, with no prompt (a phone has nowhere sensible to
// show a save dialog mid-page). When one finishes, the shell is told the path
// and type (venetium_w10m_events.download_done) and offers to open it or
// moves it to the phone's Downloads library.

#ifndef VENETIUM_W10M_BROWSER_VW_DOWNLOAD_DELEGATE_H_
#define VENETIUM_W10M_BROWSER_VW_DOWNLOAD_DELEGATE_H_

#include <stdint.h>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "components/download/public/common/download_target_info.h"
#include "content/public/browser/download_manager_delegate.h"

namespace venetium_w10m {

class VwDownloadDelegate : public content::DownloadManagerDelegate {
 public:
  explicit VwDownloadDelegate(const base::FilePath& directory);
  VwDownloadDelegate(const VwDownloadDelegate&) = delete;
  VwDownloadDelegate& operator=(const VwDownloadDelegate&) = delete;
  ~VwDownloadDelegate() override;

  // content::DownloadManagerDelegate:
  void Shutdown() override;
  bool DetermineDownloadTarget(
      download::DownloadItem* download,
      download::DownloadTargetCallback* callback) override;
  bool ShouldOpenDownload(download::DownloadItem* item,
                          DownloadOpenDelayedCallback callback) override;
  void GetNextId(content::DownloadIdCallback callback) override;

 private:
  void OnPathChosen(download::DownloadItem* item,
                    download::DownloadTargetCallback callback,
                    const base::FilePath& path);

  const base::FilePath directory_;
  uint32_t next_id_ = 1;
  base::WeakPtrFactory<VwDownloadDelegate> weak_factory_{this};
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_DOWNLOAD_DELEGATE_H_
