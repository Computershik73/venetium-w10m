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
  return kind == PresentKind::kAngle        ? "GPU compositing"
         : kind == PresentKind::kCompositor ? "the Direct3D 11 compositor"
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
  angle_ = Way();
  d3d11_ = Way();
  compositor_ = Way();
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
  int compositor = 0;
  // An older engine's file (a bare count) does not parse, and is dropped.
  // One written before the compositor's count was kept has three numbers:
  // the fourth is 0 then.
  if (sscanf_s(text, "VWG1 %x %d %d %d", &stamp, &angle, &d3d11,
               &compositor) < 3) {
    return;
  }
  if (stamp != stamp_) {
    Note("gpu: a new build, or other engine switches; what earlier runs "
         "could not do is forgotten");
    return;
  }
  angle_.count = angle;
  d3d11_.count = d3d11;
  compositor_.count = compositor;
}

PresentGuard::Way* PresentGuard::WayOf(PresentKind kind) {
  return kind == PresentKind::kAngle        ? &angle_
         : kind == PresentKind::kCompositor ? &compositor_
                                            : &d3d11_;
}

bool PresentGuard::Allowed(PresentKind kind) {
  return Failures(kind) < kTries;
}

int PresentGuard::Failures(PresentKind kind) {
  base::AutoLock hold(lock_);
  return WayOf(kind)->count;
}

void PresentGuard::Trying(PresentKind kind) {
  base::AutoLock hold(lock_);
  Way* way = WayOf(kind);
  if (way->counted || way->count == kGivenUp) {
    return;
  }
  ++way->count;
  way->counted = true;
  Write();
}

void PresentGuard::Entered(PresentKind kind) {
  base::AutoLock hold(lock_);
  Way* way = WayOf(kind);
  way->entered = true;
  if (!way->untried) {
    return;
  }
  // Suspended before it had done anything, and doing it now: the run counts
  // again.
  way->untried = false;
  if (way->worked || way->count == kGivenUp) {
    return;
  }
  ++way->count;
  way->counted = true;
  Write();
}

void PresentGuard::Left(PresentKind kind) {
  base::AutoLock hold(lock_);
  WayOf(kind)->entered = false;
}

void PresentGuard::Untried(PresentKind kind) {
  base::AutoLock hold(lock_);
  Way* way = WayOf(kind);
  if (!way->counted || way->entered || way->worked || way->count <= 0 ||
      way->count == kGivenUp) {
    return;
  }
  // Not before the swap chain the compositor shares with software frames
  // has shown one of those: a run that has put nothing on screen at all and
  // is closed may be one that hung on the way there, and it started with
  // |kind|, so nothing else counts it.
  if (kind == PresentKind::kCompositor && !d3d11_.worked) {
    return;
  }
  --way->count;
  way->counted = false;
  way->untried = true;
  Write();
}

void PresentGuard::Survived(PresentKind kind) {
  base::AutoLock hold(lock_);
  Way* way = WayOf(kind);
  // Nothing follows this in the run: no Entered() counts it again.
  way->untried = false;
  if (!way->counted || way->count <= 0 || way->count == kGivenUp) {
    return;
  }
  --way->count;
  way->counted = false;
  Write();
}

bool PresentGuard::Worked(PresentKind kind) {
  base::AutoLock hold(lock_);
  Way* way = WayOf(kind);
  if (way->worked) {
    return false;
  }
  way->worked = true;
  // Frames through plain Direct3D 11 are proof at once; the two that
  // composite on the GPU wait for Proven().
  if (kind == PresentKind::kD3D11 && way->count != 0 &&
      way->count != kGivenUp) {
    way->count = 0;
    way->counted = false;
    Write();
  }
  return true;
}

void PresentGuard::Proven(PresentKind kind) {
  base::AutoLock hold(lock_);
  Way* way = WayOf(kind);
  if (!way->worked || way->count == 0 || way->count == kGivenUp) {
    return;
  }
  way->count = 0;
  way->counted = false;
  Write();
}

void PresentGuard::GaveUp(PresentKind kind) {
  base::AutoLock hold(lock_);
  Way* way = WayOf(kind);
  if (way->worked) {
    // It did work on this phone, and broke later: the next run tries again.
    return;
  }
  if (way->count == kGivenUp) {
    return;
  }
  way->count = kGivenUp;
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
    if (video_trying_ || video_suspended_) {
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
  bool first = false;
  {
    base::AutoLock hold(lock_);
    video_watched_ = false;
    first = !video_worked_;
    video_worked_ = true;
    if (video_trying_) {
      video_trying_ = false;
      if (video_ > 0) {
        --video_;
        WriteVideoCount(file_, stamp_, video_);
      }
    }
  }
  if (first) {
    Note("video: the GPU's decoder has given frames for a few seconds; the "
         "run counts against it again only while another decoder is new");
  }
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

void PresentGuard::VideoLost() {
  {
    base::AutoLock hold(lock_);
    video_watched_ = false;
    if (!video_trying_) {
      return;
    }
    // Counted, and left so: nothing after this takes the run out again.
    video_trying_ = false;
    video_lost_ = true;
  }
  Note("video: the graphics device went while the GPU's decoder was first at "
       "work; this run counts against it");
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
    } else if (video_worked_ && !video_lost_ && video_ != 0) {
      // It decoded on the GPU and lived to be suspended with no decoder new:
      // what earlier runs died of is forgotten.
      video_ = 0;
      WriteVideoCount(file_, stamp_, video_);
    }
  } else if (video_watched_ && !video_trying_) {
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
  if (!angle_.count && !d3d11_.count && !compositor_.count) {
    ::DeleteFileW(file_.value().c_str());
    return;
  }
  // The compositor's count last: an engine from before it reads the first
  // three fields and is not confused by a fourth.
  char text[64];
  const int length = sprintf_s(text, "VWG1 %08x %d %d %d\n",
                               static_cast<unsigned>(stamp_), angle_.count,
                               d3d11_.count, compositor_.count);
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
