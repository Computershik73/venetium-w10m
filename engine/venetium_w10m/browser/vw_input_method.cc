// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_input_method.h"

#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/text_input_client.h"
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

}  // namespace

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

void VwInputMethod::OnTextInputTypeChanged(ui::TextInputClient* client) {
  ui::InputMethodMinimal::OnTextInputTypeChanged(client);
  const ui::TextInputType type = GetTextInputType();
  // Blink reports every focus move as a text input state change, so a type
  // that stays "text" from one field to the next still arrives here; the
  // shell is told it is a new field and raises the keyboard again.
  Publish(type != ui::TEXT_INPUT_TYPE_NONE);
}

void VwInputMethod::SetVirtualKeyboardVisibilityIfEnabled(bool should_show) {
  ui::InputMethodMinimal::SetVirtualKeyboardVisibilityIfEnabled(should_show);
  if (should_show) {
    Publish(true);
  }
}

void VwInputMethod::OnDidChangeFocusedClient(
    ui::TextInputClient* focused_before,
    ui::TextInputClient* focused) {
  ui::InputMethodMinimal::OnDidChangeFocusedClient(focused_before, focused);
  Publish(focused != nullptr);
}

void VwInputMethod::Publish(bool new_field) {
  VwEngine* engine = VwEngine::Get();
  if (!engine) {
    return;
  }
  const ui::TextInputType type = GetTextInputType();
  const int32_t kind = KindOf(type);
  if (new_field && kind != 0) {
    ++serial_;
  }
  if (type != last_type_) {
    NoteF("ime: text input type %d (kind %d)", static_cast<int>(type), kind);
    last_type_ = type;
  }
  engine->text_input_wanted.store(kind != 0 ? 1 : 0);
  engine->text_input_state.store(((serial_ & 0xffffff) << 8) |
                                 static_cast<uint32_t>(kind));
}

}  // namespace venetium_w10m
