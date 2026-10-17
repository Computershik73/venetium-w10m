// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/browser/vw_interface_guard.h"

#include <windows.h>

#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <string>

#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "venetium_w10m/browser/vw_present_guard.h"
#include "venetium_w10m/common/vw_log.h"

namespace venetium_w10m::interface_guard {

namespace {

base::FilePath File(const base::FilePath& profile_dir) {
  return profile_dir.Append(FILE_PATH_LITERAL("interface-guard"));
}

// Runs that ended while trying, as the file says; 0 if it is missing or was
// written by another build.
int Read(const base::FilePath& file) {
  HANDLE handle =
      ::CreateFileW(file.value().c_str(), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return 0;
  }
  char text[32] = {};
  DWORD got = 0;
  ::ReadFile(handle, text, sizeof(text) - 1, &got, nullptr);
  ::CloseHandle(handle);
  unsigned stamp = 0;
  int count = 0;
  if (sscanf_s(text, "VWI1 %x %d", &stamp, &count) != 2 ||
      stamp != EngineBuildStamp()) {
    return 0;
  }
  return count;
}

void Write(const base::FilePath& file, int count) {
  if (!count) {
    ::DeleteFileW(file.value().c_str());
    return;
  }
  char text[32];
  const int length = sprintf_s(text, "VWI1 %08x %d\n",
                               static_cast<unsigned>(EngineBuildStamp()),
                               count);
  HANDLE handle = ::CreateFileW(file.value().c_str(), GENERIC_WRITE, 0,
                                nullptr, CREATE_ALWAYS, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE || length <= 0) {
    return;
  }
  DWORD written = 0;
  ::WriteFile(handle, text, static_cast<DWORD>(length), &written, nullptr);
  ::FlushFileBuffers(handle);
  ::CloseHandle(handle);
}

struct State {
  base::Lock lock;
  base::FilePath file;  // set by Trying()
  bool proven = false;
};

State& TheState() {
  static base::NoDestructor<State> state;
  return *state;
}

}  // namespace

bool GaveUp(const base::FilePath& profile_dir) {
  return Read(File(profile_dir)) >= kTries;
}

void Trying(const base::FilePath& profile_dir) {
  State& state = TheState();
  base::AutoLock hold(state.lock);
  state.file = File(profile_dir);
  const int count = Read(state.file) + 1;
  Write(state.file, count);
  NoteF("interface: Chrome's, try %d (after %d that ended before its window "
        "showed, the shell's)",
        count, kTries);
}

void Proven() {
  State& state = TheState();
  base::AutoLock hold(state.lock);
  if (state.file.empty() || state.proven) {
    return;
  }
  state.proven = true;
  Write(state.file, 0);
  Note("interface: Chrome's window has been on screen; not counted");
}

}  // namespace venetium_w10m::interface_guard
