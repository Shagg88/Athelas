// ROTK Fix Launcher - starting a game's PC port (a separate program, such as
// The Two Towers' TwoTowers.exe) and editing its settings. The interface is the
// one in TT-PORT-INTERFACE.md: --version, --describe-settings (JSON), a plain
// [section] / key = value settings file, and exit codes.
#pragma once
#include <string>
#include <vector>
#include <windows.h>

struct PortSetting {
    std::string section, key, type;    // type: bool, int, float, choice, string
    std::string label, help;
    std::string def;                   // default, written as it goes in the file (choices and strings quoted)
    double min = 0, max = 0;
    std::vector<std::string> choices;
    std::vector<std::string> choiceLabels;   // optional display names, one per choice ("0" -> "Off")
};

struct PortSettingsFile {
    std::vector<std::string> lines;    // the file as read, so unknown keys and comments are kept
    std::string get(const std::string &sec, const std::string &key, const std::string &def) const;
    void set(const std::string &sec, const std::string &key, const std::string &value);
    void remove(const std::string &sec, const std::string &key);   // drops the key's line, if there is one
};

struct Port {
    std::string exe;                   // full path to the port program, "" if not found
    std::string version;               // from --version
    std::vector<PortSetting> settings; // from --describe-settings
    struct Preset { std::string name; std::vector<std::pair<std::string, std::string>> values; };   // "section.key" -> value as written in the file
    std::vector<Preset> presets;       // optional "presets" in the list, in its order
    std::string err;                   // why it couldn't be read
};

// How a run ended. The status is kept apart from the exit code, so any exit code (0xFFFFFFFF too) is just a code.
enum RunStatus { RUN_EXITED, RUN_NOT_STARTED, RUN_TIMED_OUT };
struct RunResult {
    RunStatus status = RUN_EXITED;
    DWORD code = 0;   // the exit code, when status is RUN_EXITED
    bool ok() const { return status == RUN_EXITED && code == 0; }
};
// Runs 'exe args', capturing stdout, waiting at most timeoutMs. A program still running after timeoutMs is stopped.
RunResult run_capture(const std::string &exe, const std::string &args, std::string &out, DWORD timeoutMs);
// How a run ended in plain words: "timed out", "exit 3", "exit 0xC0000135: a DLL it needs is missing"
std::string exit_code_text(const RunResult &rc);
// Fills version and settings from the program. logPath: when not "", what was run, how it ended and everything
// the program printed is written there (so a problem report shows why a port didn't answer).
bool port_query(Port &p, const std::string &logPath = std::string());

bool settings_file_load(const std::string &path, PortSettingsFile &f);
bool settings_file_save(const std::string &path, const PortSettingsFile &f);
std::string toml_unquote(const std::string &v);
std::string toml_quote(const std::string &s);

// Starts the port; the returned process handle is waited on by the caller.
HANDLE port_start(const std::string &exe, const std::string &args, std::string &err);
// The reason in the log's last "Exit (N): ..." line, or "".
std::string port_log_reason(const std::string &logPath);
