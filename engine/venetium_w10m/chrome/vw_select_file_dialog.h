// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Chrome's file dialogs on the phone. On the desktop they are the shell's
// IFileDialog, run in a utility service; neither exists here. A file to
// open comes from the shell's picker (VwDialogs::PickFiles), copied into the
// app's own folders, which are all the engine's file calls can reach. A
// file to save goes into the app's Downloads folder under the name Chrome
// suggests: nowhere else is the app's to write. Folders cannot be chosen.

#ifndef VENETIUM_W10M_CHROME_VW_SELECT_FILE_DIALOG_H_
#define VENETIUM_W10M_CHROME_VW_SELECT_FILE_DIALOG_H_

#include <memory>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_dialog_factory.h"

namespace venetium_w10m {

class VwSelectFileDialog : public ui::SelectFileDialog {
 public:
  VwSelectFileDialog(Listener* listener,
                     std::unique_ptr<ui::SelectFilePolicy> policy);
  VwSelectFileDialog(const VwSelectFileDialog&) = delete;
  VwSelectFileDialog& operator=(const VwSelectFileDialog&) = delete;

  // ui::SelectFileDialog:
  bool IsRunning(gfx::NativeWindow owning_window) const override;
  void ListenerDestroyed() override;

 protected:
  // ui::SelectFileDialog:
  void SelectFileImpl(Type type,
                      const std::u16string& title,
                      const base::FilePath& default_path,
                      const FileTypeInfo* file_types,
                      int file_type_index,
                      const base::FilePath::StringType& default_extension,
                      gfx::NativeWindow owning_window,
                      const GURL* caller) override;
  bool HasMultipleFileTypeChoicesImpl() override;

 private:
  ~VwSelectFileDialog() override;

  void Picked(bool multiple, std::vector<base::FilePath> paths);
  void Cancelled();

  bool running_ = false;
  base::WeakPtrFactory<VwSelectFileDialog> weak_factory_{this};
};

class VwSelectFileDialogFactory : public ui::SelectFileDialogFactory {
 public:
  // Replaces Chrome's (ChromeSelectFileDialogFactory). UI thread.
  static void Install();

  // ui::SelectFileDialogFactory:
  ui::SelectFileDialog* Create(
      ui::SelectFileDialog::Listener* listener,
      std::unique_ptr<ui::SelectFilePolicy> policy) override;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_SELECT_FILE_DIALOG_H_
