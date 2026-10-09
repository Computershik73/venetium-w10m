// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// What the browser says it is.
//
// A phone with a 5-inch screen should get the pages sites make for phones,
// and today a site decides that from the user agent and its client hints.
// Saying "Windows Phone" gets the pages of 2015, made for IE Mobile and Edge
// 13 and long unmaintained; saying "Windows NT" gets desktop pages at a
// 980-pixel viewport. So the mobile agent is Chrome's reduced Android one
// ("Linux; Android 10; K", which carries no device information by design),
// and the client hints agree with it. The desktop-site switch sends Chrome's
// Windows agent instead.

#ifndef VENETIUM_W10M_COMMON_VW_USER_AGENT_H_
#define VENETIUM_W10M_COMMON_VW_USER_AGENT_H_

#include <string>

#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace venetium_w10m {

std::string MobileUserAgent();
std::string DesktopUserAgent();
blink::UserAgentMetadata MobileUserAgentMetadata();
blink::UserAgentMetadata DesktopUserAgentMetadata();

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_COMMON_VW_USER_AGENT_H_
