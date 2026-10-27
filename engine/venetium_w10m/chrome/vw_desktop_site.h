// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// "Desktop site", as Chrome for Android has it: each page's navigation asks
// for the site's desktop pages (Chrome's Windows user agent and client
// hints) or its phone pages (vw_user_agent.h). The menu's "Desktop site" is
// remembered per site; "Desktop site by default" (the Windows 10 Mobile
// submenu) or Continuum's mouse and monitor make desktop pages the rule.
// The Chrome Web Store, which turns phones away, always gets the desktop.

#ifndef VENETIUM_W10M_CHROME_VW_DESKTOP_SITE_H_
#define VENETIUM_W10M_CHROME_VW_DESKTOP_SITE_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace venetium_w10m {

class DesktopSite : public content::WebContentsObserver,
                    public content::WebContentsUserData<DesktopSite> {
 public:
  DesktopSite(const DesktopSite&) = delete;
  DesktopSite& operator=(const DesktopSite&) = delete;
  ~DesktopSite() override;

  // content::WebContentsObserver:
  void DidStartNavigation(content::NavigationHandle* handle) override;

 private:
  friend class content::WebContentsUserData<DesktopSite>;
  explicit DesktopSite(content::WebContents* contents);

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_CHROME_VW_DESKTOP_SITE_H_
