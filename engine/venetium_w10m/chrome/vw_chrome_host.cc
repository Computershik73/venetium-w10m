// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_chrome_host.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "ui/aura/window.h"
#include "ui/base/base_window.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/focus_controller.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/browser/vw_interface_guard.h"
#include "venetium_w10m/browser/vw_platform.h"
#include "venetium_w10m/browser/vw_window_manager.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// The window in front that is not a browser window: a dialog, a bubble, the
// find bar's or a permission prompt's. Null when a browser window is.
views::Widget* FrontWidgetOtherThanBrowser() {
  VwPlatform* platform = VwPlatform::Get();
  if (!platform || !platform->window_manager()) {
    return nullptr;
  }
  aura::Window* active =
      platform->window_manager()->focus_controller()->GetActiveWindow();
  if (!active || BrowserView::GetBrowserViewForNativeWindow(active)) {
    return nullptr;
  }
  views::Widget* widget = views::Widget::GetWidgetForNativeWindow(active);
  return widget && widget->IsVisible() ? widget : nullptr;
}

std::vector<Profile*> Profiles() {
  ProfileManager* manager = g_browser_process->profile_manager();
  return manager ? manager->GetLoadedProfiles() : std::vector<Profile*>();
}

}  // namespace

VwChromeHost::VwChromeHost() {
  browsers_.Observe(GlobalBrowserCollection::GetInstance());
}

VwChromeHost::~VwChromeHost() {
  Observe(nullptr);
}

void VwChromeHost::Started() {
  // Chrome ends the run when no window and nothing else holds it open; on a
  // phone that is up to the shell.
  keep_alive_ = std::make_unique<ScopedKeepAlive>(
      KeepAliveOrigin::PANEL, KeepAliveRestartOption::DISABLED);
  started_ = true;
  WatchActiveTab();
  UpdateBackState();
  // A run that has gone on with Chrome's window for a while did not die of
  // Chrome's interface.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&interface_guard::Proven), base::Seconds(10));
}

BrowserWindowInterface* VwChromeHost::ActiveBrowser() {
  return GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
}

void VwChromeHost::OpenUrl(const GURL& url) {
  BrowserWindowInterface* browser = ActiveBrowser();
  if (!browser) {
    std::vector<Profile*> profiles = Profiles();
    if (profiles.empty()) {
      NoteF("chrome: no profile to open %s in", url.spec().c_str());
      return;
    }
    chrome::NewEmptyWindow(profiles.front());
    browser = ActiveBrowser();
    if (!browser) {
      return;
    }
  }
  browser->OpenGURL(url, WindowOpenDisposition::NEW_FOREGROUND_TAB);
}

bool VwChromeHost::BackStep(bool act) {
  // A menu: Chrome's own, a page's context menu, a <select> list.
  if (views::MenuController* menu = views::MenuController::GetActiveInstance()) {
    if (act) {
      menu->Cancel(views::MenuController::ExitType::kAll);
    }
    return true;
  }
  // A dialog or a bubble in front of the browser window.
  if (views::Widget* widget = FrontWidgetOtherThanBrowser()) {
    if (act) {
      widget->Close();
    }
    return true;
  }
  BrowserWindowInterface* browser = ActiveBrowser();
  if (!browser) {
    return false;
  }
  // A page in fullscreen (a video), or the browser.
  ui::BaseWindow* window = browser->GetWindow();
  if (window && window->IsFullscreen()) {
    if (act) {
      if (ExclusiveAccessManager* access =
              browser->GetFeatures().exclusive_access_manager()) {
        access->ExitExclusiveAccess();
      }
    }
    return true;
  }
  TabStripModel* tabs = browser->GetTabStripModel();
  content::WebContents* contents = tabs ? tabs->GetActiveWebContents() : nullptr;
  if (!contents) {
    return false;
  }
  if (contents->GetController().CanGoBack()) {
    if (act) {
      contents->GetController().GoBack();
    }
    return true;
  }
  // A tab a page opened, with nowhere left to go back to: closing it goes
  // back to the page that opened it.
  const int index = tabs->active_index();
  if (index >= 0 && tabs->GetOpenerOfTabAt(index)) {
    if (act) {
      tabs->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
    }
    return true;
  }
  return false;
}

bool VwChromeHost::Back() {
  const bool did = BackStep(/*act=*/true);
  UpdateBackState();
  return did;
}

void VwChromeHost::UpdateBackState() {
  if (!started_) {
    return;
  }
  VwEngine::Get()->selected_can_go_back.store(BackStep(/*act=*/false));
}

void VwChromeHost::WatchActiveTab() {
  BrowserWindowInterface* browser = ActiveBrowser();
  TabStripModel* tabs = browser ? browser->GetTabStripModel() : nullptr;
  Observe(tabs ? tabs->GetActiveWebContents() : nullptr);
}

void VwChromeHost::OnBrowserCreated(BrowserWindowInterface* browser) {
  active_tab_changes_[browser] = browser->RegisterActiveTabDidChange(
      base::BindRepeating(
          [](base::WeakPtr<VwChromeHost> host, BrowserWindowInterface*) {
            if (host) {
              host->WatchActiveTab();
              host->UpdateBackState();
            }
          },
          weak_factory_.GetWeakPtr()));
}

void VwChromeHost::OnBrowserClosed(BrowserWindowInterface* browser) {
  active_tab_changes_.erase(browser);
  Observe(nullptr);
  // After Chrome is done with the closing window.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&VwChromeHost::ReopenIfEmpty,
                                weak_factory_.GetWeakPtr()));
}

void VwChromeHost::OnBrowserActivated(BrowserWindowInterface* browser) {
  WatchActiveTab();
  UpdateBackState();
}

void VwChromeHost::ReopenIfEmpty() {
  if (quitting_ || !started_ || ActiveBrowser()) {
    WatchActiveTab();
    UpdateBackState();
    return;
  }
  std::vector<Profile*> profiles = Profiles();
  if (profiles.empty()) {
    return;
  }
  Note("chrome: the last window closed; a new one, as a phone browser has");
  chrome::NewEmptyWindow(profiles.front());
  WatchActiveTab();
  UpdateBackState();
}

void VwChromeHost::NavigationEntryCommitted(
    const content::LoadCommittedDetails& load_details) {
  UpdateBackState();
}

void VwChromeHost::NavigationListPruned(
    const content::PrunedDetails& pruned_details) {
  UpdateBackState();
}

void VwChromeHost::NavigationEntriesDeleted() {
  UpdateBackState();
}

void VwChromeHost::Suspending() {
  if (started_) {
    interface_guard::Proven();
  }
  // The phone ends a suspended app without telling it: preferences,
  // cookies and storage are written out now, and the run is marked as one
  // that was ended, not one that crashed -- the next start then restores
  // without asking whether to.
  if (PrefService* local_state = g_browser_process->local_state()) {
    local_state->CommitPendingWrite();
  }
  for (Profile* profile : Profiles()) {
    if (ExitTypeService* exit = ExitTypeService::GetInstanceForProfile(profile)) {
      exit->SetCurrentSessionExitType(ExitType::kForcedShutdown);
    }
    profile->GetPrefs()->CommitPendingWrite();
    content::StoragePartition* partition =
        profile->GetDefaultStoragePartition();
    partition->GetCookieManagerForBrowserProcess()->FlushCookieStore(
        base::DoNothing());
    partition->Flush();
  }
}

void VwChromeHost::Resumed() {
  for (Profile* profile : Profiles()) {
    if (ExitTypeService* exit = ExitTypeService::GetInstanceForProfile(profile)) {
      exit->SetCurrentSessionExitType(ExitType::kCrashed);
    }
  }
}

namespace {

// Background tabs whose pages can be discarded now, seen longest ago first:
// not the one in front of a window, none that is already discarded or that
// Chrome keeps (playing audio, capturing, a form being filled...), none that
// left the screen less than |spare| ago.
std::vector<resource_coordinator::TabLifecycleUnitExternal*> DiscardCandidates(
    base::TimeDelta spare) {
  std::vector<resource_coordinator::TabLifecycleUnitExternal*> found;
  const base::Time now = base::Time::Now();
  std::vector<BrowserWindowInterface*> browsers;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&browsers](BrowserWindowInterface* browser) {
        browsers.push_back(browser);
        return true;
      });
  for (BrowserWindowInterface* browser : browsers) {
    TabStripModel* tabs = browser->GetTabStripModel();
    if (!tabs) {
      continue;
    }
    for (int i = 0; i < tabs->count(); ++i) {
      if (i == tabs->active_index()) {
        continue;
      }
      auto* unit = resource_coordinator::TabLifecycleUnitExternal::
          FromWebContents(tabs->GetWebContentsAt(i));
      if (!unit || !unit->IsAutoDiscardable() ||
          unit->GetTabState() ==
              ::mojom::LifecycleUnitState::DISCARDED ||
          now - unit->GetLastFocusedTime() < spare) {
        continue;
      }
      found.push_back(unit);
    }
  }
  std::sort(found.begin(), found.end(), [](auto* a, auto* b) {
    return a->GetLastFocusedTime() < b->GetLastFocusedTime();
  });
  return found;
}

}  // namespace

int VwChromeHost::DiscardableTabs(base::TimeDelta spare) {
  return static_cast<int>(DiscardCandidates(spare).size());
}

int VwChromeHost::DiscardBackgroundTabs(int limit,
                                        base::TimeDelta spare,
                                        const char* cause) {
  int discarded = 0;
  for (auto* unit : DiscardCandidates(spare)) {
    if (discarded >= limit) {
      break;
    }
    if (unit->DiscardTab(::mojom::LifecycleUnitDiscardReason::URGENT)) {
      NoteF("tab discarded (%s): %s", cause,
            unit->GetWebContents()->GetLastCommittedURL().spec().c_str());
      ++discarded;
    }
  }
  return discarded;
}

void VwChromeHost::Quit() {
  Note("chrome: quitting");
  quitting_ = true;
  keep_alive_.reset();
  chrome::AttemptExit();
}

}  // namespace venetium_w10m
