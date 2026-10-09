// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// A tab: one WebContents, its delegate and its observer. What the shell
// shows about it -- URL, title, progress, whether back and forward can go
// anywhere, whether the page is secure -- is sent whenever it changes
// (venetium_w10m_events.tab_state). Windows a page opens become tabs.

#ifndef VENETIUM_W10M_BROWSER_VW_TAB_H_
#define VENETIUM_W10M_BROWSER_VW_TAB_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "url/gurl.h"

namespace aura {
class Window;
}

namespace venetium_w10m {

class VwBrowser;

class VwTab : public content::WebContentsDelegate,
              public content::WebContentsObserver {
 public:
  VwTab(VwBrowser* browser,
        int32_t id,
        int32_t opener,
        std::unique_ptr<content::WebContents> contents);
  VwTab(const VwTab&) = delete;
  VwTab& operator=(const VwTab&) = delete;
  ~VwTab() override;

  int32_t id() const { return id_; }
  int32_t opener() const { return opener_; }
  content::WebContents* contents() { return contents_.get(); }
  aura::Window* view();

  void Navigate(const GURL& url);
  void GoBack();
  void GoForward();
  void Reload();
  // Follows the desktop-site switch: the page shown is loaded again with
  // the other user agent and its viewport rules, both at once.
  void SetDesktopMode(bool desktop);
  // Whether this tab's pages are the desktop ones (OverrideWebPreferences).
  bool desktop() const { return desktop_; }
  // The tab a WebContents of this browser belongs to (its delegate), or null
  // while it is being made.
  static VwTab* FromWebContents(content::WebContents* contents);
  void Stop();
  bool CanGoBack();

  bool fullscreen() const { return fullscreen_; }
  void ExitFullscreen();

  // Sends the tab's state to the shell if anything in it changed (or always,
  // with force).
  void ReportState(bool force = false);

  // content::WebContentsDelegate:
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;
  void ActivateContents(content::WebContents* contents) override;
  void CloseContents(content::WebContents* source) override;
  void LoadingStateChanged(content::WebContents* source,
                           bool should_show_loading_ui) override;
  void NavigationStateChanged(content::WebContents* source,
                              content::InvalidateTypes changed_flags) override;
  void VisibleSecurityStateChanged(content::WebContents* source) override;
  bool CanEnterFullscreenModeForTab(
      content::RenderFrameHost* requesting_frame) override;
  void EnterFullscreenModeForTab(
      content::RenderFrameHost* requesting_frame,
      const blink::mojom::FullscreenOptions& options) override;
  void ExitFullscreenModeForTab(content::WebContents* web_contents) override;
  bool IsFullscreenForTabOrPending(
      const content::WebContents* web_contents) override;
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override;
  void RunFileChooser(content::RenderFrameHost* render_frame_host,
                      scoped_refptr<content::FileSelectListener> listener,
                      const blink::mojom::FileChooserParams& params) override;
  void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      content::MediaResponseCallback callback) override;
  bool CheckMediaAccessPermission(content::RenderFrameHost* render_frame_host,
                                  const url::Origin& security_origin,
                                  blink::mojom::MediaStreamType type) override;
  void BeforeUnloadFired(content::WebContents* tab,
                         bool proceed,
                         bool* proceed_to_fire_unload) override;

  // content::WebContentsObserver:
  // (Its BeforeUnloadFired, unlike the delegate's above, is not needed.)
  using content::WebContentsObserver::BeforeUnloadFired;
  void LoadProgressChanged(double progress) override;
  void TitleWasSet(content::NavigationEntry* entry) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;

 private:
  raw_ptr<VwBrowser> browser_;
  const int32_t id_;
  const int32_t opener_;
  std::unique_ptr<content::WebContents> contents_;
  bool fullscreen_ = false;
  bool desktop_ = false;

  // What the shell was last told.
  std::string reported_url_;
  std::string reported_title_;
  float reported_progress_ = -1;
  int reported_flags_ = -1;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_TAB_H_
