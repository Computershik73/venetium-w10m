// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/common/vw_user_agent.h"

#include "base/strings/strcat.h"
#include "base/version_info/version_info.h"
#include "components/embedder_support/user_agent_utils.h"

namespace venetium_w10m {

namespace {

// Reduced user agents carry only the major version: "150.0.0.0".
std::string ReducedVersion() {
  return base::StrCat({version_info::GetMajorVersionNumber(), ".0.0.0"});
}

blink::UserAgentMetadata BaseMetadata() {
  // Brands (Chromium, the GREASE brand) and the full version, as Chromium
  // computes them.
  blink::UserAgentMetadata metadata = embedder_support::GetUserAgentMetadata();
  metadata.architecture = "arm";
  metadata.bitness = "32";
  metadata.wow64 = false;
  metadata.model = "";
  return metadata;
}

}  // namespace

std::string MobileUserAgent() {
  return base::StrCat({"Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 "
                       "(KHTML, like Gecko) Chrome/",
                       ReducedVersion(), " Mobile Safari/537.36"});
}

std::string DesktopUserAgent() {
  return base::StrCat({"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                       "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/",
                       ReducedVersion(), " Safari/537.36"});
}

blink::UserAgentMetadata MobileUserAgentMetadata() {
  blink::UserAgentMetadata metadata = BaseMetadata();
  metadata.platform = "Android";
  metadata.platform_version = "10.0.0";
  metadata.mobile = true;
  metadata.form_factors = {blink::kMobileFormFactor};
  return metadata;
}

blink::UserAgentMetadata DesktopUserAgentMetadata() {
  blink::UserAgentMetadata metadata = BaseMetadata();
  metadata.platform = "Windows";
  metadata.platform_version = "10.0.0";
  metadata.mobile = false;
  metadata.architecture = "x86";
  metadata.bitness = "64";
  metadata.form_factors = {blink::kDesktopFormFactor};
  return metadata;
}

}  // namespace venetium_w10m
