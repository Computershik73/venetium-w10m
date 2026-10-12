// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_download_delegate.h"

#include <set>
#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_item_utils.h"
#include "net/base/filename_util.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// The downloads coming in now, from being given a path to being over.
// UI thread.
std::set<download::DownloadItem*>& ComingIn() {
  static base::NoDestructor<std::set<download::DownloadItem*>> items;
  return *items;
}

// Tells the shell when one download is over, then goes away.
class CompletionObserver : public download::DownloadItem::Observer {
 public:
  static void Attach(download::DownloadItem* item) {
    ComingIn().insert(item);
    item->AddObserver(new CompletionObserver());
  }

  void OnDownloadUpdated(download::DownloadItem* item) override {
    switch (item->GetState()) {
      case download::DownloadItem::COMPLETE: {
        const std::string path = base::WideToUTF8(item->GetTargetFilePath().value());
        NoteF("download: done, %s (%s)", path, item->GetMimeType());
        if (VwEngine* engine = VwEngine::Get()) {
          venetium_w10m_events events = engine->events();
          if (events.download_done) {
            events.download_done(path.c_str(), item->GetMimeType().c_str());
          }
        }
        Detach(item);
        break;
      }
      case download::DownloadItem::CANCELLED:
      case download::DownloadItem::INTERRUPTED:
        NoteF("download: stopped, state %d, reason %d",
              static_cast<int>(item->GetState()),
              static_cast<int>(item->GetLastReason()));
        Detach(item);
        break;
      default:
        break;
    }
  }

  void OnDownloadDestroyed(download::DownloadItem* item) override {
    Detach(item);
  }

 private:
  void Detach(download::DownloadItem* item) {
    ComingIn().erase(item);
    item->RemoveObserver(this);
    delete this;
  }
};

base::FilePath ChoosePath(const GURL& url,
                          const std::string& content_disposition,
                          const std::string& suggested_filename,
                          const std::string& mime_type,
                          const base::FilePath& directory) {
  base::FilePath name =
      net::GenerateFileName(url, content_disposition, std::string(),
                            suggested_filename, mime_type, "download");
  if (!base::PathExists(directory)) {
    base::CreateDirectory(directory);
  }
  base::FilePath path = directory.Append(name);
  // Never over an earlier download of the same name.
  for (int i = 1; base::PathExists(path) && i < 1000; ++i) {
    path = directory.Append(name.InsertBeforeExtensionASCII(
        " (" + std::to_string(i) + ")"));
  }
  return path;
}

}  // namespace

bool DownloadComingInFrom(content::WebContents* contents) {
  for (download::DownloadItem* item : ComingIn()) {
    // The WebContents the download began in, even if that has gone on to
    // another page since.
    if (content::DownloadItemUtils::GetOriginalWebContents(item) == contents) {
      return true;
    }
  }
  return false;
}

VwDownloadDelegate::VwDownloadDelegate(const base::FilePath& directory)
    : directory_(directory) {}

VwDownloadDelegate::~VwDownloadDelegate() = default;

void VwDownloadDelegate::Shutdown() {
  weak_factory_.InvalidateWeakPtrs();
}

bool VwDownloadDelegate::DetermineDownloadTarget(
    download::DownloadItem* download,
    download::DownloadTargetCallback* callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!download->GetForcedFilePath().empty()) {
    download::DownloadTargetInfo target_info;
    target_info.target_path = download->GetForcedFilePath();
    target_info.intermediate_path = download->GetForcedFilePath();
    std::move(*callback).Run(std::move(target_info));
    return true;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN,
       base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ChoosePath, download->GetURL(),
                     download->GetContentDisposition(),
                     download->GetSuggestedFilename(),
                     download->GetMimeType(), directory_),
      base::BindOnce(&VwDownloadDelegate::OnPathChosen,
                     weak_factory_.GetWeakPtr(), download,
                     std::move(*callback)));
  return true;
}

void VwDownloadDelegate::OnPathChosen(download::DownloadItem* item,
                                      download::DownloadTargetCallback callback,
                                      const base::FilePath& path) {
  NoteF("download: %s -> %s", item->GetURL().spec(),
        base::WideToUTF8(path.value()));
  CompletionObserver::Attach(item);
  download::DownloadTargetInfo target_info;
  target_info.target_path = path;
  target_info.intermediate_path =
      path.AddExtension(FILE_PATH_LITERAL(".crdownload"));
  std::move(callback).Run(std::move(target_info));
}

bool VwDownloadDelegate::ShouldOpenDownload(
    download::DownloadItem* item,
    content::DownloadOpenDelayedCallback callback) {
  return true;
}

void VwDownloadDelegate::GetNextId(content::DownloadIdCallback callback) {
  std::move(callback).Run(next_id_++);
}

}  // namespace venetium_w10m
