// the log's opening lines: what the game runs on and with which settings
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(_M_X64) || defined(__x86_64__)
#include <intrin.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "image_symbols.h"

#pragma comment(lib, "advapi32.lib")

namespace app {

namespace {

std::string RegString(HKEY key, const char* name) {
    char text[256] = {};
    DWORD size = sizeof(text) - 1;
    if (RegQueryValueExA(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(text), &size) != ERROR_SUCCESS) return {};
    return text;
}

std::string OsVersion() {
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ | KEY_WOW64_64KEY,
                      &key) != ERROR_SUCCESS)
        return "unknown";
    std::string name = RegString(key, "ProductName"), display = RegString(key, "DisplayVersion"),
                build = RegString(key, "CurrentBuildNumber");
    DWORD ubr = 0, size = sizeof(ubr);
    RegQueryValueExA(key, "UBR", nullptr, nullptr, reinterpret_cast<LPBYTE>(&ubr), &size);
    RegCloseKey(key);
    // Windows 11 still names itself Windows 10 here, its build number tells
    if (auto ten = name.find("Windows 10"); ten != std::string::npos && std::atoi(build.c_str()) >= 22000)
        name.replace(ten, 10, "Windows 11");
    if (!display.empty()) name += " " + display;
    if (!build.empty()) name += " (build " + build + "." + std::to_string(ubr) + ")";
    using WineVersion = const char*(__cdecl*)();
    if (auto wine = reinterpret_cast<WineVersion>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")))) {
        name += std::string(" on Wine ") + wine();
        // the system Wine runs on (CrossOver on a Mac says Darwin): Mac reports tell themselves apart
        using WineHost = void(__cdecl*)(const char**, const char**);
        const char *sys = nullptr, *rel = nullptr;
        if (auto host = reinterpret_cast<WineHost>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_host_version"))))
            host(&sys, &rel);
        if (sys && *sys) name += std::string(" on ") + (std::strcmp(sys, "Darwin") == 0 ? "macOS (Darwin " : std::string(sys) + " (") + (rel ? rel : "") + ")";
    }
    return name;
}

std::string CpuName() {
#if !(defined(_M_X64) || defined(__x86_64__))
    // no cpuid on ARM: Windows keeps the processor's name
    char name[128] = {};
    DWORD size = sizeof name;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, name, &size) != ERROR_SUCCESS)
        return "unknown";
    std::string s = name;
    s.erase(0, s.find_first_not_of(' '));
    s.erase(s.find_last_not_of(' ') + 1);
    return s;
#else
    int r[4] = {};
    __cpuid(r, int(0x80000000));
    if (uint32_t(r[0]) < 0x80000004) return "unknown";
    char brand[49] = {};
    for (int i = 0; i < 3; i++) {
        __cpuid(r, int(0x80000002 + i));
        std::memcpy(brand + i * 16, r, 16);
    }
    std::string s = brand;
    s.erase(0, s.find_first_not_of(' '));
    s.erase(s.find_last_not_of(' ') + 1);
    return s;
#endif
}

}  // namespace

uint64_t OneCoreMask() {
    // the first of the cores with the highest efficiency class: a performance core where they differ
#ifdef __MINGW32__  // its headers lack the CPU set calls (test builds only): the first core
    return 1;
#else
    ULONG size = 0;
    GetSystemCpuSetInformation(nullptr, 0, &size, GetCurrentProcess(), 0);
    std::vector<uint8_t> buf(size);
    auto* info = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buf.data());
    if (!size || !GetSystemCpuSetInformation(info, size, &size, GetCurrentProcess(), 0)) return 1;
    int best = -1, cls = -1;
    for (ULONG at = 0; at < size;) {
        auto* e = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buf.data() + at);
        if (e->Type == CpuSetInformation && e->CpuSet.Group == 0 && e->CpuSet.LogicalProcessorIndex < 64 &&
            int(e->CpuSet.EfficiencyClass) > cls) {
            cls = e->CpuSet.EfficiencyClass;
            best = e->CpuSet.LogicalProcessorIndex;
        }
        at += e->Size;
    }
    return best < 0 ? 1 : 1ull << best;
#endif
}

void LogSystem() {
    std::time_t now = std::time(nullptr);
    char date[32] = {};
    std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    LOGR("%ls (%s), %s", kGame.title, IMAGE_XBE, date);
    LOGR("Path: %ls", ExeDir().c_str());
    LOGR("OS: %s", OsVersion().c_str());
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    LOGR("CPU: %s (%lu threads)", CpuName().c_str(), si.dwNumberOfProcessors);
#if defined(_M_ARM64) || defined(__aarch64__)
    LOGR("Build: ARM64");
#else
    // GetNativeSystemInfo tells the machine's own processor, even to an x64 program Windows on ARM is translating
    LOGR("Build: x64%s", si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64
                             ? ", translated by Windows on ARM (the ARM64 build runs natively)" : "");
#endif
    MEMORYSTATUSEX mem{sizeof(mem)};
    GlobalMemoryStatusEx(&mem);
    LOGR("RAM: %.1f GB", mem.ullTotalPhys / 1073741824.0);
    // one line per adapter, each monitor reports its own
    std::vector<std::string> seen;
    for (DWORD i = 0;; i++) {
        DISPLAY_DEVICEA dd{sizeof(dd)};
        if (!EnumDisplayDevicesA(nullptr, i, &dd, 0)) break;
        if (std::find(seen.begin(), seen.end(), dd.DeviceString) != seen.end()) continue;
        seen.emplace_back(dd.DeviceString);
        LOGR("GPU %zu: %s%s", seen.size(), dd.DeviceString, (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? " (primary)" : "");
    }
    Mode desk = Desktop();
    LOGR("Desktop: %ux%u", desk.width, desk.height);
    LogSettings();
}

}  // namespace app
