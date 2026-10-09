// ImportProbe.cpp
#include "pch.h"

#include "engine/ImportProbe.h"

#include <algorithm>
#include <vector>

#include "client/Log.h"

namespace venetium_w10m::engine {

using client::Log;

namespace {

std::wstring Widen(std::string const& text) {
  return std::wstring(text.begin(), text.end());
}

// The DLL's file, read piece by piece: it is far too big to read whole.
class PeFile {
 public:
  explicit PeFile(std::wstring const& path) {
    file_ = ::CreateFile2(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                          OPEN_EXISTING, nullptr);
  }
  ~PeFile() {
    if (file_ != INVALID_HANDLE_VALUE) ::CloseHandle(file_);
  }
  bool ok() const { return file_ != INVALID_HANDLE_VALUE; }

  bool Read(uint64_t offset, void* out, uint32_t size) const {
    LARGE_INTEGER at;
    at.QuadPart = static_cast<LONGLONG>(offset);
    DWORD got = 0;
    return ::SetFilePointerEx(file_, at, nullptr, FILE_BEGIN) &&
           ::ReadFile(file_, out, size, &got, nullptr) && got == size;
  }

  template <typename T>
  bool ReadStruct(uint64_t offset, T* out) const {
    return Read(offset, out, sizeof(T));
  }

  std::string ReadString(uint64_t offset) const {
    std::string text;
    char chunk[64];
    while (text.size() < 512) {
      if (!Read(offset + text.size(), chunk, sizeof(chunk))) break;
      for (char c : chunk) {
        if (!c) return text;
        text.push_back(c);
      }
    }
    return text;
  }

 private:
  HANDLE file_ = INVALID_HANDLE_VALUE;
};

using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);

HMODULE LoadSystemDll(std::string const& name) {
  const std::wstring wide = Widen(name);
  if (HMODULE loaded = ::GetModuleHandleW(wide.c_str())) return loaded;
  // A DLL of the package (an api-set forwarder) first, then the system's.
  if (HMODULE packaged = ::LoadPackagedLibrary(wide.c_str(), 0)) {
    return packaged;
  }
  static const auto load_library_ex = reinterpret_cast<LoadLibraryExWFn>(
      ::GetProcAddress(::GetModuleHandleW(L"kernelbase.dll"),
                       "LoadLibraryExW"));
  return load_library_ex ? load_library_ex(wide.c_str(), nullptr, 0)
                         : nullptr;
}

}  // namespace

int ProbeImports(std::wstring const& file) {
  const std::wstring path =
      std::wstring(winrt::Windows::ApplicationModel::Package::Current()
                       .InstalledLocation()
                       .Path()) +
      L"\\" + file;
  PeFile pe(path);
  if (!pe.ok()) {
    Log::Write(L"probe: cannot open", path);
    return 0;
  }
  IMAGE_DOS_HEADER dos{};
  IMAGE_NT_HEADERS32 nt{};
  if (!pe.ReadStruct(0, &dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
      !pe.ReadStruct(dos.e_lfanew, &nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
      nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    Log::Write(L"probe: not a 32-bit PE file", file);
    return 0;
  }
  std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
  const uint64_t section_table = dos.e_lfanew + offsetof(IMAGE_NT_HEADERS32,
                                                         OptionalHeader) +
                                 nt.FileHeader.SizeOfOptionalHeader;
  if (sections.empty() ||
      !pe.Read(section_table, sections.data(),
               static_cast<uint32_t>(sections.size() *
                                     sizeof(IMAGE_SECTION_HEADER)))) {
    Log::Write(L"probe: no section table", file);
    return 0;
  }
  // RVA to file offset (0: not in any section).
  auto offset_of = [&](uint32_t rva) -> uint64_t {
    for (auto const& s : sections) {
      const uint32_t size = std::max(s.Misc.VirtualSize, s.SizeOfRawData);
      if (rva >= s.VirtualAddress && rva < s.VirtualAddress + size) {
        return uint64_t{s.PointerToRawData} + (rva - s.VirtualAddress);
      }
    }
    return 0;
  };

  const IMAGE_DATA_DIRECTORY dir =
      nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  uint64_t at = offset_of(dir.VirtualAddress);
  if (!dir.VirtualAddress || !at) {
    Log::Write(L"probe: no import table", file);
    return 0;
  }
  int missing = 0;
  int checked = 0;
  for (;; at += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
    IMAGE_IMPORT_DESCRIPTOR desc{};
    if (!pe.ReadStruct(at, &desc) || !desc.Name) break;
    const std::string dll = pe.ReadString(offset_of(desc.Name));
    HMODULE module = LoadSystemDll(dll);
    if (!module) {
      Log::WriteNum(L"probe: MISSING DLL " + Widen(dll) + L", error",
                    static_cast<long long>(::GetLastError()));
      ++missing;
      continue;
    }
    uint64_t thunk = offset_of(desc.OriginalFirstThunk ? desc.OriginalFirstThunk
                                                       : desc.FirstThunk);
    for (; thunk; thunk += sizeof(IMAGE_THUNK_DATA32)) {
      IMAGE_THUNK_DATA32 entry{};
      if (!pe.ReadStruct(thunk, &entry) || !entry.u1.AddressOfData) break;
      ++checked;
      if (IMAGE_SNAP_BY_ORDINAL32(entry.u1.Ordinal)) {
        const WORD ordinal = IMAGE_ORDINAL32(entry.u1.Ordinal);
        if (!::GetProcAddress(module, MAKEINTRESOURCEA(ordinal))) {
          Log::Write(L"probe: MISSING " + Widen(dll) + L"!#" +
                     std::to_wstring(ordinal));
          ++missing;
        }
        continue;
      }
      // IMAGE_IMPORT_BY_NAME: a hint, then the name.
      const std::string name =
          pe.ReadString(offset_of(entry.u1.AddressOfData) + sizeof(WORD));
      if (!::GetProcAddress(module, name.c_str())) {
        Log::Write(L"probe: MISSING " + Widen(dll) + L"!" + Widen(name));
        ++missing;
      }
    }
  }
  Log::Write(L"probe: " + file + L": " + std::to_wstring(checked) +
             L" imports checked, " + std::to_wstring(missing) + L" missing");
  return missing;
}

}  // namespace venetium_w10m::engine
