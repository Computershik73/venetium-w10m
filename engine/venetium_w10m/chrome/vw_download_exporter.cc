// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_download_exporter.h"

#include <string>

#include "base/files/file_path.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

// static
void VwDownloadExporter::Watch(content::DownloadManager* manager) {
  if (manager) {
    new VwDownloadExporter(manager);
  }
}

VwDownloadExporter::VwDownloadExporter(content::DownloadManager* manager)
    : manager_(manager) {
  manager_->AddObserver(this);
}

VwDownloadExporter::~VwDownloadExporter() {
  for (download::DownloadItem* item : watched_) {
    item->RemoveObserver(this);
  }
  if (manager_) {
    manager_->RemoveObserver(this);
  }
}

void VwDownloadExporter::OnDownloadCreated(content::DownloadManager* manager,
                                           download::DownloadItem* item) {
  if (item->GetState() != download::DownloadItem::IN_PROGRESS) {
    return;
  }
  item->AddObserver(this);
  watched_.insert(item);
}

void VwDownloadExporter::ManagerGoingDown(content::DownloadManager* manager) {
  manager_->RemoveObserver(this);
  manager_ = nullptr;
  delete this;
}

void VwDownloadExporter::OnDownloadUpdated(download::DownloadItem* item) {
  switch (item->GetState()) {
    case download::DownloadItem::IN_PROGRESS:
    case download::DownloadItem::INTERRUPTED:  // may be resumed
      return;
    case download::DownloadItem::CANCELLED:
      break;
    case download::DownloadItem::COMPLETE: {
      const base::FilePath path = item->GetTargetFilePath();
      const std::string utf8 = path.AsUTF8Unsafe();
      NoteF("download: %s finished (%lld bytes)", utf8.c_str(),
            static_cast<long long>(item->GetReceivedBytes()));
      const venetium_w10m_events events = VwEngine::Get()->events();
      if (events.download_done && !path.empty()) {
        events.download_done(utf8.c_str(), item->GetMimeType().c_str());
      }
      break;
    }
    case download::DownloadItem::MAX_DOWNLOAD_STATE:
      return;
  }
  item->RemoveObserver(this);
  watched_.erase(item);
}

void VwDownloadExporter::OnDownloadDestroyed(download::DownloadItem* item) {
  item->RemoveObserver(this);
  watched_.erase(item);
}

}  // namespace venetium_w10m
