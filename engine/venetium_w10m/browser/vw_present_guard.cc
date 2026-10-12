// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_present_guard.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include <string>

#include "base/win/current_module.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m {

namespace {

// A count no run reaches by dying: given up without a crash.
constexpr int kGivenUp = 9;

// This build of the engine: the linker's time stamp, and the signature of
// its debug record, which follows what was linked (the time stamp alone is
// the same for a month of builds).
uint32_t BuildStamp() {
  const auto* base = reinterpret_cast<const uint8_t*>(CURRENT_MODULE());
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  uint32_t stamp = nt->FileHeader.TimeDateStamp;
  const IMAGE_DATA_DIRECTORY& directory =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
  if (!directory.VirtualAddress) {
    return stamp;
  }
  const auto* entry = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(
      base + directory.VirtualAddress);
  for (DWORD i = 0; i < directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i) {
    // "RSDS", a GUID, an age.
    uint32_t words[5] = {};
    if (entry[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
        entry[i].SizeOfData < sizeof(words) || !entry[i].AddressOfRawData) {
      continue;
    }
    memcpy(words, base + entry[i].AddressOfRawData, sizeof(words));
    if (words[0] == 0x53445352) {
      stamp ^= words[1] ^ words[2] ^ words[3] ^ words[4];
    }
  }
  return stamp;
}

const char* Name(PresentKind kind) {
  return kind == PresentKind::kAngle ? "GPU compositing"
                                     : "presenting through Direct3D 11";
}

}  // namespace

// static
PresentGuard* PresentGuard::Get() {
  static PresentGuard guard;
  return &guard;
}

void PresentGuard::Load(const base::FilePath& file, bool forget) {
  base::AutoLock hold(lock_);
  file_ = file;
  angle_ = 0;
  d3d11_ = 0;
  if (forget) {
    ::DeleteFileW(file.value().c_str());
    Note("gpu: what earlier runs could not do is forgotten (asked to retry)");
    return;
  }
  HANDLE handle = ::CreateFileW(file.value().c_str(), GENERIC_READ,
                                FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                                nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return;
  }
  char text[64] = {};
  DWORD got = 0;
  ::ReadFile(handle, text, sizeof(text) - 1, &got, nullptr);
  ::CloseHandle(handle);
  unsigned stamp = 0;
  int angle = 0;
  int d3d11 = 0;
  // An older engine's file (a bare count) does not parse, and is dropped.
  if (sscanf_s(text, "VWG1 %x %d %d", &stamp, &angle, &d3d11) != 3) {
    return;
  }
  if (stamp != BuildStamp()) {
    Note("gpu: a new build of the engine; what the last one could not do "
         "is forgotten");
    return;
  }
  angle_ = angle;
  d3d11_ = d3d11;
}

int* PresentGuard::Count(PresentKind kind) {
  return kind == PresentKind::kAngle ? &angle_ : &d3d11_;
}

bool PresentGuard::Allowed(PresentKind kind) {
  return Failures(kind) < kTries;
}

int PresentGuard::Failures(PresentKind kind) {
  base::AutoLock hold(lock_);
  return *Count(kind);
}

void PresentGuard::Trying(PresentKind kind) {
  base::AutoLock hold(lock_);
  ++*Count(kind);
  Write();
}

bool PresentGuard::Worked(PresentKind kind) {
  base::AutoLock hold(lock_);
  bool& worked =
      kind == PresentKind::kAngle ? angle_worked_ : d3d11_worked_;
  if (worked) {
    return false;
  }
  worked = true;
  if (*Count(kind) != 0 && *Count(kind) != kGivenUp) {
    *Count(kind) = 0;
    Write();
  }
  return true;
}

void PresentGuard::GaveUp(PresentKind kind) {
  base::AutoLock hold(lock_);
  const bool worked =
      kind == PresentKind::kAngle ? angle_worked_ : d3d11_worked_;
  if (worked) {
    // It did work on this phone, and broke later: the next run tries again.
    return;
  }
  if (*Count(kind) == kGivenUp) {
    return;
  }
  *Count(kind) = kGivenUp;
  Write();
  NoteF("gpu: %s is left alone from now on (a new build, or Hardware "
        "graphics off and on, tries again)",
        Name(kind));
}

void PresentGuard::Write() {
  if (file_.empty()) {
    return;
  }
  if (!angle_ && !d3d11_) {
    ::DeleteFileW(file_.value().c_str());
    return;
  }
  char text[64];
  const int length = sprintf_s(text, "VWG1 %08x %d %d\n",
                               static_cast<unsigned>(BuildStamp()), angle_,
                               d3d11_);
  HANDLE handle = ::CreateFileW(file_.value().c_str(), GENERIC_WRITE, 0,
                                nullptr, CREATE_ALWAYS, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE || length <= 0) {
    return;
  }
  DWORD written = 0;
  ::WriteFile(handle, text, static_cast<DWORD>(length), &written, nullptr);
  ::FlushFileBuffers(handle);
  ::CloseHandle(handle);
}

}  // namespace venetium_w10m
