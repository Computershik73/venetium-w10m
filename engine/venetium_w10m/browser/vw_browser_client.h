// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#ifndef VENETIUM_W10M_BROWSER_VW_BROWSER_CLIENT_H_
#define VENETIUM_W10M_BROWSER_VW_BROWSER_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/content_browser_client.h"

namespace venetium_w10m {

class VwBrowserMainParts;

// The locale the engine runs in, and the Accept-Language list made from it.
std::string ApplicationLocale();
std::string AcceptLanguages();

class VwContentBrowserClient : public content::ContentBrowserClient {
 public:
  VwContentBrowserClient();
  VwContentBrowserClient(const VwContentBrowserClient&) = delete;
  VwContentBrowserClient& operator=(const VwContentBrowserClient&) = delete;
  ~VwContentBrowserClient() override;

  // content::ContentBrowserClient:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
  std::string GetUserAgent() override;
  blink::UserAgentMetadata GetUserAgentMetadata() override;
  std::string GetApplicationLocale() override;
  std::string GetAcceptLangs(content::BrowserContext* context) override;
  void OverrideWebPreferences(content::WebContents* web_contents,
                              content::SiteInstance& main_frame_site,
                              blink::web_pref::WebPreferences* prefs) override;
  void ConfigureNetworkContextParams(
      content::BrowserContext* context,
      bool in_memory,
      const base::FilePath& relative_partition_path,
      network::mojom::NetworkContextParams* network_context_params,
      cert_verifier::mojom::CertVerifierCreationParams*
          cert_verifier_creation_params) override;
  std::vector<base::FilePath> GetNetworkContextsParentDirectory() override;
  bool HandleExternalProtocol(
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
      mojo::PendingRemote<network::mojom::URLLoaderFactory>* out_factory)
      override;
  base::FilePath GetDefaultDownloadDirectory() override;
  base::FilePath GetShaderDiskCacheDirectory() override;
  base::FilePath GetGrShaderDiskCacheDirectory() override;
  base::FilePath GetGraphiteDawnDiskCacheDirectory() override;
  base::FilePath GetNetLogDefaultDirectory() override;
  base::FilePath GetFirstPartySetsDirectory() override;
  std::optional<base::FilePath> GetLocalTracesDirectory() override;
  base::FilePath GetSandboxedStorageServiceDataDirectory() override;
  bool ShouldSandboxNetworkService() override;
  bool ShouldSandboxAudioService() override;
  bool IsShuttingDown() override;
  base::OnceClosure SelectClientCertificate(
      content::BrowserContext* browser_context,
      int process_id,
      content::WebContents* web_contents,
      net::SSLCertRequestInfo* cert_request_info,
      net::ClientCertIdentityList client_certs,
      std::unique_ptr<content::ClientCertificateDelegate> delegate) override;

 private:
  raw_ptr<VwBrowserMainParts> main_parts_ = nullptr;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_BROWSER_VW_BROWSER_CLIENT_H_
