// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_browser_client.h"

#include <utility>

#include "base/i18n/rtl.h"
#include "base/strings/string_util.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/client_certificate_delegate.h"
#include "content/public/browser/web_contents.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/ssl/client_cert_identity.h"
#include "net/ssl/ssl_private_key.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "third_party/blink/public/mojom/webpreferences/web_preferences.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/pointer/pointer_device.h"
#include "url/gurl.h"
#include "venetium_w10m/browser/vw_browser_context.h"
#include "venetium_w10m/browser/vw_browser_main_parts.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"
#include "venetium_w10m/common/vw_user_agent.h"

namespace venetium_w10m {

namespace {

base::FilePath ProfileDir() {
  return VwEngine::Get()->config().profile_dir;
}

}  // namespace

std::string ApplicationLocale() {
  // l10n_util only answers with a locale it has a locales/*.pak for, and the
  // package carries none (the engine's strings are in venetium_engine.pak):
  // the phone's own language, as headless does.
  std::string locale = l10n_util::GetApplicationLocale(std::string());
  if (locale.empty()) {
    locale = base::i18n::GetConfiguredLocale();
  }
  return locale.empty() ? "en-US" : locale;
}

std::string AcceptLanguages() {
  const std::string locale = ApplicationLocale();
  std::string langs = locale;
  const size_t dash = locale.find('-');
  if (dash != std::string::npos) {
    langs += "," + locale.substr(0, dash);
  }
  if (!base::StartsWith(locale, "en")) {
    langs += ",en-US,en";
  }
  return langs;
}

VwContentBrowserClient::VwContentBrowserClient() = default;
VwContentBrowserClient::~VwContentBrowserClient() = default;

std::unique_ptr<content::BrowserMainParts>
VwContentBrowserClient::CreateBrowserMainParts(bool is_integration_test) {
  auto parts = std::make_unique<VwBrowserMainParts>();
  main_parts_ = parts.get();
  return parts;
}

std::string VwContentBrowserClient::GetUserAgent() {
  return MobileUserAgent();
}

blink::UserAgentMetadata VwContentBrowserClient::GetUserAgentMetadata() {
  return MobileUserAgentMetadata();
}

std::string VwContentBrowserClient::GetApplicationLocale() {
  return ApplicationLocale();
}

std::string VwContentBrowserClient::GetAcceptLangs(
    content::BrowserContext* context) {
  return AcceptLanguages();
}

void VwContentBrowserClient::OverrideWebPreferences(
    content::WebContents* web_contents,
    content::SiteInstance& main_frame_site,
    blink::web_pref::WebPreferences* prefs) {
  // A touch screen, whatever GetSystemMetrics says on a phone (its user32 is
  // a stub): pages get touch events and (pointer: coarse).
  prefs->touch_event_feature_detection_enabled = true;
  prefs->pointer_events_max_touch_points = 5;
  prefs->available_pointer_types = ui::POINTER_TYPE_COARSE;
  prefs->primary_pointer_type = blink::mojom::PointerType::kPointerCoarseType;
  prefs->available_hover_types = ui::HOVER_TYPE_NONE;
  prefs->primary_hover_type = blink::mojom::HoverType::kHoverNone;
  prefs->password_echo_enabled_touch = true;

  // How Chrome for Android lays out pages: <meta name=viewport> honoured,
  // pages without one laid out at 980 CSS pixels and zoomed to fit, pinch and
  // double-tap zoom. The desktop-site switch turns all of this off.
  const bool mobile = !VwEngine::Get()->desktop_mode.load();
  prefs->viewport_enabled = true;
  prefs->viewport_meta_enabled = mobile;
  prefs->viewport_style = mobile ? blink::mojom::ViewportStyle::kMobile
                                 : blink::mojom::ViewportStyle::kDefault;
  prefs->shrinks_viewport_contents_to_fit = true;
  prefs->main_frame_resizes_are_orientation_changes = true;
  prefs->double_tap_to_zoom_enabled = true;
  prefs->auto_zoom_focused_editable_to_legible_scale = mobile;
  prefs->text_size_adjust_enabled = mobile;
  prefs->default_minimum_page_scale_factor = 0.25f;
  prefs->default_maximum_page_scale_factor = 5.0f;
  prefs->initialize_at_minimum_page_scale = true;
  prefs->smooth_scroll_for_find_enabled = true;
}

void VwContentBrowserClient::ConfigureNetworkContextParams(
    content::BrowserContext* context,
    bool in_memory,
    const base::FilePath& relative_partition_path,
    network::mojom::NetworkContextParams* params,
    cert_verifier::mojom::CertVerifierCreationParams*
        cert_verifier_creation_params) {
  params->user_agent = GetUserAgent();
  params->accept_language = GetAcceptLangs(context);
  params->enable_zstd = true;
  // A phone ends a suspended app without warning, which looks to the
  // browser like a session that ended: session cookies are kept so a
  // relaunch is still signed in.
  params->persist_session_cookies = true;
  params->restore_old_session_cookies = true;
  // Cookies on disk unencrypted: there is no OSCrypt key store here, and
  // without a cookie_encryption_provider an encrypting store is NOTREACHED.
  // The profile is in the app's own container, which only it can read.
  params->enable_encrypted_cookies = false;
  // No system proxy service: ProxyConfigServiceWin reads Internet Explorer's
  // settings through WinHTTP and registry watchers the phone does not have.
  params->initial_proxy_config =
      net::ProxyConfigWithAnnotation::CreateDirect();
  params->http_cache_enabled = true;
  params->http_cache_max_size = 64 * 1024 * 1024;

  if (in_memory) {
    return;
  }
  const base::FilePath path =
      context->GetPath().Append(relative_partition_path);
  params->file_paths = network::mojom::NetworkContextFilePaths::New();
  params->file_paths->data_directory = path.Append(FILE_PATH_LITERAL("Network"));
  params->file_paths->unsandboxed_data_path = path;
  params->file_paths->trigger_migration = true;
  params->file_paths->cookie_database_name =
      base::FilePath(FILE_PATH_LITERAL("Cookies"));
  params->file_paths->http_server_properties_file_name =
      base::FilePath(FILE_PATH_LITERAL("Network Persistent State"));
  params->file_paths->transport_security_persister_file_name =
      base::FilePath(FILE_PATH_LITERAL("TransportSecurity"));
  params->file_paths->trust_token_database_name =
      base::FilePath(FILE_PATH_LITERAL("Trust Tokens"));
  params->file_paths->http_cache_directory =
      static_cast<VwBrowserContext*>(context)->cache_path().Append(
          FILE_PATH_LITERAL("HTTP"));
}

std::vector<base::FilePath>
VwContentBrowserClient::GetNetworkContextsParentDirectory() {
  return {ProfileDir()};
}

bool VwContentBrowserClient::HandleExternalProtocol(
    const GURL& url,
    base::RepeatingCallback<content::WebContents*()> web_contents_getter,
    content::FrameTreeNodeId frame_tree_node_id,
    content::NavigationUIData* navigation_data,
    bool is_primary_main_frame,
    bool is_in_fenced_frame_tree,
    network::mojom::WebSandboxFlags sandbox_flags,
    ui::PageTransition page_transition,
    bool has_user_gesture,
    const std::optional<url::Origin>& initiating_origin,
    content::RenderFrameHost* initiator_document,
    const net::IsolationInfo& isolation_info,
    mojo::PendingRemote<network::mojom::URLLoaderFactory>* out_factory) {
  // mailto:, tel:, ms-settings:, intent-less app links: the phone opens them.
  // Only with a gesture, so a page cannot start apps by itself.
  if (!has_user_gesture) {
    NoteF("external protocol without a gesture ignored: %s", url.scheme());
    return true;
  }
  venetium_w10m_events events = VwEngine::Get()->events();
  if (events.launch_uri) {
    const std::string spec = url.spec();
    events.launch_uri(spec.c_str());
  }
  return true;
}

base::FilePath VwContentBrowserClient::GetDefaultDownloadDirectory() {
  return ProfileDir().DirName().Append(FILE_PATH_LITERAL("Downloads"));
}

base::FilePath VwContentBrowserClient::GetShaderDiskCacheDirectory() {
  return VwEngine::Get()->config().cache_dir.Append(
      FILE_PATH_LITERAL("ShaderCache"));
}

base::FilePath VwContentBrowserClient::GetGrShaderDiskCacheDirectory() {
  return VwEngine::Get()->config().cache_dir.Append(
      FILE_PATH_LITERAL("GrShaderCache"));
}

base::FilePath VwContentBrowserClient::GetGraphiteDawnDiskCacheDirectory() {
  return base::FilePath();
}

base::FilePath VwContentBrowserClient::GetNetLogDefaultDirectory() {
  return ProfileDir();
}

base::FilePath VwContentBrowserClient::GetFirstPartySetsDirectory() {
  return ProfileDir();
}

std::optional<base::FilePath>
VwContentBrowserClient::GetLocalTracesDirectory() {
  return std::nullopt;
}

base::FilePath
VwContentBrowserClient::GetSandboxedStorageServiceDataDirectory() {
  return ProfileDir();
}

bool VwContentBrowserClient::ShouldSandboxNetworkService() {
  return false;  // one process, inside the app container
}

bool VwContentBrowserClient::ShouldSandboxAudioService() {
  return false;
}

bool VwContentBrowserClient::IsShuttingDown() {
  return VwEngine::Get()->shutting_down();
}

base::OnceClosure VwContentBrowserClient::SelectClientCertificate(
    content::BrowserContext* browser_context,
    int process_id,
    content::WebContents* web_contents,
    net::SSLCertRequestInfo* cert_request_info,
    net::ClientCertIdentityList client_certs,
    std::unique_ptr<content::ClientCertificateDelegate> delegate) {
  // No client certificates on the phone: go on without one, which a server
  // asking for one optionally accepts.
  delegate->ContinueWithCertificate(nullptr, nullptr);
  return base::OnceClosure();
}

}  // namespace venetium_w10m
