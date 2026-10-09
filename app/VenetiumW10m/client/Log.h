// Log.h -- a diagnostic log that survives a device with no debugger attached.
//
// Every line goes to LocalState\venetium.log (flushed at once, so a crash
// does not take the last lines with it), to OutputDebugString, and to a ring
// buffer the shell can show on screen. The engine's own lines arrive here
// through venetium_w10m_set_logger. Adapted from gecko-w10m's client/Log.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace venetium_w10m::client {

class Log {
 public:
  // Opens the log file in |localStatePath|. Safe to call more than once.
  static void Init(std::wstring_view localStatePath);

  static void Write(std::wstring_view line);
  static void Write(std::wstring_view label, std::wstring_view value);
  static void WriteNum(std::wstring_view label, long long value);
  // From the engine: UTF-8, any thread.
  static void WriteUtf8(const char* line);
  // From inside a fault handler: straight to the file, without the lock the
  // faulting thread may be holding.
  static void WriteFromFault(std::wstring_view line);

  // The last lines, oldest first.
  static std::vector<std::wstring> Recent();
  // Called on whatever thread logged; marshal to the UI thread yourself.
  static void OnLine(std::function<void(std::wstring)> handler);
  // Full path of the log file, or empty before Init.
  static std::wstring Path();

  // Copies the logs into Pictures\Venetium logs, which every phone shows over
  // USB even when its Device Portal cannot reach LocalState. Blocking; call
  // it off the UI thread.
  static void Mirror();
};

}  // namespace venetium_w10m::client
