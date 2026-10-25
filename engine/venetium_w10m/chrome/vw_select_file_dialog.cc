// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_select_file_dialog.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/common/chrome_paths.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "venetium_w10m/browser/vw_browser.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// Where a file to save goes: the app's Downloads folder, under |suggested|'s
// name, made unique as a download's would be. On a thread that may block.
std::vector<base::FilePath> SavePath(const base::FilePath& suggested) {
  base::FilePath downloads;
  base::PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS, &downloads);
  base::FilePath name = suggested.BaseName();
  if (name.empty() || name.value() == FILE_PATH_LITERAL(".")) {
    name = base::FilePath(FILE_PATH_LITERAL("download"));
  }
  base::CreateDirectory(downloads);
  base::FilePath path = downloads.Append(name);
  for (int i = 1; base::PathExists(path) && i < 100; ++i) {
    path = downloads.Append(name).InsertBeforeExtensionASCII(
        " (" + base::NumberToString(i) + ")");
  }
  NoteF("files: Chrome saves %s", path.AsUTF8Unsafe().c_str());
  return {path};
}

}  // namespace

VwSelectFileDialog::VwSelectFileDialog(
    Listener* listener,
    std::unique_ptr<ui::SelectFilePolicy> policy)
    : ui::SelectFileDialog(listener, std::move(policy)) {}

VwSelectFileDialog::~VwSelectFileDialog() = default;

bool VwSelectFileDialog::IsRunning(gfx::NativeWindow owning_window) const {
  return running_;
}

void VwSelectFileDialog::ListenerDestroyed() {
  listener_ = nullptr;
}

bool VwSelectFileDialog::HasMultipleFileTypeChoicesImpl() {
  return false;
}

void VwSelectFileDialog::SelectFileImpl(
    Type type,
    const std::u16string& title,
    const base::FilePath& default_path,
    const FileTypeInfo* file_types,
    int file_type_index,
    const base::FilePath::StringType& default_extension,
    gfx::NativeWindow owning_window,
    const GURL* caller) {
  // Listeners expect the answer after this returns, never inside it.
  auto answer_later = [this](base::OnceClosure answer) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(answer));
  };
  switch (type) {
    case SELECT_OPEN_FILE:
    case SELECT_OPEN_MULTI_FILE: {
      VwBrowser* browser = VwEngine::Get()->browser();
      if (!browser) {
        answer_later(base::BindOnce(&VwSelectFileDialog::Cancelled,
                                    weak_factory_.GetWeakPtr()));
        return;
      }
      running_ = true;
      const bool multiple = type == SELECT_OPEN_MULTI_FILE;
      browser->dialogs()->PickFiles(
          multiple, base::BindOnce(&VwSelectFileDialog::Picked,
                                   weak_factory_.GetWeakPtr(), multiple));
      return;
    }
    case SELECT_SAVEAS_FILE: {
      running_ = true;
      // The shell's save picker; without a shell, the app's Downloads.
      VwBrowser* browser = VwEngine::Get()->browser();
      if (browser && VwEngine::Get()->events().file_chooser) {
        browser->dialogs()->SaveFile(
            default_path.BaseName().AsUTF8Unsafe(),
            base::BindOnce(&VwSelectFileDialog::Picked,
                           weak_factory_.GetWeakPtr(), false));
        return;
      }
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
          base::BindOnce(&SavePath, default_path),
          base::BindOnce(&VwSelectFileDialog::Picked,
                         weak_factory_.GetWeakPtr(), false));
      return;
    }
    default:
      Note("files: a folder cannot be chosen here");
      answer_later(base::BindOnce(&VwSelectFileDialog::Cancelled,
                                  weak_factory_.GetWeakPtr()));
      return;
  }
}

void VwSelectFileDialog::Picked(bool multiple,
                                std::vector<base::FilePath> paths) {
  running_ = false;
  if (!listener_) {
    return;
  }
  if (paths.empty()) {
    listener_->FileSelectionCanceled();
    return;
  }
  if (multiple) {
    std::vector<ui::SelectedFileInfo> files;
    for (const base::FilePath& path : paths) {
      files.emplace_back(path);
    }
    listener_->MultiFilesSelected(files);
  } else {
    listener_->FileSelected(ui::SelectedFileInfo(paths.front()), 0);
  }
}

void VwSelectFileDialog::Cancelled() {
  running_ = false;
  if (listener_) {
    listener_->FileSelectionCanceled();
  }
}

// static
void VwSelectFileDialogFactory::Install() {
  ui::SelectFileDialog::SetFactory(
      std::make_unique<VwSelectFileDialogFactory>());
}

ui::SelectFileDialog* VwSelectFileDialogFactory::Create(
    ui::SelectFileDialog::Listener* listener,
    std::unique_ptr<ui::SelectFilePolicy> policy) {
  return new VwSelectFileDialog(listener, std::move(policy));
}

}  // namespace venetium_w10m
