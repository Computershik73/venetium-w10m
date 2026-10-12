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

// Hardware video decoding's count is kept beside the guard's file, in one of
// its own: "VWV1 <stamp> <count>". It is counted at another time than the
// ways of presenting (when a page plays a video, and more than once a run),
// and that file's one line is theirs. No file is a count of 0; a file of
// another build or of other engine switches is forgotten, as theirs is.
std::wstring VideoFile(const base::FilePath& file) {
  return file.value() + L"-video";
}

int ReadVideoCount(const base::FilePath& file, uint32_t stamp, bool forget) {
  const std::wstring path = VideoFile(file);
  if (forget) {
    ::DeleteFileW(path.c_str());
    return 0;
  }
  HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return 0;
  }
  char text[32] = {};
  DWORD got = 0;
  ::ReadFile(handle, text, sizeof(text) - 1, &got, nullptr);
  ::CloseHandle(handle);
  unsigned written_by = 0;
  int count = 0;
  if (sscanf_s(text, "VWV1 %x %d", &written_by, &count) != 2 ||
      written_by != stamp || count < 0) {
    return 0;
  }
  return count;
}

void WriteVideoCount(const base::FilePath& file, uint32_t stamp, int count) {
  if (file.empty()) {
    return;
  }
  const std::wstring path = VideoFile(file);
  if (!count) {
    ::DeleteFileW(path.c_str());
    return;
  }
  char text[32];
  const int length =
      sprintf_s(text, "VWV1 %08x %d\n", static_cast<unsigned>(stamp), count);
  HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE || length <= 0) {
    return;
  }
  DWORD written = 0;
  ::WriteFile(handle, text, static_cast<DWORD>(length), &written, nullptr);
  ::FlushFileBuffers(handle);
  ::CloseHandle(handle);
}

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

void PresentGuard::Load(const base::FilePath& file,
                        const std::string& made_of,
                        bool forget) {
  base::AutoLock hold(lock_);
  file_ = file;
  // BuildStamp() follows venetium_engine.dll alone; ANGLE's two DLLs and the
  // shell are built and packed apart from it.
  stamp_ = BuildStamp();
  for (const char c : made_of) {
    stamp_ = (stamp_ ^ static_cast<uint8_t>(c)) * 16777619u;
  }
  video_ = ReadVideoCount(file, stamp_, forget);
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
  if (stamp != stamp_) {
    Note("gpu: a new build, or other engine switches; what earlier runs "
         "could not do is forgotten");
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
  if (kind != PresentKind::kAngle && *Count(kind) != 0 &&
      *Count(kind) != kGivenUp) {
    *Count(kind) = 0;
    Write();
  }
  return true;
}

void PresentGuard::Proven(PresentKind kind) {
  base::AutoLock hold(lock_);
  const bool worked =
      kind == PresentKind::kAngle ? angle_worked_ : d3d11_worked_;
  if (!worked || *Count(kind) == 0 || *Count(kind) == kGivenUp) {
    return;
  }
  *Count(kind) = 0;
  Write();
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
  NoteF("gpu: %s is left alone from now on (a new build, other engine "
        "switches, or Hardware graphics off and on, tries again)",
        Name(kind));
}

bool PresentGuard::VideoAllowed() {
  return VideoFailures() < kTries;
}

int PresentGuard::VideoFailures() {
  base::AutoLock hold(lock_);
  return video_;
}

void PresentGuard::VideoTrying() {
  int count = 0;
  {
    base::AutoLock hold(lock_);
    video_watched_ = true;
    if (video_worked_ || video_trying_ || video_suspended_) {
      return;
    }
    video_trying_ = true;
    count = ++video_;
    WriteVideoCount(file_, stamp_, video_);
  }
  NoteF("video: the driver is asked for video decoding; a run that ends "
        "here counts against it (this one would be %d of %d)",
        count, kTries);
}

void PresentGuard::VideoWorked() {
  {
    base::AutoLock hold(lock_);
    video_watched_ = false;
    if (video_worked_) {
      return;
    }
    video_worked_ = true;
    video_trying_ = false;
    if (video_ != 0) {
      video_ = 0;
      WriteVideoCount(file_, stamp_, video_);
    }
  }
  Note("video: the GPU's decoder has given frames for a few seconds; this "
       "run no longer counts against it");
}

void PresentGuard::VideoEnded() {
  base::AutoLock hold(lock_);
  video_watched_ = false;
  if (!video_trying_) {
    return;
  }
  video_trying_ = false;
  if (video_ > 0) {
    --video_;
    WriteVideoCount(file_, stamp_, video_);
  }
}

void PresentGuard::VideoSuspended(bool suspended) {
  base::AutoLock hold(lock_);
  video_suspended_ = suspended;
  if (suspended) {
    // The run did not die of its video.
    if (video_trying_) {
      video_trying_ = false;
      if (video_ > 0) {
        --video_;
        WriteVideoCount(file_, stamp_, video_);
      }
    }
  } else if (video_watched_ && !video_worked_ && !video_trying_) {
    // Resumed with a decoder that has yet to show it works.
    video_trying_ = true;
    ++video_;
    WriteVideoCount(file_, stamp_, video_);
  }
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
                               static_cast<unsigned>(stamp_), angle_,
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
