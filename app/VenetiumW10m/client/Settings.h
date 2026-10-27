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

  // The black theme (Chrome's menu, Windows 10 Mobile, Black theme): the
  // engine's setting, of which the shell keeps a copy for its own colours
  // before the engine starts. Read at start.
  static bool Amoled();
  static void SetAmoled(bool on);

  // Frames shown by the GPU (a Direct3D swap chain in a SwapChainPanel)
  // rather than copied into a bitmap. Read at start.
  static bool HardwareGraphics();
  static void SetHardwareGraphics(bool on);
  // Set when Hardware graphics is switched on again: the next start asks
  // the engine to forget what earlier runs could not do. Read once.
  static bool TakeGraphicsRetry();

  // Extra Chromium switches, for diagnosing on a device.
  static std::wstring EngineSwitches();
  static void SetEngineSwitches(std::wstring const& switches);
};

}  // namespace venetium_w10m::client
