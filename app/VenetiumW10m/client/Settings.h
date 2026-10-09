// Settings.h -- the browser's few preferences, in the app's LocalSettings.
#pragma once

#include <string>
#include <vector>

namespace venetium_w10m::client {

struct SearchEngine {
  const wchar_t* name;
  const wchar_t* url_template;  // "%s" is the query
};

class Settings {
 public:
  static const std::vector<SearchEngine>& SearchEngines();

  static int SearchEngineIndex();
  static void SetSearchEngineIndex(int index);
  static std::wstring SearchTemplate();

  static bool DesktopMode();
  static void SetDesktopMode(bool desktop);

  static std::wstring HomePage();

  // V8's JIT (needs the codeGeneration capability, which an
  // interop-unlocked phone grants). Read at start.
  static bool Jit();
  static void SetJit(bool jit);

  // Extra Chromium switches, for diagnosing on a device.
  static std::wstring EngineSwitches();
};

}  // namespace venetium_w10m::client
