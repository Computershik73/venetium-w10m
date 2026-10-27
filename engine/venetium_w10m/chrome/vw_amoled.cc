// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_amoled.h"

#include <atomic>
#include <memory>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/w10m_prefs.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_provider_manager.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"
#include "ui/native_theme/native_theme.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m::amoled {

namespace {

std::atomic<bool> g_on{false};

// The address bar's field and the like, which must stand out of black.
constexpr SkColor kRaised = SkColorSetRGB(0x16, 0x16, 0x16);

// After Chrome's mixers: in dark mode, the surfaces black. Most of Chrome's
// colours are made from the system tokens and follow them; the surfaces are
// set too for any recipe that names a colour of its own. Text and icons keep
// the dark theme's light grey.
void AddMixer(ui::ColorProvider* provider, const ui::ColorProviderKey& key) {
  if (!g_on.load() || key.color_mode != ui::ColorProviderKey::ColorMode::kDark) {
    return;
  }
  ui::ColorMixer& mixer = provider->AddMixer();
  const ui::ColorId black[] = {
       ui::kColorSysBase, ui::kColorSysHeader, ui::kColorSysHeaderInactive,
        ui::kColorSysHeaderContainer, ui::kColorSysSurface,
        ui::kColorSysSurface1, ui::kColorSysSurface2, ui::kColorSysSurface3,
        ui::kColorSysSurface4, ui::kColorSysSurface5,
        ui::kColorSysBaseContainer, ui::kColorSysBaseContainerElevated,
        ui::kColorFrameActive, ui::kColorFrameInactive,
        ui::kColorPrimaryBackground, ui::kColorMenuBackground,
        ui::kColorDialogBackground, ui::kColorBubbleBackground,
        static_cast<ui::ColorId>(kColorToolbar),
        static_cast<ui::ColorId>(kColorOmniboxResultsBackground),
        static_cast<ui::ColorId>(kColorNewTabPageBackground),
        static_cast<ui::ColorId>(kColorTabBackgroundActiveFrameActive)};
  for (ui::ColorId id : black) {
    mixer[id] = {SK_ColorBLACK};
  }
  const ui::ColorId raised[] = {
      ui::kColorSysOmniboxContainer, ui::kColorSysNeutralContainer,
      static_cast<ui::ColorId>(kColorLocationBarBackground)};
  for (ui::ColorId id : raised) {
    mixer[id] = {kRaised};
  }
}

void TellShell(bool on) {
  const venetium_w10m_events events = VwEngine::Get()->events();
  if (events.shell_command) {
    events.shell_command(on ? 7 : 8);
  }
}

// Colours made again everywhere: a menu, a dialog, a window.
void Repaint() {
  ui::NativeTheme::GetInstanceForNativeUi()->NotifyOnNativeThemeUpdated();
}

void Changed(Profile* profile) {
  const bool on = profile->GetPrefs()->GetBoolean(w10m::prefs::kAmoled);
  NoteF("theme: black %s", on ? "on" : "off");
  g_on.store(on);
  // Black is the dark theme's: asked of the profile's theme while it is on;
  // the phone's own theme decides again once it is off.
  if (ThemeService* theme = ThemeServiceFactory::GetForProfile(profile)) {
    theme->SetBrowserColorScheme(on ? ThemeService::BrowserColorScheme::kDark
                                    : ThemeService::BrowserColorScheme::kSystem);
  }
  Repaint();
  TellShell(on);
}

PrefChangeRegistrar& Registrar() {
  static base::NoDestructor<PrefChangeRegistrar> registrar;
  return *registrar;
}

}  // namespace

void Install() {
  g_on.store(base::CommandLine::ForCurrentProcess()->HasSwitch("vw-amoled"));
  ui::ColorProviderManager::Get().AppendColorProviderInitializer(
      base::BindRepeating(&AddMixer));
}

void Watch(Profile* profile) {
  PrefService* prefs = profile->GetPrefs();
  if (!prefs->FindPreference(w10m::prefs::kAmoled)) {
    return;
  }
  const bool on = prefs->GetBoolean(w10m::prefs::kAmoled);
  if (on != g_on.load()) {
    // The shell's copy was out of date (a new install, its settings reset).
    g_on.store(on);
    Repaint();
  }
  if (on) {
    if (ThemeService* theme = ThemeServiceFactory::GetForProfile(profile)) {
      if (theme->GetBrowserColorScheme() !=
          ThemeService::BrowserColorScheme::kDark) {
        theme->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kDark);
      }
    }
  }
  TellShell(on);
  Registrar().Reset();
  Registrar().Init(prefs);
  Registrar().Add(w10m::prefs::kAmoled,
                  base::BindRepeating(&Changed, base::Unretained(profile)));
}

}  // namespace venetium_w10m::amoled
