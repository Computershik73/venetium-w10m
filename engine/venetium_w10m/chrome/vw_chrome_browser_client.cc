// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_chrome_browser_client.h"

#include <utility>

#include "chrome/browser/chrome_browser_main.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "venetium_w10m/browser/vw_browser_client.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/chrome/vw_chrome_extra_parts.h"
#include "venetium_w10m/common/vw_user_agent.h"

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

std::string VwChromeBrowserClient::GetUserAgent() {
  return MobileUserAgent();
}

blink::UserAgentMetadata VwChromeBrowserClient::GetUserAgentMetadata() {
  return MobileUserAgentMetadata();
}

void VwChromeBrowserClient::OverrideWebPreferences(
    content::WebContents* web_contents,
    content::SiteInstance& main_frame_site,
    blink::web_pref::WebPreferences* prefs) {
  ChromeContentBrowserClient::OverrideWebPreferences(web_contents,
                                                     main_frame_site, prefs);
  ApplyPhoneWebPreferences(!VwEngine::Get()->desktop_mode.load(), prefs);
}

}  // namespace venetium_w10m
