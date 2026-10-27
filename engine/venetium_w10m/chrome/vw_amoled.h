// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The black theme for an AMOLED screen ("Black theme (AMOLED)" in the
// Windows 10 Mobile submenu, the w10m.amoled setting): Chrome's interface in
// dark mode with its surfaces black -- the toolbar, the address bar's field,
// menus, dialogs, the suggestions, the new tab page -- and its icons and
// text the dark theme's light grey. A colour mixer added after Chrome's own;
// the shell keeps a copy of the setting for its own colours (the status bar,
// the room behind the page) from the very start.

#ifndef VENETIUM_W10M_CHROME_VW_AMOLED_H_
#define VENETIUM_W10M_CHROME_VW_AMOLED_H_

class Profile;

namespace venetium_w10m::amoled {

// Before the first window: the mixer, on or off as the shell said
// (--vw-amoled).
void Install();

// The profile's setting: followed from now on, the shell told of it.
void Watch(Profile* profile);

}  // namespace venetium_w10m::amoled

#endif  // VENETIUM_W10M_CHROME_VW_AMOLED_H_
