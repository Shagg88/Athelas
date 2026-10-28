// The newest Athelas release on GitHub, asked for once at start in the background, so the launcher can say when a
// newer version than this one is out. Only the release page's address is fetched; nothing is sent but the request.
#pragma once
#include <string>

// starts the check (once; again only after update_check_reset); current = this launcher's version, "1.0.1"
void update_check_start(const std::string &current);
// the newer version's number when one is out ("1.0.2"), else empty: not known yet, no newer one, or no connection
std::string update_newer();
// whether the check has finished (found a version or failed)
bool update_checked();
// the page players download it from
const char *update_page();

// Installing it: the release's Athelas-<version>.zip (Athelas-<version>-arm64.zip on Windows on ARM) and its .sha256 are downloaded, the zip checked against it and
// unpacked over this folder (settings, saves and achievements are not in it, so they stay). Then update_restart.
void update_install_start(const std::string &version);
// 0 idle, 1 downloading, 2 unpacking, 3 done (restart), -1 failed (error says why); progress 0 to 1
int update_install_state(float *progress, std::string *error);
// at start: removes what the last update left behind
void update_tidy();
// starts Athelas again (the new one); the caller then quits
bool update_restart();

// a file from https://<host><path> (redirects followed), for the box art; false on any failure or an answer but 200
bool http_download(const wchar_t *host, const std::wstring &path, std::string &out);
