// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_tab.h"

#include <cmath>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/browser/web_contents.h"
#include "net/cert/cert_status_flags.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "ui/aura/window.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "venetium_w10m/browser/vw_browser.h"
#include "venetium_w10m/browser/vw_browser_client.h"
#include "venetium_w10m/browser/vw_dialogs.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"
#include "venetium_w10m/common/vw_user_agent.h"

namespace venetium_w10m {

VwTab::VwTab(VwBrowser* browser,
             int32_t id,
             int32_t opener,
             std::unique_ptr<content::WebContents> contents)
    : content::WebContentsObserver(contents.get()),
      browser_(browser),
      id_(id),
      opener_(opener),
      contents_(std::move(contents)) {
  contents_->SetDelegate(this);
  desktop_ = VwEngine::Get()->desktop_mode.load();
  // The desktop agent goes with every navigation made while the desktop-site
  // switch is on (NavigationController::UA_OVERRIDE_TRUE); the mobile one is
  // the browser's own.
  contents_->SetUserAgentOverride(
      blink::UserAgentOverride{DesktopUserAgent(), DesktopUserAgentMetadata()},
      /*override_in_new_tabs=*/false);
  // navigator.languages, which the renderer takes from here.
  contents_->GetMutableRendererPrefs()->accept_languages = AcceptLanguages();
  contents_->SyncRendererPrefs();
  // The preferences were made before the tab was its delegate.
  contents_->OnWebPreferencesChanged();
}

VwTab::~VwTab() {
  browser_->dialogs()->ForgetContents(contents_.get());
  contents_->SetDelegate(nullptr);
  Observe(nullptr);
}

aura::Window* VwTab::view() {
  return contents_->GetNativeView();
}

void VwTab::Navigate(const GURL& url) {
  if (!url.is_valid()) {
    return;
  }
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PageTransitionFromInt(
      ui::PAGE_TRANSITION_TYPED | ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
  params.override_user_agent =
      desktop_ ? content::NavigationController::UA_OVERRIDE_TRUE
               : content::NavigationController::UA_OVERRIDE_FALSE;
  contents_->GetController().LoadURLWithParams(params);
  contents_->Focus();
}

void VwTab::GoBack() {
  if (contents_->GetController().CanGoBack()) {
    contents_->GetController().GoBack();
  }
}

void VwTab::GoForward() {
  if (contents_->GetController().CanGoForward()) {
    contents_->GetController().GoForward();
  }
}

void VwTab::Reload() {
  // No repost warning to show: a page reached by a form is posted again.
  contents_->GetController().Reload(content::ReloadType::NORMAL,
                                    /*check_for_repost=*/false);
}

void VwTab::SetDesktopMode(bool desktop) {
  if (desktop == desktop_) {
    return;
  }
  desktop_ = desktop;
  content::NavigationController& controller = contents_->GetController();
  content::NavigationEntry* entry = controller.GetLastCommittedEntry();
  if (entry) {
    entry->SetIsOverridingUserAgent(desktop);
  }
  // The viewport and text-size settings (OverrideWebPreferences), which go
  // with the reload below: a page is never laid out by the other mode's
  // rules for longer than that.
  contents_->OnWebPreferencesChanged();
  if (!entry || entry->IsInitialEntry()) {
    return;
  }
  if (entry->GetHasPostData()) {
    // LoadOriginalRequestURL would ask for a repost warning there is no
    // dialog for, and do nothing.
    controller.Reload(content::ReloadType::NORMAL, /*check_for_repost=*/false);
  } else {
    // From the URL first asked for, so that a mobile redirect (m.example.com)
    // is undone, as Chrome's "Desktop site" does.
    controller.LoadOriginalRequestURL();
  }
}

// static
VwTab* VwTab::FromWebContents(content::WebContents* contents) {
  // Every WebContents here is a tab's, and its delegate is the tab.
  return contents ? static_cast<VwTab*>(contents->GetDelegate()) : nullptr;
}

void VwTab::Stop() {
  contents_->Stop();
}

bool VwTab::CanGoBack() {
  return contents_->GetController().CanGoBack();
}

void VwTab::ExitFullscreen() {
  if (fullscreen_) {
    contents_->ExitFullscreen(/*will_cause_resize=*/true);
  }
}

void VwTab::ReportState(bool force) {
  const GURL& url = contents_->GetVisibleURL();
  std::string url_text = url.is_valid() ? url.spec() : std::string();
  if (url_text == "about:blank") {
    url_text.clear();  // the shell shows its placeholder instead
  }
  std::string title = base::UTF16ToUTF8(contents_->GetTitle());
  const bool loading = contents_->IsLoading();
  const float progress =
      loading ? static_cast<float>(contents_->GetLoadProgress()) : 1.0f;
  auto& controller = contents_->GetController();
  const bool can_back = controller.CanGoBack();
  const bool can_forward = controller.CanGoForward();
  bool secure = false;
  if (content::NavigationEntry* entry = controller.GetVisibleEntry()) {
    const content::SSLStatus& ssl = entry->GetSSL();
    secure = url.SchemeIs("https") && ssl.certificate &&
             !net::IsCertStatusError(ssl.cert_status) &&
             !(ssl.content_status &
               content::SSLStatus::RAN_INSECURE_CONTENT);
  }
  const int flags = (loading ? 1 : 0) | (can_back ? 2 : 0) |
                    (can_forward ? 4 : 0) | (secure ? 8 : 0);
  // Progress moves in small steps; a tenth is enough for a bar.
  const bool progress_moved = std::abs(progress - reported_progress_) >= 0.1f ||
                              (progress == 1.0f) != (reported_progress_ == 1.0f);
  if (!force && url_text == reported_url_ && title == reported_title_ &&
      flags == reported_flags_ && !progress_moved) {
    return;
  }
  reported_url_ = url_text;
  reported_title_ = title;
  reported_flags_ = flags;
  reported_progress_ = progress;
  browser_->OnTabStateChanged(this);
  venetium_w10m_events events = VwEngine::Get()->events();
  if (events.tab_state) {
    events.tab_state(id_, url_text.c_str(), title.c_str(), progress,
                     loading ? 1 : 0, can_back ? 1 : 0, can_forward ? 1 : 0,
                     secure ? 1 : 0);
  }
}

// --- content::WebContentsDelegate -------------------------------------------

content::WebContents* VwTab::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  content::WebContents* target = nullptr;
  switch (params.disposition) {
    case WindowOpenDisposition::CURRENT_TAB:
      target = source;
      break;
    case WindowOpenDisposition::NEW_POPUP:
    case WindowOpenDisposition::NEW_WINDOW:
    case WindowOpenDisposition::NEW_FOREGROUND_TAB:
    case WindowOpenDisposition::NEW_BACKGROUND_TAB: {
      const bool select =
          params.disposition != WindowOpenDisposition::NEW_BACKGROUND_TAB;
      VwTab* tab = browser_->CreateTab(VwEngine::Get()->NextTabId(), GURL(),
                                       select, id_);
      target = tab ? tab->contents() : nullptr;
      break;
    }
    default:
      return nullptr;
  }
  if (!target) {
    return nullptr;
  }
  base::WeakPtr<content::NavigationHandle> handle =
      target->GetController().LoadURLWithParams(
          content::NavigationController::LoadURLParams(params));
  if (navigation_handle_callback && handle) {
    std::move(navigation_handle_callback).Run(*handle);
  }
  return target;
}

content::WebContents* VwTab::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  // Every window a page opens is a tab, sized windows (sign-in popups)
  // included: the opener relation stays, so window.opener and
  // window.close() work as the page expects.
  content::WebContents* result = new_contents.get();
  const bool select = disposition != WindowOpenDisposition::NEW_BACKGROUND_TAB;
  browser_->AdoptContents(std::move(new_contents), id_, select);
  return result;
}

void VwTab::ActivateContents(content::WebContents* contents) {
  browser_->SelectTab(id_);
}

void VwTab::CloseContents(content::WebContents* source) {
  // After this returns: the tab owns |source|.
  browser_->CloseTabSoon(id_);
}

void VwTab::LoadingStateChanged(content::WebContents* source,
                                bool should_show_loading_ui) {
  ReportState();
}

void VwTab::NavigationStateChanged(content::WebContents* source,
                                   content::InvalidateTypes changed_flags) {
  ReportState();
}

void VwTab::VisibleSecurityStateChanged(content::WebContents* source) {
  ReportState();
}

bool VwTab::CanEnterFullscreenModeForTab(
    content::RenderFrameHost* requesting_frame) {
  return true;
}

void VwTab::EnterFullscreenModeForTab(
    content::RenderFrameHost* requesting_frame,
    const blink::mojom::FullscreenOptions& options) {
  if (fullscreen_) {
    return;
  }
  fullscreen_ = true;
  NoteF("tab %d: fullscreen", id_);
  browser_->OnFullscreenChanged(this, true);
}

void VwTab::ExitFullscreenModeForTab(content::WebContents* web_contents) {
  if (!fullscreen_) {
    return;
  }
  fullscreen_ = false;
  NoteF("tab %d: fullscreen off", id_);
  browser_->OnFullscreenChanged(this, false);
}

bool VwTab::IsFullscreenForTabOrPending(
    const content::WebContents* web_contents) {
  return fullscreen_;
}

content::JavaScriptDialogManager* VwTab::GetJavaScriptDialogManager(
    content::WebContents* source) {
  return browser_->dialogs();
}

void VwTab::RunFileChooser(content::RenderFrameHost* render_frame_host,
                           scoped_refptr<content::FileSelectListener> listener,
                           const blink::mojom::FileChooserParams& params) {
  browser_->dialogs()->ChooseFiles(contents_.get(), std::move(listener),
                                   params);
}

void VwTab::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  // Camera and microphone come through the shell's capture consent later;
  // until then a page is told no, as a desktop browser says when the person
  // blocks it.
  NoteF("tab %d: camera/microphone requested, denied for now", id_);
  std::move(callback).Run(blink::mojom::StreamDevicesSet(),
                          blink::mojom::MediaStreamRequestResult::PERMISSION_DENIED,
                          nullptr);
}

bool VwTab::CheckMediaAccessPermission(
    content::RenderFrameHost* render_frame_host,
    const url::Origin& security_origin,
    blink::mojom::MediaStreamType type) {
  return false;
}

void VwTab::BeforeUnloadFired(content::WebContents* tab,
                              bool proceed,
                              bool* proceed_to_fire_unload) {
  *proceed_to_fire_unload = proceed;
}

// --- content::WebContentsObserver -------------------------------------------

void VwTab::LoadProgressChanged(double progress) {
  ReportState();
}

void VwTab::TitleWasSet(content::NavigationEntry* entry) {
  ReportState();
}

void VwTab::DidFinishNavigation(content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame()) {
    if (navigation_handle->IsErrorPage()) {
      NoteF("tab %d: %s failed, net error %d", id_,
            navigation_handle->GetURL().possibly_invalid_spec(),
            navigation_handle->GetNetErrorCode());
    } else if (navigation_handle->HasCommitted()) {
      NoteF("tab %d: at %s", id_,
            navigation_handle->GetURL().possibly_invalid_spec());
    }
  }
  ReportState();
}

void VwTab::PrimaryMainFrameRenderProcessGone(base::TerminationStatus status) {
  NoteF("tab %d: renderer gone, status %d", id_, static_cast<int>(status));
  ReportState(true);
}

}  // namespace venetium_w10m
