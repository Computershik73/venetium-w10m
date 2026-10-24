// CrashLog.cpp
#include "pch.h"

#include "client/CrashLog.h"

#include <atomic>
#include <string>

#include "client/Log.h"

// The app partition of errhandlingapi.h hides these two, though kernelbase
// exports them and an app container may call them (gecko-w10m).
extern "C" {
using VwVectoredHandler = LONG(CALLBACK*)(PEXCEPTION_POINTERS);
__declspec(dllimport) PVOID WINAPI
AddVectoredExceptionHandler(ULONG First, VwVectoredHandler Handler);
using VwTopLevelFilter = LONG(WINAPI*)(PEXCEPTION_POINTERS);
__declspec(dllimport) VwTopLevelFilter WINAPI
SetUnhandledExceptionFilter(VwTopLevelFilter filter);
}

namespace venetium_w10m::client {
namespace {

std::wstring Hex(uintptr_t v) {
  wchar_t buf[19];
  ::swprintf_s(buf, L"0x%08llx", static_cast<unsigned long long>(v));
  return buf;
}

// Module+offset: an address alone means nothing across runs.
std::wstring DescribeAddress(uintptr_t address) {
  HMODULE module = nullptr;
  if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module) ||
      !module) {
    return Hex(address) + L" (no module)";
  }
  wchar_t path[MAX_PATH] = {};
  const DWORD n = ::GetModuleFileNameW(module, path, MAX_PATH);
  std::wstring name(path, n);
  const auto slash = name.find_last_of(L'\\');
  if (slash != std::wstring::npos) name = name.substr(slash + 1);
  return name + L"+" + Hex(address - reinterpret_cast<uintptr_t>(module));
}

// 32-bit ARM Windows unwinds from tables, not a frame-pointer chain:
// RtlCaptureStackBackTrace stops after one frame, the tables go further.
using LookupFunctionEntryFn = PVOID(WINAPI*)(DWORD, PDWORD, PVOID);
using VirtualUnwindFn = PVOID(WINAPI*)(DWORD, DWORD, DWORD, PVOID, PCONTEXT,
                                       PVOID*, PDWORD, PVOID);
LookupFunctionEntryFn g_lookup = nullptr;
VirtualUnwindFn g_unwind = nullptr;

void LogStack(CONTEXT context) {
  if (!g_lookup || !g_unwind) {
    Log::WriteFromFault(L"crash:   (no unwind support)");
    return;
  }
  for (int depth = 0; depth < 40; ++depth) {
    if (!context.Pc) return;
    Log::WriteFromFault(L"crash:   " + DescribeAddress(context.Pc));
    DWORD image_base = 0;
    PVOID entry = g_lookup(context.Pc, &image_base, nullptr);
    if (!entry) return;
    PVOID handler_data = nullptr;
    DWORD establisher = 0;
    const DWORD previous = context.Pc;
    g_unwind(0 /* UNW_FLAG_NHANDLER */, image_base, context.Pc, entry,
             &context, &handler_data, &establisher, nullptr);
    if (context.Pc == previous) return;
  }
}

// When the walk stops early (a fault in a prologue, a function with no
// unwind data): the words on the stack that some function claims as a return
// address. Candidates, not a call chain.
void LogStackScan(DWORD sp) {
  if (!g_lookup) return;
  MEMORY_BASIC_INFORMATION region{};
  const auto* start = reinterpret_cast<const uintptr_t*>(sp);
  if (!::VirtualQuery(start, &region, sizeof(region))) return;
  const auto* limit = reinterpret_cast<const uintptr_t*>(
      static_cast<const char*>(region.BaseAddress) + region.RegionSize);
  Log::WriteFromFault(L"crash:   -- stack scan (candidates) --");
  int printed = 0;
  for (const uintptr_t* word = start; word < limit && printed < 32; ++word) {
    const uintptr_t value = *word;
    if (value < 0x10000) continue;
    DWORD image_base = 0;
    if (!g_lookup(static_cast<DWORD>(value), &image_base, nullptr)) continue;
    Log::WriteFromFault(L"crash:   ?? " + DescribeAddress(value));
    ++printed;
  }
}

bool IsFatal(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_BREAKPOINT:  // __debugbreak, base::ImmediateCrash
    case 0xC0000374:            // STATUS_HEAP_CORRUPTION
    case 0xC0000409:            // STATUS_STACK_BUFFER_OVERRUN (fail fast)
    case 0xC0000602:            // STATUS_FAIL_FAST_EXCEPTION
    case 0xE0000008:            // Chromium's out of memory (OOM_CRASH)
      return true;
    default:
      return false;
  }
}

std::wstring FaultDetail(const EXCEPTION_RECORD& record) {
  std::wstring text = L"code " + Hex(record.ExceptionCode) + L" at " +
                      DescribeAddress(reinterpret_cast<uintptr_t>(
                          record.ExceptionAddress));
  if ((record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
       record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
      record.NumberParameters >= 2) {
    const ULONG_PTR kind = record.ExceptionInformation[0];
    text += kind == 0 ? L", reading " : kind == 1 ? L", writing "
                                                  : L", executing ";
    text += Hex(record.ExceptionInformation[1]);
  }
  return text;
}

// The code around |pc| as it is in memory now, and its page's protection:
// an illegal instruction where the file has a valid one means the code was
// changed in memory, or the processor was not in the mode it should be.
std::wstring CodeAt(uintptr_t pc) {
  const uintptr_t start = (pc & ~uintptr_t{1}) - 8;
  MEMORY_BASIC_INFORMATION info{};
  if (!::VirtualQuery(reinterpret_cast<const void*>(start), &info,
                      sizeof(info)) ||
      info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
      reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize <
          start + 20) {
    return L"code: not readable";
  }
  const auto* half = reinterpret_cast<const uint16_t*>(start);
  wchar_t buf[160];
  ::swprintf_s(buf,
               L"code at pc-8: %04x %04x %04x %04x [%04x %04x] %04x %04x "
               L"%04x %04x, page protection 0x%lx",
               half[0], half[1], half[2], half[3], half[4], half[5], half[6],
               half[7], half[8], half[9], info.Protect);
  return buf;
}

std::wstring Registers(const CONTEXT& c) {
  wchar_t buf[400];
  ::swprintf_s(buf,
               L"r0=%08lx r1=%08lx r2=%08lx r3=%08lx r4=%08lx r5=%08lx "
               L"r6=%08lx r7=%08lx r8=%08lx r9=%08lx r10=%08lx r11=%08lx "
               L"r12=%08lx sp=%08lx lr=%08lx pc=%08lx cpsr=%08lx",
               c.R0, c.R1, c.R2, c.R3, c.R4, c.R5, c.R6, c.R7, c.R8, c.R9,
               c.R10, c.R11, c.R12, c.Sp, c.Lr, c.Pc, c.Cpsr);
  return buf;
}

// How much address space is left, its largest free piece, and what took the
// rest: a 32-bit process runs out of address space long before the phone's
// memory limit, and mostly to memory that is reserved or mapped rather than
// used.
struct AddressSpaceUse {
  uint64_t free = 0;
  uint64_t largest_free = 0;
  uint64_t image = 0;     // DLLs
  uint64_t mapped = 0;    // files and shared memory
  uint64_t committed = 0; // private, in use
  uint64_t reserved = 0;  // private, set aside
};

AddressSpaceUse AddressSpace() {
  AddressSpaceUse use;
  uintptr_t at = 0x10000;
  MEMORY_BASIC_INFORMATION region{};
  while (at < 0x80000000u &&
         ::VirtualQuery(reinterpret_cast<void*>(at), &region, sizeof(region))) {
    if (region.State == MEM_FREE) {
      use.free += region.RegionSize;
      if (region.RegionSize > use.largest_free) {
        use.largest_free = region.RegionSize;
      }
    } else if (region.Type == MEM_IMAGE) {
      use.image += region.RegionSize;
    } else if (region.Type == MEM_MAPPED) {
      use.mapped += region.RegionSize;
    } else if (region.State == MEM_COMMIT) {
      use.committed += region.RegionSize;
    } else {
      use.reserved += region.RegionSize;
    }
    const uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) +
                           region.RegionSize;
    if (next <= at) break;
    at = next;
  }
  return use;
}

// The numbers of one look, megabytes, for whoever acts on them
// (SetMemorySink).
struct MemoryNumbers {
  int32_t largest_free = 0;
  int32_t free = 0;
  int32_t app = 0;
  int32_t limit = 0;
};

std::atomic<MemorySink> g_memory_sink{nullptr};

std::wstring MemoryLine(MemoryNumbers* numbers = nullptr) {
  std::wstring line = L"memory:";
  uint64_t app = 0;
  uint64_t limit = 0;
  try {
    using winrt::Windows::System::MemoryManager;
    app = MemoryManager::AppMemoryUsage();
    limit = MemoryManager::AppMemoryUsageLimit();
    line += L" app " + std::to_wstring(app >> 20) + L" MB of " +
            std::to_wstring(limit >> 20) + L" MB";
  } catch (...) {
  }
  const AddressSpaceUse use = AddressSpace();
  if (numbers) {
    numbers->largest_free = static_cast<int32_t>(use.largest_free >> 20);
    numbers->free = static_cast<int32_t>(use.free >> 20);
    numbers->app = static_cast<int32_t>(app >> 20);
    numbers->limit = static_cast<int32_t>(limit >> 20);
  }
  const auto mb = [](uint64_t bytes) { return std::to_wstring(bytes >> 20); };
  line += L", address space free " + mb(use.free) + L" MB (largest " +
          mb(use.largest_free) + L" MB); taken: DLLs " + mb(use.image) +
          L", mapped " + mb(use.mapped) + L", private " + mb(use.committed) +
          L" + " + mb(use.reserved) + L" reserved";
  return line;
}

std::atomic<int> g_reported{0};
thread_local bool t_in_handler = false;

void Report(const wchar_t* how, PEXCEPTION_POINTERS info) {
  // A handful per run: a fault the process survives (a first-chance one
  // some __try handles) must not fill the log.
  if (g_reported.fetch_add(1) >= 8 || t_in_handler) return;
  t_in_handler = true;
  const EXCEPTION_RECORD& record = *info->ExceptionRecord;
  Log::WriteFromFault(std::wstring(L"crash: ") + how + L" " +
                      FaultDetail(record) + L", thread " +
                      std::to_wstring(::GetCurrentThreadId()));
  Log::WriteFromFault(L"crash: " + Registers(*info->ContextRecord));
  Log::WriteFromFault(L"crash: " + CodeAt(info->ContextRecord->Pc));
  Log::WriteFromFault(L"crash: " + MemoryLine());
  LogStack(*info->ContextRecord);
  LogStackScan(info->ContextRecord->Sp);
  t_in_handler = false;
}

// Last in line: any handler that can deal with the exception (V8's JIT
// page flipping, among them) has already had it.
LONG CALLBACK OnException(PEXCEPTION_POINTERS info) {
  if (info && info->ExceptionRecord &&
      IsFatal(info->ExceptionRecord->ExceptionCode)) {
    Report(L"(first chance)", info);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI OnUnhandled(PEXCEPTION_POINTERS info) {
  if (info && info->ExceptionRecord) {
    Log::WriteFromFault(L"crash: UNHANDLED, the process ends here");
    g_reported.store(0);
    Report(L"(unhandled)", info);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI Heartbeat(LPVOID) {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  std::wstring last;
  for (;;) {
    ::Sleep(2000);
    // Only when it moved by a few megabytes: a quiet page logs nothing.
    MemoryNumbers numbers;
    const std::wstring line = MemoryLine(&numbers);
    if (line != last) {
      Log::Write(line);
      last = line;
    }
    // To the engine every time, moved or not: its memory pressure goes by
    // these numbers, and by how they stand a few seconds after it has let
    // something go.
    if (const MemorySink sink = g_memory_sink.load()) {
      sink(numbers.largest_free, numbers.free, numbers.app, numbers.limit);
    }
  }
}

}  // namespace

void InstallCrashLog() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  if (HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll")) {
    g_lookup = reinterpret_cast<LookupFunctionEntryFn>(
        ::GetProcAddress(ntdll, "RtlLookupFunctionEntry"));
    g_unwind = reinterpret_cast<VirtualUnwindFn>(
        ::GetProcAddress(ntdll, "RtlVirtualUnwind"));
  }
  ::AddVectoredExceptionHandler(0, &OnException);
  ::SetUnhandledExceptionFilter(&OnUnhandled);
  if (HANDLE thread =
          ::CreateThread(nullptr, 64 * 1024, &Heartbeat, nullptr, 0, nullptr)) {
    ::CloseHandle(thread);
  }
  Log::Write(L"crash log: installed; " + MemoryLine());
}

void SetMemorySink(MemorySink sink) {
  g_memory_sink.store(sink);
}

}  // namespace venetium_w10m::client
