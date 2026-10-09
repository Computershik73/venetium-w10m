// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_clipboard.h"

#include <optional>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "components/headless/clipboard/headless_clipboard.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/clipboard_monitor.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "venetium_w10m/browser/vw_engine.h"

namespace venetium_w10m {

VwClipboard::VwClipboard() {
  headless::SetHeadlessClipboardForCurrentThread();
  ui::ClipboardMonitor::GetInstance()->AddObserver(this);
}

VwClipboard::~VwClipboard() {
  ui::ClipboardMonitor::GetInstance()->RemoveObserver(this);
}

void VwClipboard::SetPhoneText(const std::string& text) {
  std::u16string text16 = base::UTF8ToUTF16(text);
  // Unchanged since the shell last sent it: a page may have copied something
  // since, which must not be overwritten by the older phone text.
  if (text16 == phone_text_) {
    return;
  }
  phone_text_ = text16;
  writing_ = true;
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(text16);
  }
  writing_ = false;
}

void VwClipboard::OnClipboardDataChanged() {
  if (writing_) {
    return;
  }
  // The in-memory clipboard answers at once.
  ui::Clipboard::GetForCurrentThread()->ReadText(
      ui::ClipboardBuffer::kCopyPaste, std::nullopt,
      base::BindOnce(&VwClipboard::Copied, base::Unretained(this)));
}

void VwClipboard::Copied(std::u16string text) {
  if (text.empty()) {
    return;
  }
  // The phone will hold it: the shell sending it back changes nothing.
  phone_text_ = text;
  venetium_w10m_events events = VwEngine::Get()->events();
  if (events.clipboard_set) {
    events.clipboard_set(base::UTF16ToUTF8(text).c_str());
  }
}

}  // namespace venetium_w10m
