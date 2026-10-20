// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Chrome for Android's tab switcher on a phone: the tabs as a grid of cards,
// each with its page's picture, title and a close button, under a bar with a
// new tab button, the number of tabs and a menu, and a field to find a tab
// by its title or address. The tab count button beside the address bar opens
// it (w10m::SetTabSwitcherOpener); a card, the new tab button or the phone's
// Back key closes it.
//
// It is a window of its own over the browser window, filling the screen.

#ifndef VENETIUM_W10M_CHROME_VW_TAB_GRID_H_
#define VENETIUM_W10M_CHROME_VW_TAB_GRID_H_

class Browser;

namespace venetium_w10m {

class VwTabGrid {
 public:
  // Opens the tab switcher over |browser|'s window, unless it is open.
  static void Show(Browser* browser);
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_TAB_GRID_H_
