// Athelas - the launcher's own tests: the parts where a mistake costs a player their settings or a safe update.
// Built and run by launcher/test.sh (the 32-bit program, under Wine on Linux or directly on Windows).
//   tests.exe <folder with test.7z and its expected files>
#include <windows.h>
#include <cstdio>
#include <map>
#include <string>
#include "../src/config.h"
#include "../src/port.h"
#include "../src/unpack7z.h"
#include "../src/update.h"

void language_combo() {}   // main.cpp's (not linked here: it has the launcher's WinMain)

static int g_failed = 0, g_run = 0;
#define CHECK(what) do { g_run++; if (!(what)) { g_failed++; printf("FAILED line %d: %s\n", __LINE__, #what); } } while (0)

static std::string slurp(const std::string &p)
{
    std::string s;
    if (FILE *f = fopen(p.c_str(), "rb")) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); }
    return s;
}
static void spit(const std::string &p, const std::string &s)
{
    if (FILE *f = fopen(p.c_str(), "wb")) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
}

int main(int argc, char **argv)
{
    std::string dir = argc > 1 ? argv[1] : ".";

    // ---- versions: which is newer
    CHECK(update_version_newer("1.8.4", "1.8.3"));
    CHECK(update_version_newer("v1.8.4", "1.8.3"));
    CHECK(update_version_newer("1.10.0", "1.9.9"));
    CHECK(update_version_newer("2.0", "1.99.99"));
    CHECK(!update_version_newer("1.8.3", "1.8.3"));
    CHECK(!update_version_newer("1.8.2", "1.8.3"));
    CHECK(!update_version_newer("1.8", "1.8.0"));
    CHECK(!update_version_newer("dev-abc1234", "1.8.3"));   // a test build's name is never "newer"
    CHECK(!update_version_newer("", "1.8.3"));
    CHECK(!update_version_newer("1.8.4", "dev"));

    // ---- a port's settings file: edits keep everything else as the player left it
    std::string toml = dir + "\\settings-test.toml";
    spit(toml, "# my notes\r\n[display]\r\nmode = \"borderless\"   # as I like it\r\nwidth = 1920\r\n\r\n[graphics]\r\nmsaa = \"4\"\r\n"
               "path = \"C:\\\\Games\\\\a \\\"b\\\"\"\r\n\r\n[[extra]]\r\nwidth = 7\r\n\r\n[unknown]\r\nkept = true\r\n");
    PortSettingsFile f;
    CHECK(settings_file_load(toml, f));
    CHECK(!f.unreadable);
    CHECK(f.get("display", "mode", "") == "\"borderless\"");
    CHECK(f.get("display", "width", "") == "1920");
    CHECK(f.get("display", "height", "720") == "720");          // not there: the default
    CHECK(toml_unquote(f.get("graphics", "path", "")) == "C:\\Games\\a \"b\"");
    f.set("display", "width", "2560");
    f.set("display", "height", "1440");                          // a new key in a section that exists
    f.set("sound", "volume", "80");                              // a new section
    f.remove("graphics", "msaa");
    CHECK(settings_file_save(toml, f));
    PortSettingsFile g;
    CHECK(settings_file_load(toml, g));
    CHECK(g.get("display", "width", "") == "2560");
    CHECK(g.get("display", "height", "") == "1440");
    CHECK(g.get("sound", "volume", "") == "80");
    CHECK(g.get("graphics", "msaa", "gone") == "gone");
    CHECK(g.get("unknown", "kept", "") == "true");               // a section the launcher doesn't know stays
    std::string after = slurp(toml);
    CHECK(after.find("# my notes") != std::string::npos);
    CHECK(after.find("# as I like it") != std::string::npos);
    CHECK(after.find("[[extra]]") != std::string::npos);
    CHECK(after.find("width = 7") != std::string::npos);        // the array table's own width wasn't taken for [display]'s
    CHECK(toml_unquote(toml_quote("a \"b\" \\ c")) == "a \"b\" \\ c");
    // a file that isn't there: loads as empty and may be saved; never marked unreadable
    PortSettingsFile none;
    settings_file_load(dir + "\\no-such-file.toml", none);
    CHECK(!none.unreadable);
    DeleteFileA(toml.c_str());

    // ---- older settings files to UTF-8: only the lines that aren't UTF-8 yet, the original kept, UTF-16 left alone
    std::string ini = dir + "\\conv-test.ini";
    DeleteFileA((ini + ".before-utf8").c_str());
    spit(ini, "[Games]\r\nA = C:\\Jeux\\\xe9t\xe9\r\nB = C:\\\xd0\x98\xd0\xb3\xd1\x80\xd1\x8b\r\nC = plain\r\n");   // A: code page 1252; B: UTF-8 already
    CHECK(text_file_to_utf8(ini, 1252));
    CHECK(slurp(ini) == "[Games]\r\nA = C:\\Jeux\\\xc3\xa9t\xc3\xa9\r\nB = C:\\\xd0\x98\xd0\xb3\xd1\x80\xd1\x8b\r\nC = plain\r\n");
    CHECK(slurp(ini + ".before-utf8").find("\xe9t\xe9") != std::string::npos);
    CHECK(!text_file_to_utf8(ini, 1252));                        // a second time: nothing left to do
    spit(ini, "[Games]\r\nA = \xe8\r\n");
    CHECK(text_file_to_utf8(ini, 1252));
    CHECK(slurp(ini + ".before-utf8").find("\xe9t\xe9") != std::string::npos);   // the first original is kept
    spit(ini, std::string("\xff\xfe[\0G\0", 6));
    CHECK(!text_file_to_utf8(ini, 1252));                        // UTF-16: left alone
    spit(ini, "[Games]\r\nA = plain\r\n");
    CHECK(!text_file_to_utf8(ini, 1252));                        // ASCII: left alone
    CHECK(!text_file_to_utf8(dir + "\\no-such.ini", 1252));
    DeleteFileA(ini.c_str()); DeleteFileA((ini + ".before-utf8").c_str());

    // ---- unpacking a release .7z from its file (test.sh makes test.7z the way package.sh makes a release)
    std::map<std::string, std::string> got;
    unsigned lastDone = 0, lastCount = 0;
    std::wstring w7(dir.begin(), dir.end());
    bool ok = unpack7z_file(w7 + L"\\test.7z",
                            [&](const std::string &name, bool isDir, const void *data, size_t size) {
                                if (!isDir) got[name].assign((const char *)data, size);
                                return true;
                            },
                            [&](unsigned done, unsigned count) { lastDone = done; lastCount = count; });
    CHECK(ok);
    CHECK(got.size() == 3);
    CHECK(got["Athelas.txt"] == slurp(dir + "\\src7z\\Athelas.txt"));
    CHECK(got["folder/inner.bin"] == slurp(dir + "\\src7z\\folder\\inner.bin"));
    CHECK(got["folder/empty.txt"].empty());
    CHECK(lastCount > 0 && lastDone == lastCount);
    // the same from memory (the older path), and a damaged archive is refused, not half-read
    std::string bytes = slurp(dir + "\\test.7z");
    size_t n2 = 0;
    CHECK(unpack7z(bytes, [&](const std::string &, bool isDir, const void *, size_t) { if (!isDir) n2++; return true; }, nullptr));
    CHECK(n2 == 3);
    std::string cut = bytes.substr(0, bytes.size() / 2);
    CHECK(!unpack7z(cut, [&](const std::string &, bool, const void *, size_t) { return true; }, nullptr));
    CHECK(!unpack7z_file(w7 + L"\\no-such.7z", [&](const std::string &, bool, const void *, size_t) { return true; }, nullptr));

    printf("%d checks, %d failed\n", g_run, g_failed);
    return g_failed ? 1 : 0;
}
