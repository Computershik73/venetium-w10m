// ImportProbe.h -- why a DLL of the package would not load.
//
// LoadPackagedLibrary says only "the specified procedure could not be found"
// (127) or "module not found" (126). This reads the DLL's import table from
// the file and asks the phone for each DLL and function it names, the way
// the loader would, and writes every one the phone lacks to the log.
// Delay-loaded imports are left out: those fail later, one call at a time.
#pragma once

#include <string>

namespace venetium_w10m::engine {

// |file| is a file name in the package's install folder. Returns the number
// of DLLs and functions found missing (also written to the log).
int ProbeImports(std::wstring const& file);

}  // namespace venetium_w10m::engine
