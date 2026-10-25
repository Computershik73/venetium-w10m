// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// Chrome's browser client with the phone's user agent and web preferences,
// and the engine's stages of startup added to Chrome's (VwChromeExtraParts).

#ifndef VENETIUM_W10M_CHROME_VW_CHROME_BROWSER_CLIENT_H_
#define VENETIUM_W10M_CHROME_VW_CHROME_BROWSER_CLIENT_H_

#include <memory>
#include <string>

#include "chrome/browser/chrome_content_browser_client.h"

namespace venetium_w10m {

class VwChromeBrowserClient : public ChromeContentBrowserClient {
 public:
  VwChromeBrowserClient();
  VwChromeBrowserClient(const VwChromeBrowserClient&) = delete;
  VwChromeBrowserClient& operator=(const VwChromeBrowserClient&) = delete;
  ~VwChromeBrowserClient() override;

  // ChromeContentBrowserClient:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
  std::string GetUserAgent() override;
  blink::UserAgentMetadata GetUserAgentMetadata() override;
  void OverrideWebPreferences(content::WebContents* web_contents,
                              content::SiteInstance& main_frame_site,
                              blink::web_pref::WebPreferences* prefs) override;
  bool OverrideWebPreferencesAfterNavigation(
      content::WebContents* web_contents,
      content::SiteInstance& main_frame_site,
      blink::web_pref::WebPreferences* prefs) override;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_CHROME_BROWSER_CLIENT_H_
