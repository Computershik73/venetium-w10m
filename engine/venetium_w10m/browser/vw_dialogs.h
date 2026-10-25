// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// What a page asks a person: alert/confirm/prompt, "leave this page?", and
// <input type=file>. Each request gets an id and goes to the shell as an
// event; the shell shows its own XAML dialog or file picker and answers
// through venetium_w10m_dialog_reply / venetium_w10m_file_reply. With no shell
// listening, dialogs are answered at once (alert: OK, confirm: cancel,
// prompt: cancel, leaving: allowed) so a page never hangs on one.

#ifndef VENETIUM_W10M_BROWSER_VW_DIALOGS_H_
#define VENETIUM_W10M_BROWSER_VW_DIALOGS_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "third_party/blink/public/mojom/choosers/file_chooser.mojom.h"

namespace content {
class FileSelectListener;
class WebContents;
}  // namespace content

namespace venetium_w10m {

class VwDialogs : public content::JavaScriptDialogManager {
 public:
  VwDialogs();
  VwDialogs(const VwDialogs&) = delete;
  VwDialogs& operator=(const VwDialogs&) = delete;
  ~VwDialogs() override;

  // content::JavaScriptDialogManager:
  void RunJavaScriptDialog(content::WebContents* web_contents,
                           content::RenderFrameHost* render_frame_host,
                           content::JavaScriptDialogType dialog_type,
                           const std::u16string& message_text,
                           const std::u16string& default_prompt_text,
                           DialogClosedCallback callback,
                           bool* did_suppress_message) override;
  void RunBeforeUnloadDialog(content::WebContents* web_contents,
                             content::RenderFrameHost* render_frame_host,
                             bool is_reload,
                             DialogClosedCallback callback) override;
  void CancelDialogs(content::WebContents* web_contents,
                     bool reset_state) override;

  void Reply(int32_t id, bool accepted, const std::string& text);

  // <input type=file>.
  void ChooseFiles(content::WebContents* web_contents,
                   scoped_refptr<content::FileSelectListener> listener,
                   const blink::mojom::FileChooserParams& params);
  // paths: UTF-8, separated by '\n'; empty cancels.
  void FilesChosen(int32_t id, const std::string& paths);

  // A file dialog of Chrome's interface (ui::SelectFileDialog): the shell's
  // picker, which copies what is picked into the app's own folders.
  // |chosen| hears the copies' paths, none when cancelled or with no shell.
  using PickedCallback =
      base::OnceCallback<void(std::vector<base::FilePath> paths)>;
  void PickFiles(bool multiple, PickedCallback chosen);
  // A file to save into (Chrome's Save As: a page, a link, an image): the
  // shell's save picker, |suggested_name| offered. |chosen| hears the path to
  // write -- the picked file, or a file in the app's staging folder that the
  // shell copies into the picked one when the download finishes -- or none.
  void SaveFile(const std::string& suggested_name, PickedCallback chosen);

  // A tab is going away: whatever it asked is cancelled.
  void ForgetContents(content::WebContents* web_contents);
  // Whether |web_contents| is waiting for the answer to a dialog or a file
  // chooser.
  bool HasPending(content::WebContents* web_contents) const;

 private:
  struct PendingDialog {
    raw_ptr<content::WebContents> contents;
    DialogClosedCallback callback;
  };
  struct PendingFiles {
    raw_ptr<content::WebContents> contents;
    scoped_refptr<content::FileSelectListener> listener;
    blink::mojom::FileChooserParams::Mode mode;
    // PickFiles' instead of |listener|.
    PickedCallback picked;
  };

  int32_t next_id_ = 1;
  std::map<int32_t, PendingDialog> dialogs_;
  std::map<int32_t, PendingFiles> files_;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_DIALOGS_H_
