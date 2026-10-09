// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_input_method.h"

#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/ime/virtual_keyboard_controller_observer.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// The kinds venetium_w10m_text_input_state reports (see the C header).
int32_t KindOf(ui::TextInputType type) {
  switch (type) {
    case ui::TEXT_INPUT_TYPE_NONE:
      return 0;
    case ui::TEXT_INPUT_TYPE_PASSWORD:
      return 2;
    case ui::TEXT_INPUT_TYPE_SEARCH:
      return 3;
    case ui::TEXT_INPUT_TYPE_EMAIL:
      return 4;
    case ui::TEXT_INPUT_TYPE_NUMBER:
      return 5;
    case ui::TEXT_INPUT_TYPE_TELEPHONE:
      return 6;
    case ui::TEXT_INPUT_TYPE_URL:
      return 7;
    case ui::TEXT_INPUT_TYPE_TEXT_AREA:
    case ui::TEXT_INPUT_TYPE_CONTENT_EDITABLE:
      return 8;
    default:
      return 1;
  }
}

// venetium_w10m_text_input_state's flag: the page wants the keyboard up.
constexpr uint32_t kKeyboardWanted = 0x80;

}  // namespace

VwInputMethod::Keyboard::Keyboard(VwInputMethod* owner) : owner_(owner) {}

VwInputMethod::Keyboard::~Keyboard() = default;

void VwInputMethod::Keyboard::Changed(const gfx::Rect& keyboard_rect) {
  const bool visible = !keyboard_rect.IsEmpty();
  if (!visible && !visible_) {
    return;
  }
  visible_ = visible;
  if (visible) {
    // VirtualKeyboardControllerWin insets the page by the keyboard's height,
    // and the page scrolls the focused field above it.
    for (auto& observer : observers_) {
      observer.OnKeyboardVisible(keyboard_rect);
    }
    return;
  }
  // Gone (the Back key, or the user closed it): the next tap on the field
  // must ask for it again.
  owner_->wanted_ = false;
  owner_->Publish();
  for (auto& observer : observers_) {
    observer.OnKeyboardHidden();
  }
}

bool VwInputMethod::Keyboard::DisplayVirtualKeyboard() {
  ++owner_->serial_;
  owner_->wanted_ = true;
  owner_->Publish();
  return true;
}

void VwInputMethod::Keyboard::DismissVirtualKeyboard() {
  owner_->wanted_ = false;
  owner_->Publish();
}

void VwInputMethod::Keyboard::AddObserver(
    ui::VirtualKeyboardControllerObserver* observer) {
  observers_.AddObserver(observer);
}

void VwInputMethod::Keyboard::RemoveObserver(
    ui::VirtualKeyboardControllerObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool VwInputMethod::Keyboard::IsKeyboardVisible() {
  return visible_;
}

VwInputMethod::VwInputMethod(ui::ImeKeyEventDispatcher* dispatcher)
    : ui::InputMethodMinimal(dispatcher) {}

VwInputMethod::~VwInputMethod() = default;

void VwInputMethod::CommitText(const std::u16string& text) {
  ui::TextInputClient* client = GetTextInputClient();
  if (!client || text.empty()) {
    return;
  }
  if (client->HasCompositionText()) {
    client->ClearCompositionText();
  }
  client->InsertText(
      text, ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
}

void VwInputMethod::SetComposition(const std::u16string& text) {
  ui::TextInputClient* client = GetTextInputClient();
  if (!client) {
    return;
  }
  if (text.empty()) {
    if (client->HasCompositionText()) {
      client->ClearCompositionText();
    }
    return;
  }
  ui::CompositionText composition;
  composition.text = text;
  composition.selection = gfx::Range(text.size());
  client->SetCompositionText(composition);
}

void VwInputMethod::KeyboardChanged(const gfx::Rect& keyboard_rect) {
  keyboard_.Changed(keyboard_rect);
}

void VwInputMethod::OnTextInputTypeChanged(ui::TextInputClient* client) {
  ui::InputMethodMinimal::OnTextInputTypeChanged(client);
  if (GetTextInputType() == ui::TEXT_INPUT_TYPE_NONE) {
    wanted_ = false;
  }
  Publish();
}

ui::VirtualKeyboardController* VwInputMethod::GetVirtualKeyboardController() {
  return &keyboard_;
}

bool VwInputMethod::OnUntranslatedIMEMessage(const CHROME_MSG event,
                                             NativeEventResult* result) {
  return false;
}

void VwInputMethod::OnInputLocaleChanged() {}

bool VwInputMethod::IsInputLocaleCJK() const {
  return false;
}

void VwInputMethod::OnUrlChanged() {}

void VwInputMethod::OnDidChangeFocusedClient(
    ui::TextInputClient* focused_before,
    ui::TextInputClient* focused) {
  ui::InputMethodMinimal::OnDidChangeFocusedClient(focused_before, focused);
  if (!focused) {
    wanted_ = false;
  }
  Publish();
}

void VwInputMethod::Publish() {
  const ui::TextInputType type = GetTextInputType();
  const int32_t kind = KindOf(type);
  if (type != last_type_) {
    NoteF("ime: text input type %d (kind %d)", static_cast<int>(type), kind);
    last_type_ = type;
  }
  VwEngine* engine = VwEngine::Get();
  engine->text_input_wanted.store(kind != 0 ? 1 : 0);
  engine->text_input_state.store(
      ((serial_ & 0xffffff) << 8) | (wanted_ && kind ? kKeyboardWanted : 0) |
      static_cast<uint32_t>(kind));
}

}  // namespace venetium_w10m
