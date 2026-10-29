// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The phone's own consent for the app to use the camera, the microphone and
// the location (Settings > Privacy), which Windows keeps apart from a site's
// permission: Chrome asks the user about the site, then about this (the
// ContentBrowserClient's W10mDeviceAccess, patch 0091). Windows says whether
// the user has decided; when they have not -- or have turned it off for the
// app -- the shell, on the app's UI thread where alone the system's prompt
// can be raised, asks them or takes them to the setting
// (events.device_access), and answers (venetium_w10m_device_access_reply).

#ifndef VENETIUM_W10M_CHROME_VW_DEVICE_ACCESS_H_
#define VENETIUM_W10M_CHROME_VW_DEVICE_ACCESS_H_

#include <optional>

#include "base/functional/callback.h"

namespace venetium_w10m::device_access {

// kind: 1 the camera, 2 the microphone, 3 the location. UI thread.

// Whether the app may use it, or nothing while the user is still to be
// asked (once a run).
std::optional<bool> Decided(int kind);
// Asks the user through the shell; |done| runs when they have answered.
void Ask(int kind, base::OnceClosure done);
// The shell's answer.
void Answered(int kind, bool allowed);

}  // namespace venetium_w10m::device_access

#endif  // VENETIUM_W10M_CHROME_VW_DEVICE_ACCESS_H_
