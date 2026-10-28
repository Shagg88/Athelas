// update.h: GitHub answers /releases/latest with a redirect to /releases/tag/<version>; the version is read from that
// address, without following it (no API, no rate limit, nothing to parse but one header).
#include "update.h"

#include <windows.h>
#include <winhttp.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static volatile LONG g_started = 0, g_done = 0;
static std::string g_current, g_latest;
static CRITICAL_SECTION g_lock;

static const wchar_t kHost[] = L"github.com";
static const wchar_t kPath[] = L"/Shagg88/Athelas/releases/latest";
const char *update_page() { return "https://github.com/Shagg88/Athelas/releases/latest"; }

// "v1.0.2" or "1.0.2" against "1.0.1": numbers compared part by part; anything else (a test build's "dev") is never newer
static bool newer(const std::string &a, const std::string &b)
{
    const char *p = a.c_str(), *q = b.c_str();
    if (*p == 'v' || *p == 'V') p++;
    if (*q == 'v' || *q == 'V') q++;
    if (*p < '0' || *p > '9' || *q < '0' || *q > '9') return false;
    for (int i = 0; i < 4; i++) {
        long x = strtol(p, (char **)&p, 10), y = strtol(q, (char **)&q, 10);
        if (x != y) return x > y;
        if (*p == '.') p++;
        if (*q == '.') q++;
        if (!*p && !*q) return false;
    }
    return false;
}

static DWORD WINAPI check(void *)
{
    std::string found;
    HINTERNET s = WinHttpOpen(L"Athelas launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) {
        WinHttpSetTimeouts(s, 5000, 5000, 5000, 5000);
        HINTERNET c = WinHttpConnect(s, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET r = c ? WinHttpOpenRequest(c, L"HEAD", kPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE) : NULL;
        if (r) {
            DWORD off = WINHTTP_DISABLE_REDIRECTS;
            WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof off);
            if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(r, NULL)) {
                wchar_t loc[1024];
                DWORD size = sizeof loc;
                if (WinHttpQueryHeaders(r, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &size,
                                        WINHTTP_NO_HEADER_INDEX)) {
                    const wchar_t *t = wcsstr(loc, L"/releases/tag/");
                    if (t)
                        for (t += 14; *t && *t != L'/' && *t != L'?' && found.size() < 32; t++)
                            found += (char)(*t < 128 ? *t : '?');
                }
            }
            WinHttpCloseHandle(r);
        }
        if (c) WinHttpCloseHandle(c);
        WinHttpCloseHandle(s);
    }
    EnterCriticalSection(&g_lock);
    g_latest = newer(found, g_current) ? (found[0] == 'v' || found[0] == 'V' ? found.substr(1) : found) : "";
    LeaveCriticalSection(&g_lock);
    InterlockedExchange(&g_done, 1);
    return 0;
}

void update_check_start(const std::string &current)
{
    if (InterlockedExchange(&g_started, 1)) return;
    InitializeCriticalSection(&g_lock);
    g_current = current;
    HANDLE h = CreateThread(NULL, 0, check, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g_done, 1);
}

std::string update_newer()
{
    if (!g_done) return "";
    EnterCriticalSection(&g_lock);
    std::string v = g_latest;
    LeaveCriticalSection(&g_lock);
    return v;
}

bool update_checked() { return g_done != 0; }

// ---- Installing an update: the release's zip and its .sha256 from GitHub, checked, then unpacked over this folder.
// A running program and its DLLs can be renamed but not overwritten, so each file it replaces is first renamed to
// <name>.old-athelas (deleted at the next start) and the new one moved into its place. If anything fails half way,
// the files already replaced are put back.
#include "../third_party/miniz/miniz.h"
#include <bcrypt.h>
#include <vector>

static volatile LONG g_istate = 0;   // 0 idle, 1 downloading, 2 unpacking, 3 done, -1 failed
static volatile LONG g_ipermille = 0;
static std::string g_ierror;
static std::wstring g_exe_dir;

static std::wstring exe_dir_w()
{
    wchar_t p[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, p, MAX_PATH * 2);
    std::wstring s(p, n);
    return s.substr(0, s.find_last_of(L'\\'));
}

// a GET of https://<host><path> (github.com unless given), redirects followed (release files are served from another host)
static bool http_get(const std::wstring &path, std::string &out, bool progress, const wchar_t *host = kHost)
{
    out.clear();
    bool ok = false;
    HINTERNET s = WinHttpOpen(L"Athelas launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE) : NULL;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                            &size, WINHTTP_NO_HEADER_INDEX);
        DWORD total = 0;
        size = sizeof total;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &total,
                            &size, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            ok = true;
            char buf[65536];
            DWORD got = 0;
            for (;;) {
                if (!WinHttpReadData(r, buf, sizeof buf, &got)) { ok = false; break; }
                if (!got) break;
                out.append(buf, got);
                if (progress && total) InterlockedExchange(&g_ipermille, (LONG)(out.size() * 1000ull / total));
            }
            if (total && out.size() != total) ok = false;
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

bool http_download(const wchar_t *host, const std::wstring &path, std::string &out) { return http_get(path, out, false, host); }

static std::string sha256_hex(const std::string &data)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    unsigned char d[32];
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return "";
    if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0 &&
        BCryptHashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0 && BCryptFinishHash(h, d, 32, 0) == 0)
        for (unsigned char b : d) { hex += "0123456789abcdef"[b >> 4]; hex += "0123456789abcdef"[b & 15]; }
    if (h) BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
}

static std::wstring widen(const std::string &s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static void make_dirs(const std::wstring &file)
{
    for (size_t i = g_exe_dir.size() + 1; (i = file.find(L'\\', i)) != std::wstring::npos; i++)
        CreateDirectoryW(file.substr(0, i).c_str(), NULL);
}

static bool fail(const std::string &why)
{
    g_ierror = why;
    InterlockedExchange(&g_istate, -1);
    return false;
}

static bool is_hex64(const std::string &t)
{
    if (t.size() != 64) return false;
    for (char ch : t) if (!isxdigit((unsigned char)ch)) return false;
    return true;
}

// Windows on ARM (IsWow64Process2 says the machine is ARM64, whatever this 32-bit launcher runs as)
static bool on_arm64()
{
    typedef BOOL(WINAPI * Wow64Process2)(HANDLE, USHORT *, USHORT *);
    Wow64Process2 f = (Wow64Process2)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process2");
    USHORT process = 0, machine = 0;
    return f && f(GetCurrentProcess(), &process, &machine) && machine == 0xAA64;
}

static bool install(const std::string &version)
{
    // somewhere to write first (a protected folder such as Program Files needs the download page instead)
    std::wstring probe = g_exe_dir + L"\\athelas-update-test.tmp";
    HANDLE ph = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (ph == INVALID_HANDLE_VALUE) return fail("Windows doesn't let Athelas change its own folder. Download the update from its page instead.");
    CloseHandle(ph);

    // a Windows on ARM PC takes the release's ARM zip (the same files, plus the ports' ARM64 builds)
    std::wstring base = L"/Shagg88/Athelas/releases/download/" + widen(version) + L"/Athelas-" + widen(version) +
                        (on_arm64() ? L"-arm64.zip" : L".zip");
    std::string sum, zip;
    // its checksum: the .sha256 beside the zip, else the one the release notes give ("SHA-256: `...`")
    if (!http_get(base + L".sha256", sum, false) || sum.size() < 64 || !is_hex64(sum.substr(0, 64))) {
        std::string page;
        sum.clear();
        if (http_get(L"/Shagg88/Athelas/releases/tag/" + widen(version), page, false)) {
            size_t at = page.find("SHA-256");
            while (at != std::string::npos && sum.empty()) {
                for (size_t i = at; i + 64 <= page.size() && i < at + 400; i++)
                    if (is_hex64(page.substr(i, 64)) && (i == 0 || !isxdigit((unsigned char)page[i - 1]))) { sum = page.substr(i, 64); break; }
                at = page.find("SHA-256", at + 7);
            }
        }
        if (sum.empty()) return fail("The update's checksum couldn't be found. Try again later, or download it from its page.");
    }
    sum = sum.substr(0, 64);
    for (char &ch : sum) ch = (char)tolower((unsigned char)ch);
    if (!http_get(base, zip, true)) return fail("The download didn't finish. Check the internet connection and try again.");
    if (sha256_hex(zip) != sum) return fail("The download was damaged (its checksum doesn't match). Try again.");

    InterlockedExchange(&g_istate, 2);
    mz_zip_archive ar;
    memset(&ar, 0, sizeof ar);
    if (!mz_zip_reader_init_mem(&ar, zip.data(), zip.size(), 0)) return fail("The update couldn't be opened.");
    mz_uint count = mz_zip_reader_get_num_files(&ar);
    // the files, and the folder they are all in when the zip has one at its top ("Athelas-1.1/...")
    std::vector<std::string> names(count);
    std::string top;
    bool one_top = true;
    for (mz_uint i = 0; i < count; i++) {
        char n[1024];
        mz_zip_reader_get_filename(&ar, i, n, sizeof n);
        names[i] = n;
        std::string first = names[i].substr(0, names[i].find('/') + 1);
        if (i == 0) top = first;
        if (first.empty() || first != top) one_top = false;
    }
    bool has_root_exe = false;
    for (auto &n : names) if (n == "Athelas.exe") has_root_exe = true;
    size_t strip = one_top && !has_root_exe ? top.size() : 0;

    struct Done { std::wstring target; bool had_old; };
    std::vector<Done> done;
    auto undo = [&] {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            DeleteFileW(it->target.c_str());
            if (it->had_old) MoveFileExW((it->target + L".old-athelas").c_str(), it->target.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    };
    bool ok = true;
    for (mz_uint i = 0; i < count && ok; i++) {
        if (mz_zip_reader_is_file_a_directory(&ar, i)) continue;
        std::string rel = names[i].substr(std::min(strip, names[i].size()));
        if (rel.empty() || rel.find("..") != std::string::npos || rel[0] == '/' || rel.find(':') != std::string::npos) continue;
        for (char &ch : rel) if (ch == '/') ch = '\\';
        std::wstring target = g_exe_dir + L"\\" + widen(rel), fresh = target + L".new-athelas";
        size_t size = 0;
        void *p = mz_zip_reader_extract_to_heap(&ar, i, &size, 0);
        if (!p) { ok = fail("The update couldn't be unpacked (" + rel + ")."); break; }
        make_dirs(target);
        HANDLE f = CreateFileW(fresh.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD wrote = 0;
        bool written = f != INVALID_HANDLE_VALUE && WriteFile(f, p, (DWORD)size, &wrote, NULL) && wrote == size;
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        mz_free(p);
        if (!written) { DeleteFileW(fresh.c_str()); ok = fail("Couldn't write " + rel + "."); break; }
        bool had = GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (had && !MoveFileExW(target.c_str(), (target + L".old-athelas").c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(fresh.c_str());
            ok = fail("Couldn't replace " + rel + " (is a game still running?).");
            break;
        }
        if (!MoveFileExW(fresh.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            if (had) MoveFileExW((target + L".old-athelas").c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
            DeleteFileW(fresh.c_str());
            ok = fail("Couldn't replace " + rel + ".");
            break;
        }
        done.push_back({target, had});
        InterlockedExchange(&g_ipermille, (LONG)((i + 1) * 1000ull / count));
    }
    mz_zip_reader_end(&ar);
    if (!ok) { undo(); return false; }
    InterlockedExchange(&g_istate, 3);
    return true;
}

static std::string g_iversion;
static DWORD WINAPI install_thread(void *) { install(g_iversion); return 0; }

void update_install_start(const std::string &version)
{
    if (g_istate == 1 || g_istate == 2 || g_istate == 3) return;
    g_exe_dir = exe_dir_w();
    g_iversion = version;
    g_ierror.clear();
    InterlockedExchange(&g_ipermille, 0);
    InterlockedExchange(&g_istate, 1);
    HANDLE h = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else fail("The update couldn't start.");
}

int update_install_state(float *progress, std::string *error)
{
    if (progress) *progress = g_ipermille / 1000.0f;
    if (error && g_istate == -1) *error = g_ierror;
    return (int)g_istate;
}

// the files the last update replaced, left as <name>.old-athelas while the old launcher still ran them
static void tidy(const std::wstring &dir, int depth)
{
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 4) tidy(dir + L"\\" + n, depth + 1);
        } else if (n.size() > 12 && (n.compare(n.size() - 12, 12, L".old-athelas") == 0 || n.compare(n.size() - 12, 12, L".new-athelas") == 0)) {
            DeleteFileW((dir + L"\\" + n).c_str());
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

void update_tidy() { tidy(exe_dir_w(), 0); }

bool update_restart()
{
    std::wstring exe = exe_dir_w() + L"\\Athelas.exe";
    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!CreateProcessW(exe.c_str(), &cmd[0], NULL, NULL, FALSE, 0, NULL, exe_dir_w().c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
