// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Downloads the user can find. An app may not write the phone's Downloads
// with Win32 calls (with interop unlock alone): Chrome downloads into the
// app's LocalState\Downloads (vw_chrome_run.cc), and each download that
// finishes is handed to the shell (events.download_done), which copies it
// through the storage API into Downloads\Venetium, or into the file picked in
// a Save As. Opening the download, or showing its folder, then goes to the
// copy (the shell keeps which is whose). Where the app may write Downloads
// itself (root access), Chrome downloads there and the shell only says so.

#ifndef VENETIUM_W10M_CHROME_VW_DOWNLOAD_EXPORTER_H_
#define VENETIUM_W10M_CHROME_VW_DOWNLOAD_EXPORTER_H_

#include <map>

#include "base/memory/raw_ptr.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"

namespace venetium_w10m {

class VwDownloadExporter : public content::DownloadManager::Observer,
                           public download::DownloadItem::Observer {
 public:
  // Watches |manager|'s downloads from now on; deletes itself when the
  // manager goes down.
  static void Watch(content::DownloadManager* manager);

  VwDownloadExporter(const VwDownloadExporter&) = delete;
  VwDownloadExporter& operator=(const VwDownloadExporter&) = delete;

 private:
  explicit VwDownloadExporter(content::DownloadManager* manager);
  ~VwDownloadExporter() override;

  // content::DownloadManager::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void ManagerGoingDown(content::DownloadManager* manager) override;

  // download::DownloadItem::Observer:
  void OnDownloadUpdated(download::DownloadItem* item) override;
  void OnDownloadDestroyed(download::DownloadItem* item) override;

  raw_ptr<content::DownloadManager> manager_;
  // The downloads of this run still going (those loaded from the history
  // finished in an earlier one and were handed over then), with what the log
  // has said of each: its target, its last interruption, its danger.
  struct Said {
    bool target = false;
    int interrupt_reason = 0;
    int danger = 0;
  };
  std::map<download::DownloadItem*, Said> watched_;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_DOWNLOAD_EXPORTER_H_
