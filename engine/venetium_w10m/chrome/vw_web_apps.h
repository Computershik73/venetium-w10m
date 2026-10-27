// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Installed apps (PWAs) on the phone's Start screen. An app's menu pins it:
// the shell makes a secondary tile of the app's icon and name, and a tap on
// the tile opens the app again (venetium_w10m_launch_app). A phone has no
// other place for installed apps to be found.

#ifndef VENETIUM_W10M_CHROME_VW_WEB_APPS_H_
#define VENETIUM_W10M_CHROME_VW_WEB_APPS_H_

#include <string>

class Browser;

namespace venetium_w10m::web_apps {

// The app of |browser|'s window: its icon written for the shell, then the
// shell's pin_app event. UI thread.
void PinToStart(Browser* browser);

// Opens the installed app |app_id| in its window, once the apps are known.
// UI thread.
void Launch(const std::string& app_id);

}  // namespace venetium_w10m::web_apps

#endif  // VENETIUM_W10M_CHROME_VW_WEB_APPS_H_
