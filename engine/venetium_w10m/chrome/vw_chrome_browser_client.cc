// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_chrome_browser_client.h"

#include <utility>

#include "base/command_line.h"
#include "chrome/browser/chrome_browser_main.h"
#include "components/embedder_support/switches.h"
#include "components/embedder_support/user_agent_utils.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "venetium_w10m/browser/vw_browser_client.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/chrome/vw_chrome_extra_parts.h"
#include "venetium_w10m/chrome/vw_desktop_site.h"
#include "venetium_w10m/chrome/vw_device_access.h"
#include "venetium_w10m/common/vw_user_agent.h"
#include "content/public/browser/site_instance.h"
#include "content/public/common/url_constants.h"
#include "content/public/browser/security_principal.h"

namespace venetium_w10m {

VwChromeBrowserClient::VwChromeBrowserClient() = default;
VwChromeBrowserClient::~VwChromeBrowserClient() = default;

std::unique_ptr<content::BrowserMainParts>
VwChromeBrowserClient::CreateBrowserMainParts(bool is_integration_test) {
  std::unique_ptr<content::BrowserMainParts> parts =
      ChromeContentBrowserClient::CreateBrowserMainParts(is_integration_test);
  // After all of Chrome's own: each stage of ours runs once Chrome's parts
  // have done theirs.
  static_cast<ChromeBrowserMainParts*>(parts.get())
      ->AddParts(std::make_unique<VwChromeExtraParts>());
  return parts;
}

namespace {

// An agent of the user's own (--user-agent in the engine switches): another
// browser's, an older Chrome's.
std::string CustomUserAgent() {
  return base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      embedder_support::kUserAgent);
}

}  // namespace

std::string VwChromeBrowserClient::GetUserAgent() {
  const std::string custom = CustomUserAgent();
  return custom.empty() ? MobileUserAgent() : custom;
}

blink::UserAgentMetadata VwChromeBrowserClient::GetUserAgentMetadata() {
  // A custom agent's client hints are Chrome's minimal ones, which claim
  // nothing a custom agent may contradict.
  return CustomUserAgent().empty() ? MobileUserAgentMetadata()
                                   : embedder_support::GetUserAgentMetadata();
}

void VwChromeBrowserClient::OnWebContentsCreated(
    content::WebContents* web_contents) {
  ChromeContentBrowserClient::OnWebContentsCreated(web_contents);
  // Every page's navigations ask for the site's desktop or phone pages.
  DesktopSite::CreateForWebContents(web_contents);
}

namespace {

// Chrome's own pages (chrome://history, settings, downloads, bookmarks...)
// are desktop pages with no <meta name=viewport>: laid out at 980 CSS pixels
// and zoomed out to fit a phone, their text was tiny. Without the mobile
// viewport they are laid out at the phone's width, as in a narrow desktop
// window, and take their own narrow layouts (the side menu in a drawer) at a
// readable size. Returns whether |prefs| changed.
bool ApplyChromePageLayout(content::SiteInstance& main_frame_site,
                           blink::web_pref::WebPreferences* prefs) {
  const content::SecurityPrincipal& site =
      main_frame_site.GetSecurityPrincipal();
  const bool chrome_page = site.SchemeIs(content::kChromeUIScheme) ||
                           site.SchemeIs(content::kChromeUIUntrustedScheme);
  if (prefs->viewport_enabled == !chrome_page) {
    return false;
  }
  prefs->viewport_enabled = !chrome_page;
  return true;
}

}  // namespace

void VwChromeBrowserClient::OverrideWebPreferences(
    content::WebContents* web_contents,
    content::SiteInstance& main_frame_site,
    blink::web_pref::WebPreferences* prefs) {
  ChromeContentBrowserClient::OverrideWebPreferences(web_contents,
                                                     main_frame_site, prefs);
  ApplyPhoneWebPreferences(!VwEngine::Get()->desktop_mode.load(), prefs);
  ApplyChromePageLayout(main_frame_site, prefs);
}

bool VwChromeBrowserClient::OverrideWebPreferencesAfterNavigation(
    content::WebContents* web_contents,
    content::SiteInstance& main_frame_site,
    blink::web_pref::WebPreferences* prefs) {
  bool changed = ChromeContentBrowserClient::
      OverrideWebPreferencesAfterNavigation(web_contents, main_frame_site,
                                            prefs);
  changed |= ApplyChromePageLayout(main_frame_site, prefs);
  return changed;
}

std::optional<bool> VwChromeBrowserClient::W10mDeviceAccess(int kind) {
  return device_access::Decided(kind);
}

void VwChromeBrowserClient::W10mAskDeviceAccess(int kind,
                                                base::OnceClosure done) {
  device_access::Ask(kind, std::move(done));
}

}  // namespace venetium_w10m
