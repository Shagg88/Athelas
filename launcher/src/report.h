// Athelas - a problem report: the logs, settings and a description of the PC in one zip, made on the player's
// computer and never sent anywhere by Athelas. The player attaches it to the GitHub issue themselves.
#pragma once
#include <string>
#include <vector>

struct ReportFile {
    std::string name;   // its name in the zip ("TT-Xbox/tt.log")
    std::string path;   // where it is
    bool text = true;   // text files have the Windows user folder taken out
};

// The PC: Windows version, processor, memory, graphics adapters, displays (one "Name: value" line each)
std::string report_system_text();
// The Windows user folder (C:\Users\Name) and the user name, replaced in text so a report doesn't show who made it
std::string report_anonymise(const std::string &text);
// The newest .dmp in a folder written in the last 'days' days, "" if none
std::string report_newest_dump(const std::string &dir, int days);
// Writes the zip: the files that exist, plus "About this PC.txt" with 'system'. zipPath is where it went.
bool report_write(const std::vector<ReportFile> &files, const std::string &system, std::string &zipPath, std::string &err);
