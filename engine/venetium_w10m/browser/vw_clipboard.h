// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The pages' clipboard and the phone's. Chromium's Windows clipboard is a
// window's (OpenClipboard needs an HWND), so the engine's lives in memory
// (components/headless' in-memory clipboard). Text a page copies goes to the
// shell (events.clipboard_set), which puts it on the phone's clipboard; the
// phone's text, which the shell sends whenever it may have changed
// (venetium_w10m_clipboard_phone_text), is written into the engine's, so a
// paste in a page gets it.

#ifndef VENETIUM_W10M_BROWSER_VW_CLIPBOARD_H_
#define VENETIUM_W10M_BROWSER_VW_CLIPBOARD_H_

#include <string>

#include "ui/base/clipboard/clipboard_observer.h"

namespace venetium_w10m {

class VwClipboard : public ui::ClipboardObserver {
 public:
  // UI thread, before anything uses the clipboard: installs the in-memory
  // clipboard for the thread.
  VwClipboard();
  VwClipboard(const VwClipboard&) = delete;
  VwClipboard& operator=(const VwClipboard&) = delete;
  ~VwClipboard() override;

  // The phone's clipboard holds |text| (UTF-8).
  void SetPhoneText(const std::string& text);

 private:
  // ui::ClipboardObserver:
  void OnClipboardDataChanged() override;

  void Copied(std::u16string text);

  // Set while the phone's text is being written, so it is not sent back.
  bool writing_ = false;
  std::u16string phone_text_;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_CLIPBOARD_H_
