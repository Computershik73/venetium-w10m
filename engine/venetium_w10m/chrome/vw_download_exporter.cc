// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_download_exporter.h"

#include <string>

#include "base/files/file_path.h"
#include "components/download/public/common/download_danger_type.h"
#include "components/download/public/common/download_interrupt_reasons.h"
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
  for (auto& [item, said] : watched_) {
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
  watched_[item] = Said();
  NoteF("download: started from %s (%s)",
        std::string(item->GetURL().host()).c_str(),
        item->GetMimeType().c_str());
}

void VwDownloadExporter::ManagerGoingDown(content::DownloadManager* manager) {
  manager_->RemoveObserver(this);
  manager_ = nullptr;
  delete this;
}

void VwDownloadExporter::OnDownloadUpdated(download::DownloadItem* item) {
  Said& said = watched_[item];
  if (!said.target && !item->GetTargetFilePath().empty()) {
    said.target = true;
    NoteF("download: going to %s",
          item->GetTargetFilePath().AsUTF8Unsafe().c_str());
  }
  const int danger = static_cast<int>(item->GetDangerType());
  if (danger != said.danger) {
    said.danger = danger;
    if (item->IsDangerous()) {
      NoteF("download: held as dangerous (%s), waiting to be kept",
            download::GetDownloadDangerTypeString(item->GetDangerType()));
    }
  }
  switch (item->GetState()) {
    case download::DownloadItem::IN_PROGRESS:
      return;
    case download::DownloadItem::INTERRUPTED: {  // may be resumed
      const int reason = static_cast<int>(item->GetLastReason());
      if (reason != said.interrupt_reason) {
        said.interrupt_reason = reason;
        NoteF("download: interrupted, %s (%lld of %lld bytes)",
              download::DownloadInterruptReasonToString(item->GetLastReason())
                  .c_str(),
              static_cast<long long>(item->GetReceivedBytes()),
              static_cast<long long>(item->GetTotalBytes()));
      }
      return;
    }
    case download::DownloadItem::CANCELLED:
      Note("download: cancelled");
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
