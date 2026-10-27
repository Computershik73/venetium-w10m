// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_dialogs.h"

#include <utility>

#include "base/files/file_path.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "content/public/browser/web_contents.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

VwDialogs::VwDialogs() = default;

VwDialogs::~VwDialogs() {
  for (auto& [id, dialog] : dialogs_) {
    std::move(dialog.callback).Run(false, std::u16string());
  }
  for (auto& [id, files] : files_) {
    files.listener->FileSelectionCanceled();
  }
}

void VwDialogs::RunJavaScriptDialog(content::WebContents* web_contents,
                                    content::RenderFrameHost* render_frame_host,
                                    content::JavaScriptDialogType dialog_type,
                                    const std::u16string& message_text,
                                    const std::u16string& default_prompt_text,
                                    DialogClosedCallback callback,
                                    bool* did_suppress_message) {
  int32_t kind = 0;
  switch (dialog_type) {
    case content::JAVASCRIPT_DIALOG_TYPE_ALERT:
      kind = 0;
      break;
    case content::JAVASCRIPT_DIALOG_TYPE_CONFIRM:
      kind = 1;
      break;
    case content::JAVASCRIPT_DIALOG_TYPE_PROMPT:
      kind = 2;
      break;
  }
  venetium_w10m_events events;
  if (VwEngine* engine = VwEngine::Get()) {
    events = engine->events();
  } else {
    events = {};
  }
  if (!events.dialog) {
    // Nobody to ask: OK an alert, cancel the rest.
    std::move(callback).Run(kind == 0, std::u16string());
    return;
  }
  const int32_t id = next_id_++;
  dialogs_[id] = PendingDialog{web_contents, std::move(callback)};
  const std::string message = base::UTF16ToUTF8(message_text);
  const std::string default_text = base::UTF16ToUTF8(default_prompt_text);
  NoteF("dialog %d: kind %d", id, kind);
  events.dialog(id, kind, message.c_str(), default_text.c_str());
}

void VwDialogs::RunBeforeUnloadDialog(
    content::WebContents* web_contents,
    content::RenderFrameHost* render_frame_host,
    bool is_reload,
    DialogClosedCallback callback) {
  venetium_w10m_events events;
  if (VwEngine* engine = VwEngine::Get()) {
    events = engine->events();
  } else {
    events = {};
  }
  if (!events.dialog) {
    std::move(callback).Run(true, std::u16string());
    return;
  }
  const int32_t id = next_id_++;
  dialogs_[id] = PendingDialog{web_contents, std::move(callback)};
  events.dialog(id, 3, "", "");
}

void VwDialogs::CancelDialogs(content::WebContents* web_contents,
                              bool reset_state) {
  for (auto it = dialogs_.begin(); it != dialogs_.end();) {
    if (it->second.contents == web_contents) {
      std::move(it->second.callback).Run(false, std::u16string());
      it = dialogs_.erase(it);
    } else {
      ++it;
    }
  }
}

void VwDialogs::Reply(int32_t id, bool accepted, const std::string& text) {
  auto it = dialogs_.find(id);
  if (it == dialogs_.end()) {
    return;
  }
  DialogClosedCallback callback = std::move(it->second.callback);
  dialogs_.erase(it);
  std::move(callback).Run(accepted, base::UTF8ToUTF16(text));
}

void VwDialogs::ChooseFiles(content::WebContents* web_contents,
                            scoped_refptr<content::FileSelectListener> listener,
                            const blink::mojom::FileChooserParams& params) {
  venetium_w10m_events events;
  if (VwEngine* engine = VwEngine::Get()) {
    events = engine->events();
  } else {
    events = {};
  }
  if (!events.file_chooser ||
      params.mode == blink::mojom::FileChooserParams::Mode::kOpenDirectory ||
      params.mode == blink::mojom::FileChooserParams::Mode::kUploadFolder) {
    listener->FileSelectionCanceled();
    return;
  }
  std::vector<std::string> accept;
  for (const std::u16string& type : params.accept_types) {
    accept.push_back(base::UTF16ToUTF8(type));
  }
  const std::string accept_list = base::JoinString(accept, ",");
  const int32_t id = next_id_++;
  files_[id] =
      PendingFiles{web_contents, std::move(listener), params.mode, {}};
  const bool multiple =
      params.mode == blink::mojom::FileChooserParams::Mode::kOpenMultiple;
  const bool save = params.mode == blink::mojom::FileChooserParams::Mode::kSave;
  NoteF("files %d: accept '%s', multiple %d, save %d", id, accept_list,
        multiple ? 1 : 0, save ? 1 : 0);
  events.file_chooser(id, accept_list.c_str(), multiple ? 1 : 0, save ? 1 : 0);
}

void VwDialogs::FilesChosen(int32_t id, const std::string& paths) {
  auto it = files_.find(id);
  if (it == files_.end()) {
    return;
  }
  PendingFiles pending = std::move(it->second);
  files_.erase(it);
  if (pending.picked) {
    std::vector<base::FilePath> picked;
    for (const std::string& path :
         base::SplitString(paths, "\n", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY)) {
      picked.emplace_back(base::UTF8ToWide(path));
    }
    std::move(pending.picked).Run(std::move(picked));
    return;
  }
  std::vector<blink::mojom::FileChooserFileInfoPtr> chosen;
  for (const std::string& path :
       base::SplitString(paths, "\n", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    chosen.push_back(blink::mojom::FileChooserFileInfo::NewNativeFile(
        blink::mojom::NativeFileInfo::New(
            base::FilePath(base::UTF8ToWide(path)), std::u16string(),
            std::vector<std::u16string>())));
  }
  if (chosen.empty()) {
    pending.listener->FileSelectionCanceled();
    return;
  }
  pending.listener->FileSelected(std::move(chosen), base::FilePath(),
                                 pending.mode);
}

void VwDialogs::PickFiles(bool multiple, PickedCallback chosen) {
  const venetium_w10m_events events = VwEngine::Get()->events();
  if (!events.file_chooser) {
    std::move(chosen).Run({});
    return;
  }
  const int32_t id = next_id_++;
  files_[id] = PendingFiles{nullptr, nullptr,
                            blink::mojom::FileChooserParams::Mode::kOpen,
                            std::move(chosen)};
  NoteF("files %d: Chrome's file dialog, multiple %d", id, multiple ? 1 : 0);
  events.file_chooser(id, "", multiple ? 1 : 0, 0);
}

void VwDialogs::SaveFile(const std::string& suggested_name,
                         PickedCallback chosen) {
  const venetium_w10m_events events = VwEngine::Get()->events();
  if (!events.file_chooser) {
    std::move(chosen).Run({});
    return;
  }
  const int32_t id = next_id_++;
  files_[id] = PendingFiles{nullptr, nullptr,
                            blink::mojom::FileChooserParams::Mode::kSave,
                            std::move(chosen)};
  NoteF("files %d: Chrome saves, offering %s", id, suggested_name.c_str());
  // In save mode the "accept" argument carries the name offered.
  events.file_chooser(id, suggested_name.c_str(), 0, 1);
}

void VwDialogs::PickFolder(PickedCallback chosen) {
  const venetium_w10m_events events = VwEngine::Get()->events();
  if (!events.file_chooser) {
    std::move(chosen).Run({});
    return;
  }
  const int32_t id = next_id_++;
  files_[id] = PendingFiles{nullptr, nullptr,
                            blink::mojom::FileChooserParams::Mode::kUploadFolder,
                            std::move(chosen)};
  NoteF("files %d: Chrome asks for a folder", id);
  events.file_chooser(id, "", 0, 2);
}

void VwDialogs::ForgetContents(content::WebContents* web_contents) {
  CancelDialogs(web_contents, true);
  for (auto it = files_.begin(); it != files_.end();) {
    if (it->second.contents == web_contents) {
      it->second.listener->FileSelectionCanceled();
      it = files_.erase(it);
    } else {
      ++it;
    }
  }
}

bool VwDialogs::HasPending(content::WebContents* web_contents) const {
  for (const auto& [id, dialog] : dialogs_) {
    if (dialog.contents == web_contents) {
      return true;
    }
  }
  for (const auto& [id, files] : files_) {
    if (files.contents == web_contents) {
      return true;
    }
  }
  return false;
}

}  // namespace venetium_w10m
