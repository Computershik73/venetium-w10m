// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).
//
// The engine's line to the shell's log. A phone has no debugger and no
// console, so this is how anything the engine knows reaches a person: every
// line goes to the logger the shell installed (venetium_w10m_set_logger),
// which writes it to LocalState\venetium.log at once.

#ifndef VENETIUM_W10M_COMMON_VW_LOG_H_
#define VENETIUM_W10M_COMMON_VW_LOG_H_

#include <string>
#include <string_view>

#include "third_party/abseil-cpp/absl/strings/str_format.h"
#include "venetium_w10m/capi/venetium_w10m_capi.h"

namespace venetium_w10m {

void SetShellLogger(venetium_w10m_log_fn fn);

// Any thread. A line without its newline.
void Note(std::string_view line);

template <typename... Args>
void NoteF(const absl::FormatSpec<Args...>& format, const Args&... args) {
  Note(absl::StrFormat(format, args...));
}

// Sends Chromium's own LOG() output (WARNING and up, or everything with
// --v=1) to the shell's log as well. Call once, after logging is set up.
void ForwardChromiumLogs();

}  // namespace venetium_w10m

#endif  // VENETIUM_W10M_COMMON_VW_LOG_H_
