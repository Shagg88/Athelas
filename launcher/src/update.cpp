#include <functional>
// update.h: GitLab answers /-/releases/permalink/latest with a redirect to /-/releases/<tag>; the version is read
// from that address, without following it (no API, nothing to parse but one header). GitLab since 9 Nov 2026 (the
// GitHub account was suspended). Each release's tag is its version ("1.8.3") and it carries the .7z and its .sha256
// as release links with the direct paths /Athelas-<version>.7z and /Athelas-<version>.7z.sha256.
#include "update.h"

#include <windows.h>
#include <winhttp.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static volatile LONG g_started = 0, g_done = 0;
static std::string g_current, g_latest;
static std::string g_tag;   // the release's tag as its page has it ("1.8.4", or "v1.8.4"): the download's address uses it as it is
static CRITICAL_SECTION g_lock;

// GitHub first (Athelas' home again since 11 Nov 2026), GitLab when GitHub doesn't answer; the update is then
// downloaded from whichever answered
static const wchar_t kHost[] = L"github.com";
static volatile LONG g_fromGitLab = 0;
const char *update_page()
{
    return g_fromGitLab ? "https://gitlab.com/shagg88/athelas/-/releases" : "https://github.com/Shagg88/Athelas/releases/latest";
}

// a HEAD of https://<host><path> without following its redirect: the Location, or "" (none, or no answer)
static std::wstring redirect_of(HINTERNET s, const wchar_t *host, const wchar_t *path)
{
    std::wstring out;
    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"HEAD", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE) : NULL;
    if (r) {
        DWORD off = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof off);
        if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(r, NULL)) {
            wchar_t loc[1024];
            DWORD size = sizeof loc;
            if (WinHttpQueryHeaders(r, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &size, WINHTTP_NO_HEADER_INDEX))
                out = loc;
        }
        WinHttpCloseHandle(r);
    }
    if (c) WinHttpCloseHandle(c);
    return out;
}
// the part of loc after marker, up to / ? or # ("" when the marker isn't there)
static std::string tag_after(const std::wstring &loc, const wchar_t *marker)
{
    std::string found;
    size_t at = loc.find(marker);
    if (at == std::wstring::npos) return found;
    for (size_t i = at + wcslen(marker); i < loc.size() && loc[i] != L'/' && loc[i] != L'?' && loc[i] != L'#' && found.size() < 32; i++)
        found += (char)(loc[i] < 128 ? loc[i] : '?');
    return found;
}

// "v1.0.2" or "1.0.2" against "1.0.1": numbers compared part by part; anything else (a test build's "dev") is never newer
bool update_version_newer(const std::string &a, const std::string &b)
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
        // GitHub: /releases/latest redirects to /releases/tag/<version>
        found = tag_after(redirect_of(s, L"github.com", L"/Shagg88/Athelas/releases/latest"), L"/releases/tag/");
        if (found.empty()) {
            // GitLab: /-/releases/permalink/latest redirects to /-/releases/<version>
            found = tag_after(redirect_of(s, L"gitlab.com", L"/shagg88/athelas/-/releases/permalink/latest"), L"/-/releases/");
            if (found == "permalink") found.clear();
            if (!found.empty()) InterlockedExchange(&g_fromGitLab, 1);
        }
        WinHttpCloseHandle(s);
    }
    EnterCriticalSection(&g_lock);
    g_latest = update_version_newer(found, g_current) ? (found[0] == 'v' || found[0] == 'V' ? found.substr(1) : found) : "";
    g_tag = g_latest.empty() ? "" : found;
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

// ---- Installing an update: the release's .7z and its .sha256 from GitHub (or GitLab), checked, then unpacked over this folder.
// A running program and its DLLs can be renamed but not overwritten, so each file it replaces is first renamed to
// <name>.old-athelas (deleted at the next start) and the new one moved into its place. If anything fails half way,
// the files already replaced are put back.
#include "../third_party/miniz/miniz.h"
#include "unpack7z.h"
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

// a GET of https://<host><path> (gitlab.com unless given), redirects followed (release links lead to where the file is stored)
// With toFile, the body is written to that file as it arrives (never held in memory whole) and out gets its SHA-256
// as 64 hex digits.
static bool http_get(const std::wstring &path, std::string &out, bool progress, const wchar_t *host = kHost, HANDLE toFile = NULL)
{
    out.clear();
    BCRYPT_ALG_HANDLE halg = NULL;
    BCRYPT_HASH_HANDLE hh = NULL;
    if (toFile && (BCryptOpenAlgorithmProvider(&halg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0 ||
                   BCryptCreateHash(halg, &hh, NULL, 0, NULL, 0, 0) != 0)) {
        if (halg) BCryptCloseAlgorithmProvider(halg, 0);
        return false;
    }
    bool ok = false;
    HINTERNET s = WinHttpOpen(L"Athelas launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) {
        if (hh) BCryptDestroyHash(hh);
        if (halg) BCryptCloseAlgorithmProvider(halg, 0);
        return false;
    }
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
            // one block of the right size up front: growing a 124 MB string in a 32-bit launcher needs a 256 MB block
            // at some point, which can fail; a failure is an ordinary "couldn't download", not a crash
            try { if (total && !toFile) out.reserve(total); } catch (...) { ok = false; }
            char buf[65536];
            DWORD got = 0;
            unsigned long long sofar = 0;
            for (;;) {
                if (!WinHttpReadData(r, buf, sizeof buf, &got)) { ok = false; break; }
                if (!got) break;
                if (toFile) {
                    DWORD wrote = 0;
                    if (!WriteFile(toFile, buf, got, &wrote, NULL) || wrote != got || BCryptHashData(hh, (PUCHAR)buf, got, 0) != 0) { ok = false; break; }
                } else {
                    // (only the download itself is large, and it goes to a file: a checksum or a page
                    // of this size isn't one)
                    if (out.size() + got > (16u << 20)) { ok = false; break; }
                    try { out.append(buf, got); } catch (...) { ok = false; break; }
                }
                sofar += got;
                if (progress && total) InterlockedExchange(&g_ipermille, (LONG)(sofar * 1000ull / total));
            }
            if (total && sofar != total) ok = false;
        }
    }
    if (toFile) {
        unsigned char d[32];
        if (ok && BCryptFinishHash(hh, d, 32, 0) == 0)
            for (unsigned char v : d) { out += "0123456789abcdef"[v >> 4]; out += "0123456789abcdef"[v & 15]; }
        else ok = false;
        BCryptDestroyHash(hh);
        BCryptCloseAlgorithmProvider(halg, 0);
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

bool http_download(const wchar_t *host, const std::wstring &path, std::string &out) { return http_get(path, out, false, host); }

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

// the reason only: the thread reports "failed" once everything is undone (install_thread), so "Try again" can't
// start a second install while the first is still putting files back
static bool fail(const std::string &why)
{
    if (g_ierror.empty()) g_ierror = why;
    return false;
}
// an install in progress, noted beside the launcher: if Athelas ends before it finishes (closed, crashed, power cut),
// the next start puts the old files back (update_tidy) instead of keeping a mix of two versions
static std::wstring journal_path() { return g_exe_dir + L"\\athelas-update-unfinished.txt"; }
// a read-only file (7-Zip keeps the attribute when it extracts; 1.8.3's .7z had 81 such files) can be renamed but not
// deleted or moved onto: before either, the attribute is cleared (review). Nothing happens when it isn't there.
static void make_writable(const std::wstring &p)
{
    DWORD a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY) && !(a & FILE_ATTRIBUTE_DIRECTORY))
        SetFileAttributesW(p.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
}

static bool is_hex64(const std::string &t)
{
    if (t.size() != 64) return false;
    for (char ch : t) if (!isxdigit((unsigned char)ch)) return false;
    return true;
}

static std::wstring download_path() { return g_exe_dir + L"\\athelas-update.download"; }

static void finish_unfinished();
static bool install(const std::string &version)
{
    finish_unfinished();   // a journal left by an earlier attempt: its files are put back before anything new
    if (GetFileAttributesW(journal_path().c_str()) != INVALID_FILE_ATTRIBUTES)   // it couldn't be (before the download, not after)
        return fail("An earlier update couldn't be fully undone (a file was in use, or another Athelas window is installing). "
                    "Close every Athelas window, start it again, then try again.");
    // somewhere to write first (a protected folder such as Program Files needs the download page instead)
    std::wstring probe = g_exe_dir + L"\\athelas-update-test.tmp";
    HANDLE ph = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (ph == INVALID_HANDLE_VALUE) return fail("Windows doesn't let Athelas change its own folder. Download the update from its page instead.");
    CloseHandle(ph);

    // one download for every PC since 1.7.7 (it carries the ports' ARM64 builds and ARM64.txt as well); a .7z since
    // 1.8.3 (smaller), the .zip for a release before it
    // GitHub: /releases/download/<tag>/Athelas-<version>.7z; GitLab: /-/releases/<tag>/downloads/Athelas-<version>.7z
    // (the release link's direct path)
    const bool gl = g_fromGitLab != 0;
    const wchar_t *host = gl ? L"gitlab.com" : L"github.com";
    // (the tag as the release has it - a "v1.8.4" tag's files are under v1.8.4 - and the file's name without the v)
    EnterCriticalSection(&g_lock);
    std::wstring rtag = widen(g_tag.empty() ? version : g_tag);
    LeaveCriticalSection(&g_lock);
    std::wstring tag = gl ? L"/shagg88/athelas/-/releases/" + rtag + L"/downloads/Athelas-" + widen(version)
                          : L"/Shagg88/Athelas/releases/download/" + rtag + L"/Athelas-" + widen(version);
    std::wstring page_path = gl ? L"/shagg88/athelas/-/releases/" + rtag : L"/Shagg88/Athelas/releases/tag/" + rtag;
    auto file = [](const std::wstring &p) { return p; };
    std::string sum, zip;
    bool sevenz = false;
    // its checksum: the .sha256 beside the download, else the one the release notes give ("SHA-256: `...`")
    for (int k = 0; k < 2 && sum.empty(); k++) {
        std::string s;
        if (http_get(file(tag + (k == 0 ? L".7z.sha256" : L".zip.sha256")), s, false, host) && s.size() >= 64 && is_hex64(s.substr(0, 64))) {
            sum = s; sevenz = k == 0;
        }
    }
    std::wstring base = tag + (sevenz ? L".7z" : L".zip");
    if (sum.empty()) {
        std::string page;
        if (http_get(page_path, page, false, host)) {
            size_t at = page.find("SHA-256");
            while (at != std::string::npos && sum.empty()) {
                for (size_t i = at; i + 64 <= page.size() && i < at + 400; i++)
                    if (is_hex64(page.substr(i, 64)) && (i == 0 || !isxdigit((unsigned char)page[i - 1]))) { sum = page.substr(i, 64); break; }
                at = page.find("SHA-256", at + 7);
            }
            sevenz = page.find("Athelas-" + version + ".7z") != std::string::npos;
            base = tag + (sevenz ? L".7z" : L".zip");
        }
        if (sum.empty()) return fail("The update's checksum couldn't be found. Try again later, or download it from its page.");
    }
    sum = sum.substr(0, 64);
    for (char &ch : sum) ch = (char)tolower((unsigned char)ch);
    // the download goes to a file beside Athelas.exe, not into memory. One handle from the download to the end of the
    // unpacking (others may read it, none may write, replace or delete it), then the file is deleted, however this ends.
    struct DownloadFile {
        std::wstring path;
        HANDLE h;
        ~DownloadFile() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); DeleteFileW(path.c_str()); }
    } dl{ download_path(), INVALID_HANDLE_VALUE };
    dl.h = CreateFileW(dl.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (dl.h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION) return fail("Another Athelas window is downloading the update. Close it, then try again.");
        return fail("Windows doesn't let Athelas change its own folder. Download the update from its page instead.");
    }
    {
        std::string got;
        if (!http_get(file(base), got, true, host, dl.h))
            return fail("The download didn't finish. Check the internet connection (and the free disk space) and try again.");
        if (got != sum) return fail("The download was damaged (its checksum doesn't match). Try again.");
    }
    if (!sevenz) {   // (a release before 1.8.3: its .zip is small enough to read whole)
        LARGE_INTEGER zs, zero;
        zero.QuadPart = 0;
        bool read = GetFileSizeEx(dl.h, &zs) && zs.QuadPart < (300ll << 20) && SetFilePointerEx(dl.h, zero, NULL, FILE_BEGIN);
        if (read) {
            zip.resize((size_t)zs.QuadPart);
            DWORD n = 0;
            read = zip.empty() || (ReadFile(dl.h, &zip[0], (DWORD)zip.size(), &n, NULL) && n == zip.size());
        }
        if (!read) return fail("The update couldn't be opened.");
    }

    InterlockedExchange(&g_istate, 2);
    HANDLE jf = CreateFileW(journal_path().c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    auto journal = [&](const std::wstring &target, bool had) {   // "1 <path>": an old copy kept; "0 <path>": a new file
        if (jf == INVALID_HANDLE_VALUE) return;
        // relative to Athelas' folder: the undo still finds the files when the folder is reached by another spelling
        // (c:\games against C:\Games, a short name, a moved folder) at the next start
        std::wstring rel = target.size() > g_exe_dir.size() + 1 ? target.substr(g_exe_dir.size() + 1) : target;
        std::wstring line = (had ? L"1 " : L"0 ") + rel + L"\n";
        int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), NULL, 0, NULL, NULL);
        std::string u(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), &u[0], n, NULL, NULL);
        DWORD w;
        WriteFile(jf, u.data(), (DWORD)u.size(), &w, NULL);
        FlushFileBuffers(jf);
    };
    struct Done { std::wstring target; bool had_old; };
    std::vector<Done> done;
    // false: a file couldn't be put back (antivirus holding a new exe, say). The journal is then kept, so the next
    // start finishes the undo (finish_unfinished) before its tidy-up deletes the .old-athelas copies (review)
    auto undo = [&]() -> bool {
        bool all = true;
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            make_writable(it->target);
            if (!DeleteFileW(it->target.c_str()) && GetFileAttributesW(it->target.c_str()) != INVALID_FILE_ATTRIBUTES) all = false;
            if (it->had_old && !MoveFileExW((it->target + L".old-athelas").c_str(), it->target.c_str(), MOVEFILE_REPLACE_EXISTING)) all = false;
        }
        return all;
    };
    // an exception (out of memory while unpacking) must still undo, close the journal and delete it: otherwise a
    // "Try again" would treat the half-installed new files as the old ones and overwrite the real backups
    struct Guard {
        bool armed = true; HANDLE jf; std::function<bool()> undo;
        ~Guard() { if (!armed) return; bool back = undo(); if (jf != INVALID_HANDLE_VALUE) CloseHandle(jf); if (back) DeleteFileW(journal_path().c_str()); }
    } guard{true, jf, undo};

    // one file of the update put in place: written beside it first, the old one kept until the whole update is in
    auto put = [&](std::string rel, const void *p, size_t size) -> bool {
        if (rel.empty() || rel.find("..") != std::string::npos || rel[0] == '/' || rel.find(':') != std::string::npos) return true;
        for (char &ch : rel) if (ch == '/') ch = '\\';
        std::wstring target = g_exe_dir + L"\\" + widen(rel), fresh = target + L".new-athelas";
        make_dirs(target);
        HANDLE f = CreateFileW(fresh.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD wrote = 0;
        bool written = f != INVALID_HANDLE_VALUE && (size == 0 || (WriteFile(f, p, (DWORD)size, &wrote, NULL) && wrote == size));
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        if (!written) { DeleteFileW(fresh.c_str()); return fail("Couldn't write " + rel + "."); }
        bool had = GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES;
        make_writable(target);                       // (so its .old-athelas copy can be deleted later)
        make_writable(target + L".old-athelas");     // one an earlier update left read-only is moved onto
        journal(target, had);   // before the move: a start after a crash here still finds the old copy
        if (had && !MoveFileExW(target.c_str(), (target + L".old-athelas").c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(fresh.c_str());
            return fail("Couldn't replace " + rel + " (is a game still running?).");
        }
        if (!MoveFileExW(fresh.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            if (had) MoveFileExW((target + L".old-athelas").c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
            DeleteFileW(fresh.c_str());
            return fail("Couldn't replace " + rel + ".");
        }
        done.push_back({target, had});
        return true;
    };
    bool ok = true;
    if (sevenz) {
        // made by package.sh with its files at the top (Athelas.exe beside the folders), in blocks small enough for
        // this 32-bit program to unpack one at a time
        bool any = false;
        ok = unpack7z_handle(dl.h, [&](const std::string &name, bool dir, const void *data, size_t size) {
                 if (dir) return true;
                 any = true;
                 return put(name, data, size);
             },
             [&](unsigned n, unsigned count) { InterlockedExchange(&g_ipermille, (LONG)(n * 1000ull / (count ? count : 1))); });
        if (!ok) fail("The update couldn't be opened or unpacked.");
        if (ok && !any) ok = fail("The update had no files in it. Try again, or download it from its page.");
    } else {
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
        for (mz_uint i = 0; i < count && ok; i++) {
            if (mz_zip_reader_is_file_a_directory(&ar, i)) continue;
            std::string rel = names[i].substr(std::min(strip, names[i].size()));
            size_t size = 0;
            void *p = mz_zip_reader_extract_to_heap(&ar, i, &size, 0);
            if (!p) { ok = fail("The update couldn't be unpacked (" + rel + ")."); break; }
            ok = put(rel, p, size);
            mz_free(p);
            InterlockedExchange(&g_ipermille, (LONG)((i + 1) * 1000ull / count));
        }
        mz_zip_reader_end(&ar);
    }
    guard.armed = false;
    bool back = ok || undo();
    if (jf != INVALID_HANDLE_VALUE) CloseHandle(jf);
    if (back) DeleteFileW(journal_path().c_str());   // finished, or undone (kept when the undo couldn't finish)
    if (!ok) return false;
    InterlockedExchange(&g_istate, 3);
    return true;
}

static std::string g_iversion;
static DWORD WINAPI install_thread(void *)
{
    bool ok = false;
    try { ok = install(g_iversion); }
    catch (...) { fail("Athelas ran out of memory installing the update. Close other programs and try again."); }
    if (!ok) {
        if (g_ierror.empty()) g_ierror = "The update couldn't be installed.";
        InterlockedExchange(&g_istate, -1);
    }
    return 0;
}

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
    else { fail("The update couldn't start."); InterlockedExchange(&g_istate, -1); }
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
        } else if (n.size() > 12 && (n.compare(n.size() - 12, 12, L".old-athelas") == 0 || n.compare(n.size() - 12, 12, L".new-athelas") == 0 ||
                                      n.compare(n.size() - 12, 12, L".del-athelas") == 0)) {
            make_writable(dir + L"\\" + n);
            DeleteFileW((dir + L"\\" + n).c_str());
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

// an install that never finished (see journal_path): its new files are taken away and the old ones put back
static void finish_unfinished()
{
    g_exe_dir = exe_dir_w();
    std::wstring j = journal_path();
    HANDLE f = CreateFileW(j.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    std::string data;
    char buf[65536];
    DWORD got;
    while (ReadFile(f, buf, sizeof buf, &got, NULL) && got) data.append(buf, got);
    CloseHandle(f);
    std::vector<std::string> lines;
    for (size_t at = 0; at < data.size();) {
        size_t e = data.find('\n', at);
        if (e == std::string::npos) break;   // a half-written last line: its file wasn't touched yet
        lines.push_back(data.substr(at, e - at));
        at = e + 1;
    }
    bool all_ok = true;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (it->size() < 3) continue;
        std::wstring target = widen(it->substr(2));
        if (target.size() > 2 && target[1] == L':') {   // (a full path: a journal from before they were relative)
            if (target.size() <= g_exe_dir.size() || _wcsnicmp(target.c_str(), g_exe_dir.c_str(), g_exe_dir.size()) != 0) { all_ok = false; continue; }
        } else {
            if (target.find(L"..") != std::wstring::npos || target[0] == L'\\' || target[0] == L'/') { all_ok = false; continue; }
            target = g_exe_dir + L"\\" + target;
        }
        std::wstring old = target + L".old-athelas";
        make_writable(target);
        make_writable(target + L".del-athelas");
        if ((*it)[0] == '1') {
            if (GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES &&
                !MoveFileExW(old.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                // in use (this very Athelas.exe): moved aside instead, and deleted at a later start
                MoveFileExW(target.c_str(), (target + L".del-athelas").c_str(), MOVEFILE_REPLACE_EXISTING);
                if (!MoveFileExW(old.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) all_ok = false;
            }
        } else {
            if (!DeleteFileW(target.c_str()) && !MoveFileExW(target.c_str(), (target + L".del-athelas").c_str(), MOVEFILE_REPLACE_EXISTING) &&
                GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) all_ok = false;
        }
    }
    if (all_ok) DeleteFileW(j.c_str());   // kept when a file was locked: the next start tries again
}

void update_tidy()
{
    finish_unfinished();   // first: it needs the .old-athelas copies tidy() deletes
    // a journal still there: another Athelas window is installing right now, or an undo isn't finished - the
    // .old-athelas copies are its only way back, so nothing is tidied this time (review)
    if (GetFileAttributesW(journal_path().c_str()) != INVALID_FILE_ATTRIBUTES) return;
    DeleteFileW((exe_dir_w() + L"\\athelas-update.download").c_str());   // a download a closed launcher left behind
    tidy(exe_dir_w(), 0);
}

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

// ---- After an update (by the launcher, the installer, or a .7z extracted by hand): the files an older Athelas
// shipped that this one doesn't are taken out of the way, so nothing left over can clash with the new version
// (Shagg, 8 Nov 2026). They are moved, not deleted (Shagg, 13 Nov 2026), into the folder "Old files - safe to
// delete" beside Athelas.exe, each under its old path: the player deletes that folder when they like, and nothing
// is lost if one was wanted after all. So a file the player changed goes there too (it could clash just the same).
// athelas-files.txt (this download's files and their SHA-256) against athelas-installed.txt (the list the last run
// saw) and athelas-shipped-before.txt (what 1.8.2 shipped, for a folder from before these lists). Only paths an
// Athelas download put there are looked at: files the player added, settings, saves, shader caches, logs, texture
// packs, covers and the games' own files are never touched.
#include <map>
static bool read_all(const std::wstring &path, std::string &out)
{
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER n;
    bool ok = GetFileSizeEx(f, &n) && n.QuadPart < (1LL << 30);
    if (ok) {
        out.resize((size_t)n.QuadPart);
        DWORD got = 0;
        ok = out.empty() || (ReadFile(f, &out[0], (DWORD)out.size(), &got, NULL) && got == out.size());
    }
    CloseHandle(f);
    return ok;
}
// "<sha256>  <path>" lines; the path lower-cased with backslashes as the key
static void read_list(const std::wstring &path, std::map<std::string, std::string> &into)
{
    std::string t;
    if (!read_all(path, t)) return;
    size_t at = 0;
    while (at < t.size()) {
        size_t e = t.find('\n', at);
        std::string l = t.substr(at, e == std::string::npos ? std::string::npos : e - at);
        at = e == std::string::npos ? t.size() : e + 1;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (l.size() < 67 || !is_hex64(l.substr(0, 64)) || l[64] != ' ') continue;
        std::string p = l.substr(l[65] == ' ' || l[65] == '*' ? 66 : 65);
        for (char &ch : p) ch = ch == '/' ? '\\' : (char)tolower((unsigned char)ch);
        into[p] = l.substr(0, 64);
    }
}
static bool protected_path(const std::string &p)
{
    if (p.empty() || p.find("..") != std::string::npos || p.find(':') != std::string::npos || p[0] == '\\') return true;
    for (const char *part : { "saves\\", "shadercache", "logs\\", "reports\\", "textures\\", "screenshots", "my game\\", "covers\\",
                              "old files - safe to delete\\" })
        if (p.find(part) != std::string::npos) return true;
    for (const char *ext : { ".ini", ".toml", ".sav", ".log" })
        if (p.size() > strlen(ext) && !p.compare(p.size() - strlen(ext), strlen(ext), ext)) return true;
    return p == "athelas-files.txt" || p == "athelas-installed.txt";
}

static const wchar_t *const kOldFilesDir = L"Old files - safe to delete";
int update_remove_leftovers()
{
    std::wstring dir = exe_dir_w();
    std::string now, before;
    if (!read_all(dir + L"\\athelas-files.txt", now)) return 0;    // a build from source: no list, nothing done
    if (read_all(dir + L"\\athelas-installed.txt", before) && before == now) return 0;   // the same version as last time
    // an update being put in place by another window, or an undo not yet finished: the files may be a mix of two
    // versions, so nothing is judged left over until it is done (review)
    if (GetFileAttributesW((dir + L"\\athelas-update-unfinished.txt").c_str()) != INVALID_FILE_ATTRIBUTES) return 0;
    std::map<std::string, std::string> cur, old;
    read_list(dir + L"\\athelas-files.txt", cur);
    if (cur.empty()) return 0;
    read_list(dir + L"\\athelas-shipped-before.txt", old);
    read_list(dir + L"\\athelas-installed.txt", old);
    int removed = 0;
    std::string log, stuckLines;   // stuckLines: kept in the installed list, so the next start tries them again
    for (auto &kv : old) {
        if (cur.count(kv.first) || protected_path(kv.first)) continue;
        std::wstring f = dir + L"\\" + widen(kv.first);
        DWORD attr = GetFileAttributesW(f.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;   // gone already
        // never through a linked folder (a junction or symbolic link the player made, a game folder kept elsewhere):
        // what is behind it isn't Athelas' to tidy (review)
        bool linked = (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        for (std::wstring d = f.substr(0, f.find_last_of(L'\\')); !linked && d.size() > dir.size(); d = d.substr(0, d.find_last_of(L'\\'))) {
            DWORD da = GetFileAttributesW(d.c_str());
            linked = da == INVALID_FILE_ATTRIBUTES || (da & FILE_ATTRIBUTE_REPARSE_POINT);
        }
        if (linked) continue;
        // moved under its old path (the folders made first); a copy an earlier update put there is kept: this one
        // gets a number (name.2, name.3...)
        std::wstring to = dir + L"\\" + kOldFilesDir + L"\\" + widen(kv.first);
        for (size_t at = dir.size() + 1; (at = to.find(L'\\', at)) != std::wstring::npos; at++)
            CreateDirectoryW(to.substr(0, at).c_str(), NULL);
        for (int n = 2; n < 100 && GetFileAttributesW(to.c_str()) != INVALID_FILE_ATTRIBUTES; n++) {
            std::wstring base = dir + L"\\" + kOldFilesDir + L"\\" + widen(kv.first);
            to = base + L"." + std::to_wstring(n);
        }
        make_writable(f);
        if (!MoveFileExW(f.c_str(), to.c_str(), 0)) {   // in use (a game running): tried again next start
            stuckLines += kv.second + "  " + kv.first + "\r\n";
            continue;
        }
        removed++;
        log += kv.first + "\r\n";
        // its folder too, once nothing is left in it (RemoveDirectory only takes an empty one; never a linked one)
        for (std::wstring d = f.substr(0, f.find_last_of(L'\\')); d.size() > dir.size(); d = d.substr(0, d.find_last_of(L'\\'))) {
            DWORD da = GetFileAttributesW(d.c_str());
            if (da == INVALID_FILE_ATTRIBUTES || (da & FILE_ATTRIBUTE_REPARSE_POINT) || !RemoveDirectoryW(d.c_str())) break;
        }
    }
    if (removed) {
        // added to (each update's moves under their own heading), not written over
        HANDLE h = CreateFileW((dir + L"\\athelas-cleanup.log").c_str(), FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            std::string head = "Left over from an older Athelas and moved into the folder \"Old files - safe to delete\" (" +
                               std::to_string(removed) + "). Delete that folder whenever you like:\r\n";
            DWORD w;
            WriteFile(h, head.data(), (DWORD)head.size(), &w, NULL);
            WriteFile(h, log.data(), (DWORD)log.size(), &w, NULL);
            CloseHandle(h);
        }
    }
    // the installed list: this version's, plus any file that couldn't go yet (a game still running), so the next
    // start tries those again - and a later version still compares against this version's list (review)
    {
        std::string list = now;
        if (!list.empty() && list.back() != '\n') list += "\r\n";
        list += stuckLines;
        std::wstring tmp = dir + L"\\athelas-installed.txt.new-athelas";
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD w = 0;
        bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, list.data(), (DWORD)list.size(), &w, NULL) && w == list.size();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        make_writable(dir + L"\\athelas-installed.txt");
        if (!ok || !MoveFileExW(tmp.c_str(), (dir + L"\\athelas-installed.txt").c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(tmp.c_str());
    }
    return removed;
}
