// Copyright 2026 The Venetium-W10M Authors. GPL-3.0 (see LICENSE).

#include "venetium_w10m/common/vw_log.h"

#include <windows.h>

#include <atomic>

#include "base/logging.h"

namespace venetium_w10m {

namespace {

std::atomic<venetium_w10m_log_fn> g_logger{nullptr};

const char* SeverityName(int severity) {
  switch (severity) {
    case logging::LOGGING_INFO:
      return "INFO";
    case logging::LOGGING_WARNING:
      return "WARNING";
    case logging::LOGGING_ERROR:
      return "ERROR";
    case logging::LOGGING_FATAL:
      return "FATAL";
    default:
      return "VERBOSE";
  }
}

bool OnChromiumLog(int severity,
                   const char* file,
                   int line,
                   size_t message_start,
                   const std::string& str) {
  // INFO lines only when asked for (--v / --enable-logging=...): Chromium is
  // chatty, and every line here is a synchronous write on a phone's eMMC.
  if (severity >= logging::LOGGING_WARNING || VLOG_IS_ON(1)) {
    std::string_view message(str);
    message.remove_prefix(std::min(message_start, message.size()));
    while (!message.empty() &&
           (message.back() == '\n' || message.back() == '\r')) {
      message.remove_suffix(1);
    }
    const char* base = file;
    for (const char* p = file; p && *p; ++p) {
      if (*p == '/' || *p == '\\') {
        base = p + 1;
      }
    }
    Note(absl::StrFormat("chromium %s %s:%d %s", SeverityName(severity),
                         base ? base : "?", line, message));
  }
  // false: Chromium still does its own logging (the file in the profile).
  return false;
}

}  // namespace

void SetShellLogger(venetium_w10m_log_fn fn) {
  g_logger.store(fn);
}

void Note(std::string_view line) {
  venetium_w10m_log_fn fn = g_logger.load();
  if (!fn) {
    return;
  }
  std::string copy(line);
  fn(copy.c_str());
}

void ForwardChromiumLogs() {
  logging::SetLogMessageHandler(&OnChromiumLog);
}

}  // namespace venetium_w10m
