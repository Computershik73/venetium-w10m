// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Text input without TSF or IMM32 (both need an HWND). The phone's
// on-screen keyboard belongs to the shell: it types into an invisible XAML
// TextBox there, and what it produces arrives here as committed text or a
// composition, which goes straight to the focused text input client --
// RenderWidgetHostViewAura, i.e. the focused field in the page.
//
// The other direction: the focused field's kind is published
// (VwEngine::text_input_state) so the shell raises the keyboard with the
// right layout -- digits for <input type=number>, @ for email -- and the
// keyboard itself is asked for the way Chrome on a Windows tablet asks for
// it: through a ui::VirtualKeyboardController, which
// RenderWidgetHostViewAura (VirtualKeyboardControllerWin) drives only when
// the user taps a field, never when a page focuses one by itself. While the
// keyboard covers the page, its rect goes back to the controller's
// observers, which inset the page so the field scrolls into view.

#ifndef VENETIUM_W10M_BROWSER_VW_INPUT_METHOD_H_
#define VENETIUM_W10M_BROWSER_VW_INPUT_METHOD_H_

#include <stdint.h>

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "ui/base/ime/input_method_minimal.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/base/ime/virtual_keyboard_controller.h"
#include "ui/gfx/geometry/rect.h"

namespace venetium_w10m {

class VwInputMethod : public ui::InputMethodMinimal {
 public:
  explicit VwInputMethod(ui::ImeKeyEventDispatcher* dispatcher);
  ~VwInputMethod() override;

  void CommitText(const std::u16string& text);
  void SetComposition(const std::u16string& text);

  // The shell's keyboard now covers |keyboard_rect| of the content area, in
  // DIPs (empty: hidden).
  void KeyboardChanged(const gfx::Rect& keyboard_rect);

  // ui::InputMethod:
  void OnTextInputTypeChanged(ui::TextInputClient* client) override;
  ui::VirtualKeyboardController* GetVirtualKeyboardController() override;
  // No IMM32 or TSF here: there are no IME messages and no input locale to
  // follow; the shell's keyboard does all of that.
  bool OnUntranslatedIMEMessage(const CHROME_MSG event,
                                NativeEventResult* result) override;
  void OnInputLocaleChanged() override;
  bool IsInputLocaleCJK() const override;
  void OnUrlChanged() override;

 protected:
  // ui::InputMethodBase:
  void OnDidChangeFocusedClient(ui::TextInputClient* focused_before,
                                ui::TextInputClient* focused) override;

 private:
  // The shell's keyboard as Chromium's IME code sees it.
  class Keyboard : public ui::VirtualKeyboardController {
   public:
    explicit Keyboard(VwInputMethod* owner);
    ~Keyboard() override;

    void Changed(const gfx::Rect& keyboard_rect);

    // ui::VirtualKeyboardController:
    bool DisplayVirtualKeyboard() override;
    void DismissVirtualKeyboard() override;
    void AddObserver(ui::VirtualKeyboardControllerObserver* observer) override;
    void RemoveObserver(
        ui::VirtualKeyboardControllerObserver* observer) override;
    bool IsKeyboardVisible() override;

   private:
    raw_ptr<VwInputMethod> owner_;
    bool visible_ = false;
    base::ObserverList<ui::VirtualKeyboardControllerObserver>::Unchecked
        observers_;
  };

  void Publish();

  Keyboard keyboard_{this};
  // Bumped on every request to show the keyboard.
  uint32_t serial_ = 0;
  // A request to show it stands (cleared by a dismissal, by focus leaving
  // text, and by the keyboard going away).
  bool wanted_ = false;
  ui::TextInputType last_type_ = ui::TEXT_INPUT_TYPE_NONE;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_INPUT_METHOD_H_
