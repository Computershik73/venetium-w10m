// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#ifndef VENETIUM_W10M_APP_VW_CONTENT_CLIENT_H_
#define VENETIUM_W10M_APP_VW_CONTENT_CLIENT_H_

#include <string>
#include <string_view>

#include "content/public/common/content_client.h"

namespace venetium_w10m {

// Resources and strings come from venetium_engine.pak (BUILD.gn).
class VwContentClient : public content::ContentClient {
 public:
  VwContentClient();
  VwContentClient(const VwContentClient&) = delete;
  VwContentClient& operator=(const VwContentClient&) = delete;
  ~VwContentClient() override;

  // content::ContentClient:
  std::u16string GetLocalizedString(int message_id) override;
  std::string_view GetDataResource(
      int resource_id,
      ui::ResourceScaleFactor scale_factor) override;
  base::RefCountedMemory* GetDataResourceBytes(int resource_id) override;
  std::string GetDataResourceString(int resource_id) override;
  gfx::Image& GetNativeImageNamed(int resource_id) override;
};

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_APP_VW_CONTENT_CLIENT_H_
