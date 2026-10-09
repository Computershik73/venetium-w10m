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

namespace venetium_w10m::client {

// Installs the fault logger and starts the heartbeat. Once, early.
void InstallCrashLog();

}  // namespace venetium_w10m::client
