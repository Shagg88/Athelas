// Athelas - save backups and restores, shader caches set aside, one log folder per play. See userdata.h.
#include "userdata.h"
#include "../third_party/miniz/miniz.h"

#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <ctime>
#include <cstdio>

static std::string now_stamp(bool seconds)
{
    SYSTEMTIME t; GetLocalTime(&t);
    char b[40];
    if (seconds) snprintf(b, sizeof b, "%04d-%02d-%02d %02d-%02d-%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    else snprintf(b, sizeof b, "%04d-%02d-%02d %02d-%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    return b;
}

static void list_files(const std::string &root, const std::string &rel, std::vector<std::string> &out)
{
    WIN32_FIND_DATAA fd;
    std::string base = rel.empty() ? root : root + "\\" + rel;
    HANDLE h = FindFirstFileA((base + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        std::string r = rel.empty() ? fd.cFileName : rel + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) list_files(root, r, out);
        else out.push_back(r);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

bool folder_has_files(const std::string &dir)
{
    std::vector<std::string> v;
    list_files(dir, "", v);
    return !v.empty();
}

std::string desktop_dir()
{
    char d[MAX_PATH] = "";
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, d)) && d[0]) return d;
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, d)) && d[0]) return d;
    return "";
}

static std::string safe_name(std::string s)
{
    for (char &c : s) if (strchr("\\/:*?\"<>|", c)) c = '-';
    return s;
}

std::string backup_name(const std::string &game)
{
    return desktop_dir() + "\\Athelas saves - " + safe_name(game) + " - " + now_stamp(true) + ".zip";
}

static long long file_size(const std::string &p)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &a)) return -1;
    return ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

static const char *kMarker = "athelas-backup.txt";   // in every backup: which game's saves it holds
bool backup_folder(const std::string &dir, const std::string &zipPath, int &files, std::string &err, const std::string &game)
{
    files = 0;
    std::vector<std::string> list;
    list_files(dir, "", list);
    if (list.empty()) { err = "There are no saves in " + dir + " yet."; return false; }
    if (GetFileAttributesA(zipPath.c_str()) != INVALID_FILE_ATTRIBUTES) { err = zipPath + " is already there."; return false; }
    // written beside it under another name, and given its own name only once it has been read back (an earlier
    // backup is never overwritten or deleted)
    const std::string final_ = zipPath, tmp = zipPath + ".part";
    DeleteFileA(tmp.c_str());
    mz_zip_archive z; memset(&z, 0, sizeof z);
    if (!mz_zip_writer_init_file(&z, tmp.c_str(), 0)) { err = "The backup couldn't be made at " + zipPath; return false; }
    std::string mark = "Athelas saves backup\r\nGame: " + game + "\r\n";
    bool ok = mz_zip_writer_add_mem(&z, kMarker, mark.data(), mark.size(), MZ_DEFAULT_COMPRESSION) != 0;
    for (const std::string &r : list) {
        std::string name = r;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (!mz_zip_writer_add_file(&z, name.c_str(), (dir + "\\" + r).c_str(), nullptr, 0, MZ_DEFAULT_COMPRESSION)) {
            err = "Couldn't read " + r + " (is the game still running?)";
            ok = false;
            break;
        }
    }
    ok = mz_zip_writer_finalize_archive(&z) != 0 && ok;
    mz_zip_writer_end(&z);
    if (ok) {   // read it back: every file there, with its size
        mz_zip_archive rz; memset(&rz, 0, sizeof rz);
        if (!mz_zip_reader_init_file(&rz, tmp.c_str(), 0)) ok = false;
        else {
            for (const std::string &r : list) {
                std::string name = r;
                std::replace(name.begin(), name.end(), '\\', '/');
                int i = mz_zip_reader_locate_file(&rz, name.c_str(), nullptr, 0);
                mz_zip_archive_file_stat st;
                if (i < 0 || !mz_zip_reader_file_stat(&rz, (mz_uint)i, &st) || (long long)st.m_uncomp_size != file_size(dir + "\\" + r)) {
                    ok = false;
                    err = "The backup didn't check out (" + r + ")";
                    break;
                }
            }
            mz_zip_reader_end(&rz);
        }
        if (!ok && err.empty()) err = "The backup couldn't be read back";
    }
    if (ok && !MoveFileA(tmp.c_str(), final_.c_str())) { ok = false; err = "The backup couldn't be given its name"; }
    if (!ok) { DeleteFileA(tmp.c_str()); if (err.empty()) err = "The backup couldn't be written"; return false; }
    files = (int)list.size();
    return true;
}

static bool safe_rel(const std::string &n)
{
    if (n.empty() || n[0] == '/' || n[0] == '\\' || n.find(':') != std::string::npos) return false;
    size_t at = 0;
    while (at <= n.size()) {
        size_t e = n.find_first_of("/\\", at);
        std::string part = n.substr(at, e == std::string::npos ? std::string::npos : e - at);
        if (part == "..") return false;
        if (e == std::string::npos) break;
        at = e + 1;
    }
    return true;
}

bool restore_folder(const std::string &zipPath, const std::string &dir, const std::string &game, std::string &before,
                    std::string &err)
{
    before.clear();
    mz_zip_archive z; memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, zipPath.c_str(), 0)) { err = "That file isn't a zip Athelas can read."; return false; }
    mz_uint n = mz_zip_reader_get_num_files(&z);
    int usable = 0;
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) continue;
        if (!safe_rel(st.m_filename)) { mz_zip_reader_end(&z); err = "The zip has a file outside its own folders (" + std::string(st.m_filename) + "): not used."; return false; }
        if (!mz_zip_reader_is_file_a_directory(&z, i)) usable++;
    }
    if (!usable) { mz_zip_reader_end(&z); err = "The zip is empty."; return false; }
    {   // only a backup Athelas made of this game
        size_t ml = 0;
        void *m = mz_zip_reader_extract_file_to_heap(&z, kMarker, &ml, 0);
        std::string mark = m ? std::string((const char *)m, ml) : std::string();
        if (m) mz_free(m);
        if (mark.empty()) { mz_zip_reader_end(&z); err = "That zip isn't a saves backup Athelas made."; return false; }
        if (mark.find("Game: " + game + "\r\n") == std::string::npos) {
            size_t g = mark.find("Game: ");
            std::string other = g == std::string::npos ? "another game" : mark.substr(g + 6, mark.find('\r', g) - g - 6);
            mz_zip_reader_end(&z);
            err = "That backup holds the saves of " + other + ", not " + game + ".";
            return false;
        }
        if (--usable <= 0) { mz_zip_reader_end(&z); err = "The backup has no saves in it."; return false; }
    }
    // 1. the saves there now, zipped beside the backup
    if (folder_has_files(dir)) {
        int k = 0;
        std::string b = desktop_dir() + "\\Athelas saves - " + safe_name(game) + " - before restore " + now_stamp(true) + ".zip";
        if (!backup_folder(dir, b, k, err, game)) { mz_zip_reader_end(&z); err = "The current saves couldn't be backed up first, so nothing was changed: " + err; return false; }
        before = b;
    }
    // 2. the folder set aside, 3. the backup unpacked
    std::string aside = dir + ".old-" + now_stamp(true);
    bool moved = GetFileAttributesA(dir.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (moved && !MoveFileA(dir.c_str(), aside.c_str())) {
        mz_zip_reader_end(&z);
        err = "The saves folder couldn't be set aside (is the game still running?). Nothing was changed.";
        return false;
    }
    SHCreateDirectoryExA(NULL, dir.c_str(), NULL);
    bool ok = true;
    for (mz_uint i = 0; i < n && ok; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) { ok = false; break; }
        std::string rel = st.m_filename;
        std::replace(rel.begin(), rel.end(), '/', '\\');
        std::string to = dir + "\\" + rel;
        if (mz_zip_reader_is_file_a_directory(&z, i)) { SHCreateDirectoryExA(NULL, to.c_str(), NULL); continue; }
        if (!_stricmp(st.m_filename, kMarker)) continue;
        size_t sl = to.find_last_of('\\');
        SHCreateDirectoryExA(NULL, to.substr(0, sl).c_str(), NULL);
        if (!mz_zip_reader_extract_to_file(&z, i, to.c_str(), 0)) { ok = false; err = "Couldn't write " + rel; }
    }
    mz_zip_reader_end(&z);
    if (!ok) {   // put the old saves back
        std::string bad = dir + ".failed-" + now_stamp(true);
        bool back = MoveFileA(dir.c_str(), bad.c_str()) && (!moved || MoveFileA(aside.c_str(), dir.c_str()));
        if (back) err += ". Your saves were left as they were.";
        else err += ". Your saves couldn't be put back by themselves: they are in " + (moved ? aside : std::string("(none)")) +
                    (before.empty() ? std::string("") : " and in " + before) + ".";
        return false;
    }
    if (moved) {   // the set-aside copy isn't needed: the "before restore" zip has it
        std::string from = aside; from.push_back('\0');
        SHFILEOPSTRUCTA op; memset(&op, 0, sizeof op);
        op.wFunc = FO_DELETE; op.pFrom = from.c_str();
        op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT | FOF_ALLOWUNDO;   // to the Recycle Bin
        if (!before.empty()) SHFileOperationA(&op);
    }
    return true;
}

static void remove_tree(const std::string &d)
{
    std::string from = d; from.push_back('\0');
    SHFILEOPSTRUCTA op; memset(&op, 0, sizeof op);
    op.wFunc = FO_DELETE; op.pFrom = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationA(&op);
}

// the sibling folders named <base><tag>..., newest name last
static std::vector<std::string> siblings(const std::string &dir, const char *tag)
{
    std::vector<std::string> v;
    size_t sl = dir.find_last_of('\\');
    std::string parent = dir.substr(0, sl), base = dir.substr(sl + 1) + tag;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((parent + "\\" + base + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return v;
    do if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) v.push_back(parent + "\\" + fd.cFileName);
    while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(v.begin(), v.end());
    return v;
}

bool shaders_set_aside(const std::string &dir, std::string &movedTo, std::string &err)
{
    DWORD at = GetFileAttributesA(dir.c_str());
    if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY) || !folder_has_files(dir)) { err = "There are no saved shaders to clear."; return false; }
    movedTo = dir + ".cleared-" + now_stamp(true);
    if (!MoveFileA(dir.c_str(), movedTo.c_str())) { err = "The shader folder couldn't be renamed (is the game still running?)."; return false; }
    std::vector<std::string> old = siblings(dir, ".cleared-");
    for (size_t i = 0; i + 2 < old.size(); i++) remove_tree(old[i]);   // keep the newest two
    return true;
}

static bool copy_file(const std::string &a, const std::string &b) { return CopyFileA(a.c_str(), b.c_str(), FALSE) != 0; }

std::string session_save(const std::string &logsDir, const Session &s, unsigned long exitCode, const std::string &meaning, int keep)
{
    SHCreateDirectoryExA(NULL, logsDir.c_str(), NULL);
    unsigned long long secs = s.startedMs ? (GetTickCount64() - s.startedMs) / 1000 : 0;
    char start[40] = "";
    {   // local time now, less the time played (the same clock as "Ended")
        SYSTEMTIME now, then;
        FILETIME ft;
        GetLocalTime(&now);
        SystemTimeToFileTime(&now, &ft);
        ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
        u.QuadPart -= secs * 10000000ULL;
        ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
        FileTimeToSystemTime(&ft, &then);
        snprintf(start, sizeof start, "%04d-%02d-%02d %02d-%02d-%02d", then.wYear, then.wMonth, then.wDay, then.wHour, then.wMinute, then.wSecond);
    }
    std::string dir = logsDir + "\\" + start + " " + safe_name(s.game);
    SHCreateDirectoryExA(NULL, dir.c_str(), NULL);
    char code[16]; snprintf(code, sizeof code, "0x%08lX", exitCode);
    std::string t = "Game: " + s.game + "\r\nStarted: " + start + "\r\nEnded: " + now_stamp(true) +
                    "\r\nPlayed: " + std::to_string(secs / 60) + " min " + std::to_string(secs % 60) + " s" +
                    "\r\nExit code: " + std::to_string(exitCode) + " (" + code + ")" + (meaning.empty() ? "" : " - " + meaning) +
                    "\r\nProgram: " + s.exe + "\r\nArguments: " + s.args + "\r\n";
    if (FILE *f = fopen((dir + "\\session.txt").c_str(), "wb")) { fwrite(t.data(), 1, t.size(), f); fclose(f); }
    if (!s.log.empty()) copy_file(s.log, dir + "\\" + s.log.substr(s.log.find_last_of('\\') + 1));
    if (!s.dumpDir.empty()) {   // crash dumps written during this play
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((s.dumpDir + "\\*.dmp").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                ULARGE_INTEGER w; w.LowPart = fd.ftLastWriteTime.dwLowDateTime; w.HighPart = fd.ftLastWriteTime.dwHighDateTime;
                long long unixT = (long long)(w.QuadPart / 10000000ULL) - 11644473600LL;
                if (unixT + 2 >= s.startedUnix) copy_file(s.dumpDir + "\\" + fd.cFileName, dir + "\\" + fd.cFileName);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    // only the newest `keep`
    std::vector<std::string> all;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((logsDir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] >= '0' && fd.cFileName[0] <= '9') all.push_back(logsDir + "\\" + fd.cFileName);
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(all.begin(), all.end());
    for (size_t i = 0; i + keep < all.size(); i++) remove_tree(all[i]);
    return dir;
}

std::string session_newest(const std::string &logsDir)
{
    std::string best;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((logsDir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return best;
    do if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] >= '0' && fd.cFileName[0] <= '9' &&
           logsDir + "\\" + fd.cFileName > best) best = logsDir + "\\" + fd.cFileName;
    while (FindNextFileA(h, &fd));
    FindClose(h);
    return best;
}

// ---- mods
static std::string lower_s(std::string s) { for (char &c : s) c = (char)tolower((unsigned char)c); return s; }
// where "data/" starts in a zip path (any depth), or npos
static size_t data_at(const std::string &n)
{
    std::string l = lower_s(n);
    for (size_t i = 0; i + 5 <= l.size(); i++)
        if ((i == 0 || l[i - 1] == '/') && l.compare(i, 5, "data/") == 0) return i;
    return std::string::npos;
}

ModZip mod_zip_inspect(const std::string &zip)
{
    ModZip r;
    mz_zip_archive z; memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, zip.c_str(), 0)) { r.error = "That file isn't a zip Athelas can read."; return r; }
    mz_uint n = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) continue;
        std::string name = st.m_filename;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (!safe_rel(name)) { r.error = "The zip has a file outside its own folders (" + name + "): not used."; r.data = r.code = false; break; }
        if (mz_zip_reader_is_file_a_directory(&z, i)) continue;
        std::string l = lower_s(name);
        if (l.size() > 4 && l.compare(l.size() - 4, 4, ".dll") == 0) { r.code = true; r.dlls.push_back(name.substr(name.find_last_of('/') + 1)); }
        else if (data_at(name) != std::string::npos) r.data = true;
    }
    mz_zip_reader_end(&z);
    if (r.error.empty() && !r.data && !r.code) r.error = "This zip has no Data folder and no .dll, so it isn't a mod Athelas knows how to add.";
    return r;
}

bool mod_zip_install(const std::string &zip, const std::string &modsDir, const std::string &name, bool code, int &files, std::string &err)
{
    files = 0;
    mz_zip_archive z; memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, zip.c_str(), 0)) { err = "That file isn't a zip Athelas can read."; return false; }
    std::string dest = code ? modsDir : modsDir + "\\" + name;
    if (!code && GetFileAttributesA(dest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // the same mod again (a new version): the old one goes to the Recycle Bin first, so none of its files linger
        std::string from = dest; from.push_back('\0');
        SHFILEOPSTRUCTA op; memset(&op, 0, sizeof op);
        op.wFunc = FO_DELETE; op.pFrom = from.c_str();
        op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT | FOF_ALLOWUNDO;
        if (SHFileOperationA(&op) != 0 || op.fAnyOperationsAborted) { mz_zip_reader_end(&z); err = "The mod's earlier version couldn't be moved to the Recycle Bin (is the game running?)"; return false; }
    }
    SHCreateDirectoryExA(NULL, dest.c_str(), NULL);
    mz_uint n = mz_zip_reader_get_num_files(&z);
    bool ok = true;
    for (mz_uint i = 0; i < n && ok; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st) || mz_zip_reader_is_file_a_directory(&z, i)) continue;
        std::string zn = st.m_filename;
        std::replace(zn.begin(), zn.end(), '\\', '/');
        if (!safe_rel(zn)) continue;
        std::string base = zn.substr(zn.find_last_of('/') + 1), l = lower_s(base), rel;
        if (code) {   // the .dll files and their .ini, flat in the mods folder
            if (l.size() > 4 && (l.compare(l.size() - 4, 4, ".dll") == 0 || l.compare(l.size() - 4, 4, ".ini") == 0)) rel = base;
        } else {
            size_t d = data_at(zn);
            if (d != std::string::npos) rel = zn.substr(d);
            else if (l == "mod.ini" || l == "readme.txt") rel = base;
        }
        if (rel.empty()) continue;
        std::replace(rel.begin(), rel.end(), '/', '\\');
        std::string to = dest + "\\" + rel;
        // a code mod's .ini already there is kept: it holds the player's own settings for that mod
        if (code && l.size() > 4 && l.compare(l.size() - 4, 4, ".ini") == 0 && GetFileAttributesA(to.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        SHCreateDirectoryExA(NULL, to.substr(0, to.find_last_of('\\')).c_str(), NULL);
        if (!mz_zip_reader_extract_to_file(&z, i, to.c_str(), 0)) { ok = false; err = "Couldn't write " + rel + " (is the game running?)"; }
        else files++;
    }
    mz_zip_reader_end(&z);
    if (ok && !files) { err = "Nothing in the zip could be used."; ok = false; }
    return ok;
}
