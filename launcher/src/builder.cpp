// Athelas - building a port's program on the player's PC (builder.h). The steps are tools\kit\kit_build.ps1's, which
// proved them on all three Xbox ports (job 012/013: 25-43 s on 16 threads, the same program as the normal build).
#include "builder.h"
#include "config.h"
#include "json.h"
#include "port.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <algorithm>
#include <map>
#include <vector>

namespace {

CRITICAL_SECTION g_cs;
bool g_csInit = false;
BuildStatus g_st;
volatile LONG g_busy = 0, g_cancel = 0;
HANDLE g_thread = NULL;
FILE *g_log = nullptr;

struct Job { std::string folder, exe, game, logPath; LibGame g; };
Job g_job;

void lock() { if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; } EnterCriticalSection(&g_cs); }
void unlock() { LeaveCriticalSection(&g_cs); }
void logf(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

bool exists(const std::string &p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool read_file(const std::string &p, std::string &out)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char b[65536]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) out.append(b, n);
    fclose(f);
    return true;
}
bool write_file(const std::string &p, const std::string &s)
{
    FILE *f = fopen(p.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
    return fclose(f) == 0 && ok;
}
uint64_t fnv(const std::string &s)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}
std::string hex(uint64_t v) { char b[24]; snprintf(b, sizeof b, "%016llx", (unsigned long long)v); return b; }
std::string fwd(std::string s) { std::replace(s.begin(), s.end(), '\\', '/'); return s; }
void remove_tree(const std::string &dir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = fd.cFileName;
            if (n == "." || n == "..") continue;
            std::string p = dir + "\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(p); else { SetFileAttributesA(p.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileA(p.c_str()); }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}
std::vector<std::string> list_files(const std::string &dir, const char *pattern)
{
    std::vector<std::string> r;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return r;
    do if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) r.push_back(fd.cFileName); while (FindNextFileA(h, &fd));
    FindClose(h);
    return r;
}

// The player's default.xbe: from the game folder, or from inside the disc image
bool read_xbe(const LibGame &g, std::string &out)
{
    if (!g.iso) return read_file(g.path + "\\default.xbe", out);
    unsigned long long off = 0; uint32_t size = 0;
    if (!iso_locate(g.path, "default.xbe", off, size) || !size) return false;
    FILE *f = fopen(g.path.c_str(), "rb");
    if (!f) return false;
    out.resize(size);
    bool ok = _fseeki64(f, (long long)off, SEEK_SET) == 0 && fread(&out[0], 1, size, f) == size;
    fclose(f);
    return ok;
}

// kit.json, without the mark Windows PowerShell writes before UTF-8
bool read_kit(const std::string &kit, std::string &out)
{
    if (!read_file(kit + "\\kit.json", out)) return false;
    if (out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
    return true;
}
std::string stamp_text(const std::string &kitJson, const std::string &xbe)
{
    return "kit " + hex(fnv(kitJson)) + "\nxbe " + hex(fnv(xbe)) + "\n";
}

// a command line argument: quoted when it has a space, unless it carries its own quoting (-DGAME_VERSION=\"1.6.0\")
std::string q(const std::string &a)
{
    if (a.find_first_of(" \t") == std::string::npos || a.find('"') != std::string::npos) return a;
    return "\"" + a + "\"";
}

// Starts 'exe @rsp'-style programs with the build tools' environment, their output going to 'outFile'
HANDLE launch(const std::string &exe, const std::string &args, const std::string &cwd, const std::string &env, const std::string &outFile)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE out = CreateFileA(outFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) return NULL;
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out; si.hStdError = out; si.hStdInput = NULL;
    PROCESS_INFORMATION pi;
    std::string cmd = q(exe) + " " + args;
    BOOL ok = CreateProcessA(exe.c_str(), &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                             env.empty() ? NULL : (void *)env.data(), cwd.c_str(), &si, &pi);
    CloseHandle(out);
    if (!ok) return NULL;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}
int wait_exit(HANDLE p)
{
    WaitForSingleObject(p, INFINITE);
    DWORD c = (DWORD)-1;
    GetExitCodeProcess(p, &c);
    CloseHandle(p);
    return (int)c;
}
void log_file_tail(const std::string &f, int lines)
{
    std::string s;
    if (!read_file(f, s)) return;
    std::vector<std::string> ls;
    size_t st = 0;
    while (st < s.size()) { size_t e = s.find('\n', st); if (e == std::string::npos) e = s.size(); { std::string l = s.substr(st, e - st); if (!l.empty() && l.back() == '\r') l.pop_back(); ls.push_back(l); } st = e + 1; }
    for (size_t i = ls.size() > (size_t)lines ? ls.size() - lines : 0; i < ls.size(); i++) logf("  | %s", ls[i].c_str());
}

void set_phase(BuildPhase p) { lock(); g_st.phase = p; unlock(); }
bool cancelled() { return g_cancel != 0; }
DWORD stopped() { logf("cancelled"); set_phase(BP_CANCELLED); return 2; }
DWORD fail(const std::string &plain, const char *detail = "")
{
    logf("FAILED: %s %s", plain.c_str(), detail);
    lock(); g_st.phase = BP_FAILED; g_st.error = plain; unlock();
    return 1;
}

// The build tools: the llvm-mingw Athelas ships (tools\llvm-mingw beside Athelas.exe: clang, lld and the mingw-w64
// headers and libraries, all free to redistribute). Nothing is downloaded and nothing of Microsoft's SDK is needed.
bool find_tools(const std::string &arch, std::string &clangxx, std::string &env, std::string &version)
{
    std::string triple = arch == "arm64" ? "aarch64-w64-mingw32" : "x86_64-w64-mingw32";
    clangxx = exe_dir() + "\\tools\\llvm-mingw\\bin\\" + triple + "-clang++.exe";
    env.clear();   // (the tools find their own headers and libraries)
    if (!exists(clangxx)) { logf("no %s", clangxx.c_str()); return false; }
    std::string v;
    run_capture(clangxx, "--version", v, 60000);
    size_t nl = v.find_first_of("\r\n");
    version = nl == std::string::npos ? v : v.substr(0, nl);
    logf("tools: %s | %s", clangxx.c_str(), version.c_str());
    return !version.empty();
}
int clang_major(const std::string &v)
{
    size_t p = v.find("version ");
    return p == std::string::npos ? -1 : atoi(v.c_str() + p + 8);
}

DWORD WINAPI build_thread(void *)
{
    const Job &j = g_job;
    DWORD t0 = GetTickCount();
    auto secs = [&] { return (GetTickCount() - t0) / 1000.0; };
    std::string kit = builder_kit(j.folder.c_str());
    std::string target = exe_dir() + "\\" + j.folder;
    std::string work = target + "\\build";
    SHCreateDirectoryExA(NULL, work.c_str(), NULL);
    logf("Building %s (%s) from %s", j.game.c_str(), j.exe.c_str(), j.g.path.c_str());
    // a folder with letters past plain ASCII (é, ü, a user name in another alphabet): the recompiler reads its settings
    // file as UTF-8 and this program writes the Windows code page, so the paths it is given are the short (8.3) ones
    auto ascii_path = [](std::string &p) {
        bool plain = true;
        for (unsigned char c : p) if (c >= 0x80) plain = false;
        if (plain) return;
        char sh[MAX_PATH];
        DWORD n = GetShortPathNameA(p.c_str(), sh, MAX_PATH);
        if (n && n < MAX_PATH) { logf("path with special letters: %s used as %s", p.c_str(), sh); p = sh; }
        else logf("path with special letters and no short name: %s", p.c_str());
    };
    ascii_path(work);
    ascii_path(kit);

    // ---- the kit
    std::string kitText;
    if (kit.empty() || !read_kit(kit, kitText)) return fail("This copy of Athelas is missing the files it needs to build the game. Download Athelas again.");
    json::JParse jp{ kitText.data(), kitText.data() + kitText.size() };
    json::JVal k = jp.value();
    if (jp.bad || k.t != json::JVal::OBJ) return fail("This copy of Athelas has a damaged build kit. Download Athelas again.");
    auto list = [&](const char *key) {
        std::vector<std::string> r;
        if (const json::JVal *v = k.get(key)) for (auto &e : v->a) if (e.t == json::JVal::STR) r.push_back(e.s);
        return r;
    };

    // ---- 1. the build tools
    set_phase(BP_TOOLS);
    std::string clangxx, env, version;
    if (!find_tools(k.str("arch"), clangxx, env, version))
        return fail("This copy of Athelas is missing its build tools (the tools folder). Download Athelas again.");
    if (clang_major(version) != clang_major(k.str("clang")))
        return fail("This copy of Athelas has build tools from another version. Download Athelas again.", version.c_str());
    if (version != k.str("clang")) logf("note: the kit was made with %s", k.str("clang").c_str());
    if (cancelled()) return stopped();

    // ---- 2. the player's game code, recompiled (kept for the next try when it was already done for this kit and game)
    set_phase(BP_READ);
    std::string xbe;
    if (!read_xbe(j.g, xbe)) return fail("Athelas couldn't read the game's default.xbe from " + j.g.path + ". Check the game is still there.");
    std::string stamp = stamp_text(kitText, xbe);
    std::string gen = work + "\\generated", objDir = work + "\\obj", cfgDir = work + "\\config";
    std::string have;
    if (!read_file(work + "\\generated.stamp", have) || have != stamp) {
        remove_tree(gen); remove_tree(objDir); remove_tree(cfgDir);
        SHCreateDirectoryExA(NULL, gen.c_str(), NULL);
        SHCreateDirectoryExA(NULL, cfgDir.c_str(), NULL);
        if (!write_file(work + "\\default.xbe", xbe)) return fail("Athelas couldn't write to its folder " + work + ". Is the drive full?");
        for (auto &f : list_files(kit + "\\config", "*.toml")) {
            std::string c;
            read_file(kit + "\\config\\" + f, c);
            if (f == k.str("config")) {   // pointed at the player's file and our folder
                std::string r; size_t st = 0;
                while (st < c.size()) {
                    size_t e = c.find('\n', st); if (e == std::string::npos) e = c.size();
                    std::string l = c.substr(st, e - st);
                    std::string t = l; size_t sp = t.find_first_not_of(" \t"); t = sp == std::string::npos ? "" : t.substr(sp);
                    auto key = [&](const char *kk) { size_t n = strlen(kk); return t.compare(0, n, kk) == 0 && t.find_first_not_of(" \t", n) != std::string::npos && t[t.find_first_not_of(" \t", n)] == '='; };
                    if (key("path")) l = "path = \"" + fwd(work + "\\default.xbe") + "\"";
                    else if (key("out")) l = "out = \"" + fwd(gen) + "\"";
                    r += l; if (e < c.size()) r += "\n";
                    st = e + 1;
                }
                c = r;
            }
            write_file(cfgDir + "\\" + f, c);
        }
        HANDLE p = launch(kit + "\\bin\\xbe_recomp.exe", q(cfgDir + "\\" + k.str("config")) + " --xdk " + q(kit + "\\xdk"), work, env, work + "\\recomp.log");
        if (!p) return fail("Athelas couldn't start its recompiler. Your antivirus may have blocked it.");
        int rc = wait_exit(p);
        log_file_tail(work + "\\recomp.log", 4);
        if (rc != 0) {
            // the recompiler's last line says why: shown, not a guess about the release (the region was checked before)
            std::string why, s;
            if (read_file(work + "\\recomp.log", s)) {
                size_t e = s.find_last_not_of("\r\n");
                if (e != std::string::npos) { size_t b = s.find_last_of("\n", e); why = s.substr(b == std::string::npos ? 0 : b + 1, e - (b == std::string::npos ? 0 : b + 1) + 1); }
            }
            char code[32]; snprintf(code, sizeof code, "0x%08X", (unsigned)rc);
            return fail("Athelas's recompiler stopped (" + std::string(code) + (why.empty() ? "" : ": " + why) +
                        "). Try moving the Athelas folder to a short plain path such as C:\\Athelas, and check your antivirus didn't block it.",
                        ("recompiler exit " + std::to_string(rc)).c_str());
        }
        DeleteFileA((work + "\\default.xbe").c_str());
        write_file(work + "\\generated.stamp", stamp);
    } else logf("recompiled code from an earlier try kept");
    if (cancelled()) return stopped();

    // ---- 3. compile, the biggest files first, most of the threads at a time
    std::vector<std::pair<unsigned long long, std::string>> srcs;
    {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((gen + "\\*.cpp").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do srcs.push_back({ ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, fd.cFileName }); while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    if (srcs.empty()) return fail("Athelas couldn't read this copy of the game. Is it the US release?", "no generated code");
    std::sort(srcs.begin(), srcs.end(), [](auto &a, auto &b) { return a.first > b.first; });
    SHCreateDirectoryExA(NULL, objDir.c_str(), NULL);
    std::vector<std::string> inc;
    if (const json::JVal *v = k.get("includes"))
        for (auto &e : v->a) {
            std::string kind = e.str("kind");
            std::string p = fwd(kind == "gen" ? gen : kit + "\\include\\" + kind);   // (forward: clang reads \\ in a response file as an escape)
            const json::JVal *sys = e.get("sys");
            if (sys && sys->b) { inc.push_back("-isystem"); inc.push_back(p); } else inc.push_back("-I" + p);
        }
    std::vector<std::string> defines = list("defines"), flags = list("flags"), flagsMap = list("flags_map");
    // "@KIT@" in a flag: the kit's folder (the profile-guided build's -fprofile-instr-use=@KIT@/pgo/<exe>.profdata)
    for (auto *fl : { &flags, &flagsMap })
        for (auto &a : *fl) {
            size_t at = a.find("@KIT@");
            if (at != std::string::npos) a.replace(at, 5, fwd(kit));
        }
    int cpu = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    MEMORYSTATUSEX ms = { sizeof ms }; GlobalMemoryStatusEx(&ms);
    double freeGB = ms.ullAvailPhys / 1073741824.0;
    int jobs = std::max((cpu * 3 + 3) / 4, std::min(cpu, (int)((freeGB - 2) / 0.5)));
    jobs = std::max(1, std::min(std::min(jobs, cpu), 64));   // (64: what one wait can watch)
    logf("%d threads, %.1f GB free: %d compiles at a time; %d files", cpu, freeGB, jobs, (int)srcs.size());
    std::string doneList;
    read_file(objDir + "\\done.txt", doneList);
    lock(); g_st.phase = BP_COMPILE; g_st.total = (int)srcs.size(); g_st.done = 0; unlock();
    struct Run { HANDLE p; std::string name; };
    std::vector<Run> running;
    size_t next = 0; int done = 0, failed = 0; DWORD tc = GetTickCount(); int doneAtStart = 0;
    std::string firstFail;
    while ((next < srcs.size() || !running.empty())) {
        while (!cancelled() && !failed && (int)running.size() < jobs && next < srcs.size()) {
            const std::string &n = srcs[next++].second;
            std::string obj = objDir + "\\" + n + ".obj";
            if (doneList.find("\n" + n + "\n") != std::string::npos && exists(obj)) { done++; doneAtStart++; continue; }
            std::string rsp;
            for (auto &a : defines) rsp += q(a) + "\n";
            for (auto &a : inc) rsp += q(a) + "\n";
            for (auto &a : (n == "x86_func_mapping.cpp" ? flagsMap : flags)) rsp += q(a) + "\n";
            rsp += "-o\n" + q(fwd(obj)) + "\n-c\n" + q(fwd(gen + "\\" + n)) + "\n";
            write_file(objDir + "\\" + n + ".rsp", rsp);
            HANDLE p = launch(clangxx, q("@" + objDir + "\\" + n + ".rsp"), work, env, objDir + "\\" + n + ".err");
            if (!p) { failed++; firstFail = n; break; }
            running.push_back({ p, n });
        }
        if (cancelled()) {
            for (auto &r : running) { TerminateProcess(r.p, 1); wait_exit(r.p); DeleteFileA((objDir + "\\" + r.name + ".obj").c_str()); }
            write_file(objDir + "\\done.txt", doneList);
            logf("cancelled at %d of %d files", done, (int)srcs.size());
            set_phase(BP_CANCELLED);
            return 2;
        }
        if (running.empty()) { if (failed || next >= srcs.size()) break; continue; }
        std::vector<HANDLE> hs;
        for (auto &r : running) hs.push_back(r.p);
        DWORD w = WaitForMultipleObjects((DWORD)hs.size(), hs.data(), FALSE, 250);
        if (w >= WAIT_OBJECT_0 && w < WAIT_OBJECT_0 + hs.size()) {
            Run r = running[w - WAIT_OBJECT_0];
            running.erase(running.begin() + (w - WAIT_OBJECT_0));
            int rc = wait_exit(r.p);
            if (rc != 0 || !exists(objDir + "\\" + r.name + ".obj")) { failed++; if (firstFail.empty()) firstFail = r.name; }
            else { done++; doneList += "\n" + r.name + "\n"; }
        }
        ms.dwLength = sizeof ms; GlobalMemoryStatusEx(&ms);
        lock();
        g_st.done = done;
        g_st.seconds = secs();
        int fresh = done - doneAtStart;
        if (fresh >= 3) g_st.left = (GetTickCount() - tc) / 1000.0 / fresh * (srcs.size() - done) + 5;
        unlock();
        if (failed && running.empty()) break;
    }
    write_file(objDir + "\\done.txt", doneList);
    if (failed) {
        logf("%s didn't compile:", firstFail.c_str());
        log_file_tail(objDir + "\\" + firstFail + ".err", 15);
        return fail("Building the game stopped partway (the build tools reported a problem). Make a report from the About tab so it can be fixed.",
                    firstFail.c_str());
    }
    logf("compiled in %.0f s", secs());

    // ---- 4. link: the recompiled code, our own code and libraries from the kit, the Windows / C runtime libraries
    set_phase(BP_LINK);
    std::string outName = out_name(j.folder.c_str(), j.exe.c_str());
    std::string tmpExe = work + "\\" + outName;
    std::string rsp;
    for (auto &a : list("link_flags")) rsp += q(a) + "\n";
    rsp += "-o\n" + q(fwd(tmpExe)) + "\n";
    for (auto &s : srcs) rsp += q(fwd(objDir + "\\" + s.second + ".obj")) + "\n";
    for (auto &o : list("objects")) rsp += q(fwd(kit + "\\" + o)) + "\n";
    for (auto &l : list("libraries")) rsp += q(fwd(kit + "\\" + l)) + "\n";
    for (auto &l : list("system_libraries")) rsp += "-l" + l + "\n";
    write_file(work + "\\link.rsp", rsp);
    DeleteFileA(tmpExe.c_str());
    HANDLE p = launch(clangxx, q("@" + work + "\\link.rsp"), work, env, work + "\\link.log");
    if (!p) return fail("Athelas couldn't start the build tools' linker.");
    int rc = wait_exit(p);
    if (rc != 0 || !exists(tmpExe)) {
        log_file_tail(work + "\\link.log", 15);
        return fail("Building the game stopped at the last step. Make a report from the About tab so it can be fixed.", "link");
    }

    // ---- 5. in place, with its stamp; the working files go
    std::string final_ = target + "\\" + outName;
    if (!MoveFileExA(tmpExe.c_str(), final_.c_str(), MOVEFILE_REPLACE_EXISTING))
        return fail("Athelas couldn't put the new " + j.exe + " in place. Close the game if it is running, then try again.");
    write_file(final_ + ".build", stamp);
    remove_tree(work);
    // what an earlier release shipped and nothing uses now: its programs' checksum files, and an ARM64 program
    // that wasn't built here (no .build stamp; the x64 name is the one the launcher looks for, so it stays). Only after a build that worked.
    {
        std::string base = j.exe.size() > 4 ? j.exe.substr(0, j.exe.size() - 4) : j.exe;
        const std::string names[] = { base + ".exe", base + "-arm64.exe" };
        for (const std::string &n : names) {
            std::string f = target + "\\" + n;
            if (exists(f + ".sha256") && DeleteFileA((f + ".sha256").c_str())) logf("removed %s.sha256 (an earlier release's)", n.c_str());
            if (n != outName && n != base + ".exe" && exists(f) && !exists(f + ".build") && DeleteFileA(f.c_str())) logf("removed %s (an earlier release's)", n.c_str());
        }
    }
    logf("built %s in %.0f s", final_.c_str(), secs());
    lock(); g_st.phase = BP_DONE; g_st.seconds = secs(); g_st.left = 0; unlock();
    return 0;
}

DWORD WINAPI build_main(void *)
{
    DWORD r = build_thread(nullptr);
    if (g_log) { fclose(g_log); g_log = nullptr; }
    InterlockedExchange(&g_busy, 0);
    return r;
}

}   // namespace

// A Windows on ARM PC builds the ARM64 program from kits\<folder>-arm64 (the launcher is x86: it asks for the machine)
static bool on_arm()
{
    typedef BOOL(WINAPI * Wow64Process2)(HANDLE, USHORT *, USHORT *);
    Wow64Process2 f = (Wow64Process2)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process2");
    USHORT process = 0, machine = 0;
    return f && f(GetCurrentProcess(), &process, &machine) && machine == 0xAA64;
}
static bool arm_kit(const char *folder) { return on_arm() && exists(exe_dir() + "\\kits\\" + folder + "-arm64\\kit.json"); }
std::string builder_kit(const char *folder)
{
    std::string k = exe_dir() + "\\kits\\" + folder + (arm_kit(folder) ? "-arm64" : "");
    return exists(k + "\\kit.json") ? k : std::string();
}
std::string out_name(const char *folder, const char *exe)
{
    std::string e = exe;
    if (arm_kit(folder) && e.size() > 4) e = e.substr(0, e.size() - 4) + "-arm64.exe";
    return e;
}

bool builder_needed(const char *folder, const char *exe, const LibGame &g)
{
    std::string kit = builder_kit(folder);
    if (kit.empty()) return false;   // no kit: a program shipped with the release (or a developer's build) is used as it is
    std::string final_ = exe_dir() + "\\" + folder + "\\" + out_name(folder, exe);
    if (!exists(final_)) return true;
    std::string have, kitText, xbe;
    if (!read_file(final_ + ".build", have)) return true;
    if (!read_kit(kit, kitText) || !read_xbe(g, xbe)) return false;   // can't tell: keep what's there
    return have != stamp_text(kitText, xbe);
}

bool builder_start(const char *folder, const char *exe, const char *game, const LibGame &g, const std::string &logPath)
{
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) return false;
    if (g_thread) { WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); g_thread = NULL; }
    InterlockedExchange(&g_cancel, 0);
    g_job = Job{ folder, exe, game, logPath, g };
    lock();
    g_st = BuildStatus();
    g_st.phase = BP_TOOLS; g_st.game = game; g_st.folder = folder; g_st.log = logPath;
    unlock();
    size_t sl = logPath.find_last_of('\\');
    if (sl != std::string::npos) SHCreateDirectoryExA(NULL, logPath.substr(0, sl).c_str(), NULL);
    g_log = fopen(logPath.c_str(), "w");
    g_thread = CreateThread(NULL, 0, build_main, NULL, 0, NULL);
    if (!g_thread) { InterlockedExchange(&g_busy, 0); lock(); g_st.phase = BP_FAILED; g_st.error = "Athelas couldn't start building."; unlock(); return false; }
    return true;
}

void builder_cancel() { InterlockedExchange(&g_cancel, 1); }
bool builder_busy() { return g_busy != 0; }
BuildStatus builder_status() { lock(); BuildStatus s = g_st; unlock(); return s; }
void builder_clear() { if (builder_busy()) return; lock(); g_st = BuildStatus(); unlock(); }

// 1.7.5: the Xbox games ship ready to play again, so 1.7.3/1.7.4's build kits, build tools and each game's build
// folder (recompiled code, objects: hundreds of MB) are left over. Removed once every game they were for has its
// program; anything else in the folder is left alone.
void builder_cleanup_old()
{
    std::string root = exe_dir();
    if (!exists(root + "\\kits") && !exists(root + "\\tools\\llvm-mingw")) return;
    static const char *const ports[][2] = { { "TT-Xbox", "TwoTowers" }, { "ROTK-Xbox", "ROTKXbox" },
                                            { "FOTR-Xbox", "FOTRXbox" }, { "TA-Xbox", "TAXbox" } };
    for (auto &p : ports)
        if ((exists(root + "\\kits\\" + p[0]) || exists(root + "\\kits\\" + p[0] + "-arm64")) &&
            !exists(root + "\\" + p[0] + "\\" + p[1] + ".exe")) return;  // still needed
    for (auto &p : ports) {
        // the stamps of programs built here from a kit (review: an ARM64 PC kept preferring a 1.7.4 build with one)
        std::string e = root + "\\" + p[0] + "\\" + p[1];
        DeleteFileA((e + ".exe.build").c_str());
        DeleteFileA((e + "-arm64.exe.build").c_str());
    }
    for (auto &p : ports) {
        std::string b = root + "\\" + p[0] + "\\build";
        if (exists(b + "\\generated") || exists(b + "\\obj") || exists(b + "\\generated.stamp")) remove_tree(b);
    }
    remove_tree(root + "\\kits");
    remove_tree(root + "\\tools\\llvm-mingw");
    RemoveDirectoryA((root + "\\tools").c_str());  // only when nothing else is in it
}

// The game's default.xbe as SHA-1 hex ("" if unreadable): how an untested release the player says works is remembered
// and reported (GitHub)
#include <bcrypt.h>
std::string xbe_sha1(const LibGame &g)
{
    std::string xbe;
    if (!read_xbe(g, xbe)) return "";
    BCRYPT_ALG_HANDLE alg = NULL; BCRYPT_HASH_HANDLE h = NULL; unsigned char d[20]; std::string r;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, NULL, 0) != 0) return "";
    if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0 && BCryptHashData(h, (PUCHAR)xbe.data(), (ULONG)xbe.size(), 0) == 0 &&
        BCryptFinishHash(h, d, 20, 0) == 0) {
        static const char *x = "0123456789abcdef";
        for (unsigned char c : d) { r += x[c >> 4]; r += x[c & 15]; }
    }
    if (h) BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    return r;
}
