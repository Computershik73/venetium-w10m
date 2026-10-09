// Settings.cpp
#include "pch.h"

#include "client/Settings.h"

using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::IPropertyValue;
using winrt::Windows::Foundation::PropertyValue;
using winrt::Windows::Storage::ApplicationData;

namespace venetium_w10m::client {

namespace {

auto Values() {
  return ApplicationData::Current().LocalSettings().Values();
}

int32_t ReadInt(const wchar_t* key, int32_t fallback) {
  try {
    IInspectable value = Values().TryLookup(key);
    if (value) return winrt::unbox_value<int32_t>(value);
  } catch (...) {
  }
  return fallback;
}

void WriteInt(const wchar_t* key, int32_t value) {
  try {
    Values().Insert(key, winrt::box_value(value));
  } catch (...) {
  }
}

std::wstring ReadString(const wchar_t* key, const wchar_t* fallback) {
  try {
    IInspectable value = Values().TryLookup(key);
    if (value) return std::wstring(winrt::unbox_value<winrt::hstring>(value));
  } catch (...) {
  }
  return fallback;
}

}  // namespace

const std::vector<SearchEngine>& Settings::SearchEngines() {
  static const std::vector<SearchEngine> engines = {
      {L"Google", L"https://www.google.com/search?q=%s"},
      {L"Yandex", L"https://yandex.ru/search/touch/?text=%s"},
      {L"DuckDuckGo", L"https://duckduckgo.com/?q=%s"},
      {L"Bing", L"https://www.bing.com/search?q=%s"},
      {L"Startpage", L"https://www.startpage.com/sp/search?query=%s"},
  };
  return engines;
}

int Settings::SearchEngineIndex() {
  const int index = ReadInt(L"search_engine", 0);
  return index >= 0 && index < (int)SearchEngines().size() ? index : 0;
}

void Settings::SetSearchEngineIndex(int index) {
  WriteInt(L"search_engine", index);
}

std::wstring Settings::SearchTemplate() {
  return SearchEngines()[SearchEngineIndex()].url_template;
}

bool Settings::DesktopMode() {
  return ReadInt(L"desktop_mode", 0) != 0;
}

void Settings::SetDesktopMode(bool desktop) {
  WriteInt(L"desktop_mode", desktop ? 1 : 0);
}

std::wstring Settings::HomePage() {
  return ReadString(L"home_page", L"https://www.google.com/");
}

bool Settings::Jit() {
  return ReadInt(L"jit", 0) != 0;
}

void Settings::SetJit(bool jit) {
  WriteInt(L"jit", jit ? 1 : 0);
}

bool Settings::HardwareGraphics() {
  return ReadInt(L"hardware_graphics", 1) != 0;
}

void Settings::SetHardwareGraphics(bool on) {
  WriteInt(L"hardware_graphics", on ? 1 : 0);
}

std::wstring Settings::EngineSwitches() {
  return ReadString(L"engine_switches", L"");
}

}  // namespace venetium_w10m::client
