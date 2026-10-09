// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Text input without TSF or IMM32 (both need an HWND). The phone's
// on-screen keyboard belongs to the shell: it types into an invisible XAML
// TextBox there, and what it produces arrives here as committed text or a
// composition, which goes straight to the focused text input client --
// RenderWidgetHostViewAura, i.e. the focused field in the page.
//
// The other direction: whenever the focused field changes, its kind is
// published (VwEngine::text_input_state) so the shell can raise the keyboard
// with the right layout -- digits for <input type=number>, @ for email.

#ifndef VENETIUM_W10M_BROWSER_VW_INPUT_METHOD_H_
#define VENETIUM_W10M_BROWSER_VW_INPUT_METHOD_H_

#include <stdint.h>

#include <string>

#include "ui/base/ime/input_method_minimal.h"
#include "ui/base/ime/text_input_type.h"

namespace venetium_w10m {

class VwInputMethod : public ui::InputMethodMinimal {
 public:
  explicit VwInputMethod(ui::ImeKeyEventDispatcher* dispatcher);
  ~VwInputMethod() override;

  void CommitText(const std::u16string& text);
  void SetComposition(const std::u16string& text);

  // ui::InputMethod:
  void OnTextInputTypeChanged(ui::TextInputClient* client) override;
  void SetVirtualKeyboardVisibilityIfEnabled(bool should_show) override;

 protected:
  // ui::InputMethodBase:
  void OnDidChangeFocusedClient(ui::TextInputClient* focused_before,
                                ui::TextInputClient* focused) override;

 private:
  void Publish(bool new_field);

  uint32_t serial_ = 0;
  ui::TextInputType last_type_ = ui::TEXT_INPUT_TYPE_NONE;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_INPUT_METHOD_H_
