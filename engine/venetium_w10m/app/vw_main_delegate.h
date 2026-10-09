// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#ifndef VENETIUM_W10M_APP_VW_MAIN_DELEGATE_H_
#define VENETIUM_W10M_APP_VW_MAIN_DELEGATE_H_

#include <memory>
#include <optional>

#include "content/public/app/content_main_delegate.h"

namespace venetium_w10m {

class VwContentBrowserClient;
class VwContentClient;
class VwContentRendererClient;

class VwMainDelegate : public content::ContentMainDelegate {
 public:
  VwMainDelegate();
  VwMainDelegate(const VwMainDelegate&) = delete;
  VwMainDelegate& operator=(const VwMainDelegate&) = delete;
  ~VwMainDelegate() override;

  // content::ContentMainDelegate:
  std::optional<int> BasicStartupComplete() override;
  void PreSandboxStartup() override;
  content::ContentClient* CreateContentClient() override;
  content::ContentBrowserClient* CreateContentBrowserClient() override;
  content::ContentRendererClient* CreateContentRendererClient() override;

 private:
  std::unique_ptr<VwContentClient> content_client_;
  std::unique_ptr<VwContentBrowserClient> browser_client_;
  std::unique_ptr<VwContentRendererClient> renderer_client_;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_APP_VW_MAIN_DELEGATE_H_
