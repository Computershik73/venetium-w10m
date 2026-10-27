// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_web_apps.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/web_applications/web_app_command_scheduler.h"
#include "chrome/browser/web_applications/web_app_icon_manager.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m::web_apps {

namespace {

// A medium tile's logo at scale 200 (its 150 view pixels).
constexpr int kTileIconSize = 300;

// The icon as a PNG file the shell copies for its tile; on a thread that
// may block. An empty path when it could not be written.
base::FilePath WriteIcon(base::FilePath path, SkBitmap bitmap) {
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                        /*discard_transparency=*/false);
  if (!png || !base::CreateDirectory(path.DirName()) ||
      !base::WriteFile(path, *png)) {
    return base::FilePath();
  }
  return path;
}

void TellShell(std::string app_id, std::string name, base::FilePath icon) {
  NoteF("apps: %s (%s) to Start, icon %s", app_id.c_str(), name.c_str(),
        icon.empty() ? "none" : "written");
  const venetium_w10m_events events = VwEngine::Get()->events();
  if (events.pin_app) {
    events.pin_app(app_id.c_str(), name.c_str(),
                   icon.AsUTF8Unsafe().c_str());
  }
}

void IconRead(std::string app_id,
              std::string name,
              base::FilePath path,
              web_app::OrderedSizeToBitmap icons) {
  if (icons.empty()) {
    TellShell(std::move(app_id), std::move(name), base::FilePath());
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&WriteIcon, std::move(path), icons.begin()->second),
      base::BindOnce(&TellShell, app_id, name));
}

void LaunchWhenReady(std::string app_id) {
  Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
  web_app::WebAppProvider* provider =
      profile ? web_app::WebAppProvider::GetForWebApps(profile) : nullptr;
  if (!provider) {
    NoteF("apps: no apps to open %s from", app_id.c_str());
    return;
  }
  if (!provider->registrar_unsafe().GetAppById(app_id)) {
    NoteF("apps: %s is not installed (any more)", app_id.c_str());
    return;
  }
  NoteF("apps: opening %s from its tile", app_id.c_str());
  provider->scheduler().LaunchApp(app_id, /*url=*/std::nullopt,
                                  base::DoNothing());
}

}  // namespace

void PinToStart(Browser* browser) {
  web_app::AppBrowserController* controller = browser->app_controller();
  Profile* profile = browser->profile();
  web_app::WebAppProvider* provider =
      web_app::WebAppProvider::GetForWebApps(profile);
  if (!controller || !provider) {
    return;
  }
  const webapps::AppId app_id = controller->app_id();
  const std::string name =
      provider->registrar_unsafe().GetAppShortName(app_id);
  const base::FilePath path = profile->GetPath()
                                  .AppendASCII("w10m-tiles")
                                  .AppendASCII(app_id + ".png");
  provider->icon_manager().ReadIconAndResize(
      app_id, web_app::IconPurpose::ANY, kTileIconSize,
      base::BindOnce(&IconRead, app_id, name, path));
}

void Launch(const std::string& app_id) {
  Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
  web_app::WebAppProvider* provider =
      profile ? web_app::WebAppProvider::GetForWebApps(profile) : nullptr;
  if (!provider) {
    NoteF("apps: no profile yet to open %s from", app_id.c_str());
    return;
  }
  // At start the apps are known a little after the browser's window.
  provider->on_registry_ready().Post(
      FROM_HERE, base::BindOnce(&LaunchWhenReady, app_id));
}

}  // namespace venetium_w10m::web_apps
