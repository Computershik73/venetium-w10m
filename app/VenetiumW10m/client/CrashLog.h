// CrashLog.h -- what a phone says about a crash: nothing, unless asked.
//
// A Windows 10 Mobile app that faults is gone without a dialog or a dump the
// developer can reach. These write the fault to the log instead: the code,
// the registers, the faulting address as module+offset (ASLR moves modules;
// an offset into venetium_engine.dll resolves with llvm-symbolizer against
// that build's PDB), the call stack unwound from the faulting context, and a
// scan of the raw stack. And because the phone also ends an app that grows
// past its memory limit -- silently, with no exception at all -- a heartbeat
// writes the app's memory use and address space every few seconds, so the
// last lines before such an end say how close it was.
//
// Adapted from gecko-w10m's engine/CrashProbe.
#pragma once

#include <cstdint>

namespace venetium_w10m::client {

// Installs the fault logger and starts the heartbeat. Once, early.
void InstallCrashLog();

// What the heartbeat measures every two seconds, in megabytes: the largest
// free piece of the address space and all of it that is free, the app's
// memory use and the limit the phone gives it (0: not known).
using MemorySink = void (*)(int32_t largest_free_mb, int32_t free_mb,
                            int32_t app_mb, int32_t limit_mb);
// Has the heartbeat hand every measurement to |sink| as well as the log
// (null: no more). The sink is called on the heartbeat's thread. Any thread.
void SetMemorySink(MemorySink sink);

// The memory every 2 s in the log, or not (--vw-diagnostics among the engine
// switches); the engine gets the numbers either way.
void SetDiagnostics(bool on);

}  // namespace venetium_w10m::client
