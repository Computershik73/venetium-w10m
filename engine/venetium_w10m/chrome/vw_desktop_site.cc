// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_desktop_site.h"

#include <optional>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/w10m_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_user_agent.h"

namespace venetium_w10m {

namespace {

// Sites that turn a phone away whatever it asks for.
bool AlwaysDesktop(const GURL& url) {
  return url.host() == "chromewebstore.google.com";
}

bool WantsDesktop(content::WebContents* contents, const GURL& url) {
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  PrefService* prefs = profile ? profile->GetPrefs() : nullptr;
  if (prefs && prefs->FindPreference(w10m::prefs::kDesktopSites)) {
    const std::optional<bool> chosen =
        prefs->GetDict(w10m::prefs::kDesktopSites).FindBool(url.host());
    if (chosen) {
      return *chosen;
    }
  }
  if (AlwaysDesktop(url) || VwEngine::Get()->desktop_mode.load()) {
    return true;
  }
  return prefs && prefs->FindPreference(w10m::prefs::kDesktopSiteDefault) &&
         prefs->GetBoolean(w10m::prefs::kDesktopSiteDefault);
}

}  // namespace

DesktopSite::DesktopSite(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<DesktopSite>(*contents) {
  // The agent a navigation that asks for the desktop gets; the phone's is
  // the browser's own (VwChromeBrowserClient::GetUserAgent). A page a page
  // opens has a DesktopSite of its own, which decides for its site.
  blink::UserAgentOverride desktop;
  desktop.ua_string_override = DesktopUserAgent();
  desktop.ua_metadata_override = DesktopUserAgentMetadata();
  contents->SetUserAgentOverride(desktop, /*override_in_new_tabs=*/false);
}

DesktopSite::~DesktopSite() = default;

void DesktopSite::DidStartNavigation(content::NavigationHandle* handle) {
  if (!handle->IsInPrimaryMainFrame() || handle->IsSameDocument()) {
    return;
  }
  // Back and forward keep what the page had.
  if (handle->GetPageTransition() & ui::PAGE_TRANSITION_FORWARD_BACK) {
    return;
  }
  const GURL& url = handle->GetURL();
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  handle->SetIsOverridingUserAgent(WantsDesktop(web_contents(), url));
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(DesktopSite);

}  // namespace venetium_w10m
