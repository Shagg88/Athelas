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
