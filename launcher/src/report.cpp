// Athelas - a problem report in one zip (see report.h)
#include "report.h"
#include "config.h"

#include <windows.h>
#include <dxgi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

#include "../third_party/miniz/miniz.h"

// ---- the PC ----------------------------------------------------------------------------------------------

static std::string reg_string(HKEY root, const char *key, const char *value)
{
    char b[512];
    DWORD n = sizeof b;
    if (RegGetValueA(root, key, value, RRF_RT_REG_SZ, NULL, b, &n) != ERROR_SUCCESS) return "";
    return b;
}

static std::string trim(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}

static std::string windows_text()
{
    // RtlGetVersion tells the truth where GetVersionEx says "Windows 8" to programs without a manifest entry
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW *);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    OSVERSIONINFOW v = {};
    v.dwOSVersionInfoSize = sizeof v;
    if (RtlGetVersionFn f = nt ? (RtlGetVersionFn)(void *)GetProcAddress(nt, "RtlGetVersion") : nullptr) f(&v);
    const char *cv = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::string product = reg_string(HKEY_LOCAL_MACHINE, cv, "ProductName");
    // Windows 11 still calls itself Windows 10 in ProductName: the build number decides
    if (v.dwBuildNumber >= 22000 && product.rfind("Windows 10", 0) == 0) product.replace(0, 10, "Windows 11");
    std::string display = reg_string(HKEY_LOCAL_MACHINE, cv, "DisplayVersion");
    char b[64];
    snprintf(b, sizeof b, "%lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    std::string s = (product.empty() ? "Windows" : product) + (display.empty() ? "" : " " + display) + " (" + b + ")";
    // Wine and Proton (Linux, Steam Deck) say which version they are
    typedef const char *(CDECL * WineVersionFn)();
    if (WineVersionFn w = nt ? (WineVersionFn)(void *)GetProcAddress(nt, "wine_get_version") : nullptr) s += std::string(", Wine ") + w();
    return s;
}

static std::string machine_text()
{
    typedef BOOL(WINAPI * Wow64Fn)(HANDLE, USHORT *, USHORT *);
    USHORT proc = 0, native = 0;
    Wow64Fn f = (Wow64Fn)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process2");
    if (f && f(GetCurrentProcess(), &proc, &native))
        return native == 0xAA64 ? "ARM64" : native == 0x8664 ? "x64" : native == 0x14C ? "x86" : "unknown";
    BOOL wow = FALSE;
    IsWow64Process(GetCurrentProcess(), &wow);
    return wow ? "x64" : "x86";
}

static std::string wide_utf8(const wchar_t *w)
{
    char b[512];
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, b, sizeof b, NULL, NULL)) return "";
    return b;
}

// Each graphics adapter with its memory and driver version, through DXGI (loaded only here)
static std::vector<std::string> adapters_text()
{
    std::vector<std::string> out;
    HMODULE dx = LoadLibraryA("dxgi.dll");
    if (!dx) return out;
    typedef HRESULT(WINAPI * CreateFn)(REFIID, void **);
    CreateFn create = (CreateFn)(void *)GetProcAddress(dx, "CreateDXGIFactory1");
    IDXGIFactory1 *fac = nullptr;
    if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), (void **)&fac))) {
        IDXGIAdapter1 *ad = nullptr;
        for (UINT i = 0; fac->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND && i < 8; i++) {
            DXGI_ADAPTER_DESC1 d;
            if (SUCCEEDED(ad->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                std::string s = wide_utf8(d.Description);
                char b[96];
                snprintf(b, sizeof b, " (%u MB, vendor %04X device %04X", (unsigned)(d.DedicatedVideoMemory >> 20),
                         d.VendorId, d.DeviceId);
                s += b;
                LARGE_INTEGER umd;
                if (SUCCEEDED(ad->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
                    snprintf(b, sizeof b, ", driver %u.%u.%u.%u", HIWORD(umd.HighPart), LOWORD(umd.HighPart),
                             HIWORD(umd.LowPart), LOWORD(umd.LowPart));
                    s += b;
                }
                s += ")";
                if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
            }
            ad->Release();
        }
        fac->Release();
    }
    FreeLibrary(dx);
    return out;
}

std::string report_system_text()
{
    std::string s;
    s += "Windows: " + windows_text() + "\r\n";
    s += "Processor: " + trim(reg_string(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString"));
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    char b[160];
    snprintf(b, sizeof b, " (%lu threads, %s)\r\n", si.dwNumberOfProcessors, machine_text().c_str());
    s += b;
    MEMORYSTATUSEX m = {};
    m.dwLength = sizeof m;
    if (GlobalMemoryStatusEx(&m)) {
        snprintf(b, sizeof b, "Memory: %.1f GB (%.1f GB free)\r\n", m.ullTotalPhys / 1073741824.0, m.ullAvailPhys / 1073741824.0);
        s += b;
    }
    for (auto &g : adapters_text()) s += "Graphics: " + g + "\r\n";
    for (auto &mo : enum_monitors()) {
        snprintf(b, sizeof b, "Display: %dx%d at %d Hz%s, highest %d Hz", mo.deskW, mo.deskH, mo.deskHz,
                 mo.primary ? " (main)" : "", mo.maxHz());
        s += b + std::string(mo.name.empty() ? "" : " - " + mo.name) + "\r\n";
    }
    UINT dpi = 96;
    typedef UINT(WINAPI * DpiFn)();
    if (DpiFn f = (DpiFn)(void *)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem")) dpi = f();
    snprintf(b, sizeof b, "Display scale: %u%%\r\n", dpi * 100 / 96);
    s += b;
    return s;
}

// ---- taking the user's name out -----------------------------------------------------------------------------

static void replace_all_ci(std::string &s, const std::string &from, const std::string &to)
{
    if (from.empty()) return;
    std::string low = s, f = from;
    for (char &c : low) c = (char)tolower((unsigned char)c);
    for (char &c : f) c = (char)tolower((unsigned char)c);
    std::string out;
    size_t at = 0, hit;
    while ((hit = low.find(f, at)) != std::string::npos) {
        out.append(s, at, hit - at);
        out += to;
        at = hit + f.size();
    }
    if (!at) return;
    out.append(s, at, std::string::npos);
    s.swap(out);
}

std::string report_anonymise(const std::string &text)
{
    std::string s = text;
    char prof[MAX_PATH] = "", user[256] = "";
    GetEnvironmentVariableA("USERPROFILE", prof, sizeof prof);
    GetEnvironmentVariableA("USERNAME", user, sizeof user);
    std::string p = prof;
    if (p.size() > 3) {
        replace_all_ci(s, p, "%USERPROFILE%");
        std::string fwd = p;
        std::replace(fwd.begin(), fwd.end(), '\\', '/');
        replace_all_ci(s, fwd, "%USERPROFILE%");
    }
    // the name in other users-folder paths too (another drive, a moved profile, a short 8.3 name is left)
    std::string u = user;
    if (!u.empty()) {
        replace_all_ci(s, "\\Users\\" + u, "\\Users\\%USERNAME%");
        replace_all_ci(s, "/Users/" + u, "/Users/%USERNAME%");
        replace_all_ci(s, "/home/" + u, "/home/%USERNAME%");   // Wine shows Linux paths as Z:\home\name
        replace_all_ci(s, "\\home\\" + u, "\\home\\%USERNAME%");
    }
    return s;
}

// ---- the zip ------------------------------------------------------------------------------------------------

std::string report_newest_dump(const std::string &dir, int days)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.dmp").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string best;
    FILETIME bestAt = {};
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (CompareFileTime(&fd.ftLastWriteTime, &bestAt) > 0) { bestAt = fd.ftLastWriteTime; best = dir + "\\" + fd.cFileName; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (best.empty()) return "";
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a, b;
    a.LowPart = now.dwLowDateTime; a.HighPart = now.dwHighDateTime;
    b.LowPart = bestAt.dwLowDateTime; b.HighPart = bestAt.dwHighDateTime;
    if (a.QuadPart > b.QuadPart && (a.QuadPart - b.QuadPart) / 10000000ull > (unsigned long long)days * 86400ull) return "";
    return best;
}

static bool read_whole(const std::string &path, std::string &out, size_t limit, bool trim)
{
    // shared reading: a log the game still has open can be read
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(h, &size) != 0;
    if (ok) {
        // a very long log: its start (the PC, the settings) and its end (what went wrong) are what matter
        unsigned long long n = (unsigned long long)size.QuadPart;
        auto chunk = [&](unsigned long long at, size_t len, std::string &to) {
            to.resize(len);
            LARGE_INTEGER p; p.QuadPart = (LONGLONG)at;
            DWORD got = 0;
            return SetFilePointerEx(h, p, NULL, FILE_BEGIN) && ReadFile(h, &to[0], (DWORD)len, &got, NULL) && (to.resize(got), true);
        };
        if (n <= limit) ok = chunk(0, (size_t)n, out);
        else if (!trim) ok = false;   // a crash dump cut in the middle can't be opened: left out
        else {
            std::string head, tail;
            ok = chunk(0, limit / 4, head) && chunk(n - limit * 3 / 4, limit * 3 / 4, tail);
            out = head + "\r\n\r\n[... " + std::to_string(n - limit) + " bytes of the log left out here ...]\r\n\r\n" + tail;
        }
    }
    CloseHandle(h);
    return ok;
}

static std::string report_folder()
{
    std::string d = exe_dir() + "\\Reports";
    SHCreateDirectoryExA(NULL, d.c_str(), NULL);
    std::string probe = d + "\\.write-test";
    HANDLE h = CreateFileA(probe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return d; }
    char t[MAX_PATH];
    GetTempPathA(sizeof t, t);
    d = std::string(t) + "Athelas reports";
    SHCreateDirectoryExA(NULL, d.c_str(), NULL);
    return d;
}

bool report_write(const std::vector<ReportFile> &files, const std::string &system, std::string &zipPath, std::string &err)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    char name[96];
    snprintf(name, sizeof name, "Athelas report %04u-%02u-%02u %02u%02u%02u.zip", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    zipPath = report_folder() + "\\" + name;
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    if (!mz_zip_writer_init_file(&z, zipPath.c_str(), 0)) { err = "The report file couldn't be created in " + zipPath; return false; }
    bool ok = true;
    std::string about = report_anonymise(system);
    ok &= mz_zip_writer_add_mem(&z, "About this PC.txt", about.data(), about.size(), MZ_DEFAULT_COMPRESSION) != 0;
    for (auto &f : files) {
        std::string data;
        if (!read_whole(f.path, data, f.text ? 24u << 20 : 96u << 20, f.text)) continue;   // not there: left out
        if (f.text) data = report_anonymise(data);
        ok &= mz_zip_writer_add_mem(&z, f.name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION) != 0;
    }
    ok &= mz_zip_writer_finalize_archive(&z) != 0;
    mz_zip_writer_end(&z);
    if (!ok) { DeleteFileA(zipPath.c_str()); err = "The report couldn't be written (is the disk full?)."; return false; }
    return true;
}
