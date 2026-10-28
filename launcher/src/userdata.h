// Athelas - the player's own files: save backups and restores, the Xbox ports' shader caches, one log folder per play
// (as COGBox does it, Oct 2026). Paths are Windows (ANSI) paths, like the rest of the launcher.
#pragma once
#include <string>
#include <vector>

// Zip every file under dir (its sub-folders kept) into zipPath, then open the zip again and check each file is in it
// with its size. False (and the half-made zip deleted) on any problem; files = how many went in.
bool backup_folder(const std::string &dir, const std::string &zipPath, int &files, std::string &err, const std::string &game);
// The newest backup names: "<desktop>\Athelas saves - <game> - 2026-10-28 09-41.zip"
std::string backup_name(const std::string &game);
std::string desktop_dir();
// Put a backup back: first the current saves are zipped beside the backup ("... - before restore ..."), then dir's
// files are moved aside to dir + ".old-<time>" and the zip is unpacked into dir. Only plain relative paths in the zip
// are used (no "..", no drive letters). On failure the old files are put back.
bool restore_folder(const std::string &zipPath, const std::string &dir, const std::string &game, std::string &before,
                    std::string &err);
bool folder_has_files(const std::string &dir);

// A shader cache set aside: renamed to dir + ".cleared-<time>" (not deleted). Older set-aside copies beyond the newest
// two are removed.
bool shaders_set_aside(const std::string &dir, std::string &movedTo, std::string &err);

// One folder per play under logsDir: "2026-10-28 09-41-07 <what>", holding session.txt (start, end, exit code and its
// meaning, the command line) and copies of the log and of any crash dump newer than the start. Only the newest
// `keep` folders are kept.
struct Session {
    std::string game, exe, args, log, dumpDir;
    unsigned long long startedMs = 0;   // GetTickCount64 when it started
    long long startedUnix = 0;
};
std::string session_save(const std::string &logsDir, const Session &s, unsigned long exitCode, const std::string &meaning,
                         int keep = 15);
std::string session_newest(const std::string &logsDir);   // the newest session folder, or ""

// Mods for the PC game (ROTK Fix's mods folder). A mod's zip holds either a data mod - files laid out like the
// game's own, under a "Data" folder at any depth (Data\Game\..., Data\Textures\...) - or a code mod, .dll files
// with their .ini. A data mod goes to modsDir\<name>\ (its Data folder and a mod.ini, if the zip has one); a code
// mod's .dll/.ini files to modsDir itself. Only plain relative paths are taken from the zip.
struct ModZip { bool data = false, code = false; std::vector<std::string> dlls; std::string error; };
ModZip mod_zip_inspect(const std::string &zip);
bool mod_zip_install(const std::string &zip, const std::string &modsDir, const std::string &name, bool code,
                     int &files, std::string &err);
