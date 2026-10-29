// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/chrome/vw_device_access.h"

#include <windows.devices.enumeration.h>
#include <wrl/client.h>

#include <utility>
#include <vector>

#include "base/no_destructor.h"
#include "base/win/core_winrt_util.h"
#include "venetium_w10m/browser/vw_engine.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m::device_access {

namespace {

using ABI::Windows::Devices::Enumeration::DeviceAccessStatus;
using ABI::Windows::Devices::Enumeration::DeviceClass;
using ABI::Windows::Devices::Enumeration::IDeviceAccessInformation;
using ABI::Windows::Devices::Enumeration::IDeviceAccessInformationStatics;
using Microsoft::WRL::ComPtr;

constexpr int kKinds = 4;

struct State {
  // What the user answered this run, by kind.
  std::optional<bool> answered[kKinds];
  // Who waits for their answer, by kind.
  std::vector<base::OnceClosure> waiting[kKinds];
};

State& GetState() {
  static base::NoDestructor<State> state;
  return *state;
}

bool Known(int kind) {
  return kind >= 1 && kind < kKinds;
}

const char* Name(int kind) {
  switch (kind) {
    case 1:
      return "camera";
    case 2:
      return "microphone";
    default:
      return "location";
  }
}

// What Windows has recorded for the app; Unspecified when it cannot tell.
DeviceAccessStatus SystemStatus(int kind) {
  ComPtr<IDeviceAccessInformationStatics> statics;
  HRESULT hr = base::win::GetActivationFactory<
      IDeviceAccessInformationStatics,
      RuntimeClass_Windows_Devices_Enumeration_DeviceAccessInformation>(
      &statics);
  if (FAILED(hr)) {
    return DeviceAccessStatus::DeviceAccessStatus_Unspecified;
  }
  ComPtr<IDeviceAccessInformation> info;
  hr = statics->CreateFromDeviceClass(
      kind == 1   ? DeviceClass::DeviceClass_VideoCapture
      : kind == 2 ? DeviceClass::DeviceClass_AudioCapture
                  : DeviceClass::DeviceClass_Location,
      &info);
  if (FAILED(hr)) {
    return DeviceAccessStatus::DeviceAccessStatus_Unspecified;
  }
  auto status = DeviceAccessStatus::DeviceAccessStatus_Unspecified;
  info->get_CurrentStatus(&status);
  return status;
}

}  // namespace

std::optional<bool> Decided(int kind) {
  if (!Known(kind)) {
    return true;
  }
  const std::optional<bool>& answered = GetState().answered[kind];
  switch (SystemStatus(kind)) {
    case DeviceAccessStatus::DeviceAccessStatus_Allowed:
      return true;
    case DeviceAccessStatus::DeviceAccessStatus_DeniedByUser:
    case DeviceAccessStatus::DeviceAccessStatus_DeniedBySystem:
      // Off -- for this app, or for all of them: the shell takes the user to
      // the setting, once a run; after that, no.
      if (answered.has_value()) {
        return false;
      }
      return std::nullopt;
    default:
      // Never asked. The prompt's answer is not always recorded as Allowed
      // (the system may leave it unspecified): what the user said stands.
      return answered;
  }
}

void Ask(int kind, base::OnceClosure done) {
  if (!Known(kind)) {
    std::move(done).Run();
    return;
  }
  std::vector<base::OnceClosure>& waiting = GetState().waiting[kind];
  waiting.push_back(std::move(done));
  if (waiting.size() > 1) {
    return;  // Already asked.
  }
  const venetium_w10m_events events = VwEngine::Get()->events();
  if (!events.device_access) {
    Answered(kind, true);
    return;
  }
  NoteF("permissions: asking the phone for the %s", Name(kind));
  events.device_access(kind);
}

void Answered(int kind, bool allowed) {
  if (!Known(kind)) {
    return;
  }
  State& state = GetState();
  state.answered[kind] = allowed;
  NoteF("permissions: the phone %s the %s", allowed ? "gives" : "refuses",
        Name(kind));
  std::vector<base::OnceClosure> waiting = std::move(state.waiting[kind]);
  state.waiting[kind].clear();
  for (base::OnceClosure& done : waiting) {
    std::move(done).Run();
  }
}

}  // namespace venetium_w10m::device_access
