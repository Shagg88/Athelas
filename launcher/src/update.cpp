// update.h: GitHub answers /releases/latest with a redirect to /releases/tag/<version>; the version is read from that
// address, without following it (no API, no rate limit, nothing to parse but one header).
#include "update.h"

#include <windows.h>
#include <winhttp.h>

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
