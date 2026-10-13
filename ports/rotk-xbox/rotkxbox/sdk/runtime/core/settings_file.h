#pragma once
// The settings file as a launcher can edit it: plain [section] and key = value lines, one setting a line, kept to
// bools, whole numbers and quoted strings. Each setting is described once (Desc), and the same list reads the file,
// writes it back with any keys it does not know left in place, and describes the settings as JSON for a launcher
// that builds its options page from them (--describe-settings).
#include <cstdint>
#include <string>
#include <vector>

namespace cfg {

enum class Type { Bool, Int, Float, Choice, String };

struct Desc {
    const char* section;
    const char* key;
    Type type;
    const char* label;  // a launcher's caption
    const char* help;   // one line
    // where the value lives: Bool in flag; Int in value; Float in real; Choice in flag (choices[0] false, [1] true) or in value (a
    // list of numbers stores the number, "16" -> 16, a list of words the place in it) or in text (the word itself,
    // any word kept: a list that changes from PC to PC, like its screens); String in text
    bool* flag = nullptr;
    uint32_t* value = nullptr;
    std::string* text = nullptr;
    int64_t min = 0, max = 0;           // Int; Float takes them in hundredths (-100 is -1.0)
    std::vector<const char*> choices;   // Choice
    const char* old_key = nullptr;      // the top-level key an older settings file used, read once and dropped
    float* real = nullptr;              // Float
    // Choice: how a launcher shows each choice ("Off", "2x", "60 fps"), in the choices' order; empty shows them as
    // written. The file always stores the choice itself.
    std::vector<const char*> labels;
};

// the setting's current value as the file writes it: true, 60, "30", "borderless"
std::string ValueText(const Desc& d);

// Read: false only for a file that exists but is not TOML (error says why); a missing file leaves the defaults.
// Values out of range or not among the choices are ignored.
// old_layout, when given, is set for a file that still uses an older top-level key (worth writing again).
bool Read(const std::string& path, const std::vector<Desc>& list, std::string& error, bool* old_layout = nullptr);
// the file as Write would make it, keeping what other (unknown) keys `previous` holds
std::string Text(const std::vector<Desc>& list, const std::string& previous = {});
bool Write(const std::string& path, const std::vector<Desc>& list);
// a launcher's preset: "section.key" to the value as JSON writes it ("\"16\"", "true", "-0.50"); keys it leaves
// out are the player's own
struct Preset {
    const char* name;
    std::vector<std::pair<const char*, const char*>> values;
};
// {"settings":[{section,key,type,default,label,help,min,max | choices,choice_labels}], "presets":{name:{key:value}}} with the
// current values as defaults
std::string Describe(const std::vector<Desc>& list, const std::vector<Preset>& presets = {});

}  // namespace cfg
