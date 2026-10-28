#include "core/settings_file.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <toml++/toml.hpp>

namespace cfg {

namespace {

// a TOML basic string or a JSON string: both take these escapes
std::string Quote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20 || c == 0x7F) {
            char b[8];
            std::snprintf(b, sizeof(b), "\\u%04X", c);
            o += b;
        } else {
            o += char(c);
        }
    }
    return o + "\"";
}

// a choice list of numbers stores the number, one of words its place in the list
bool Numeric(const Desc& d) {
    for (const char* c : d.choices) {
        char* end;
        std::strtoll(c, &end, 10);
        if (end == c || *end) return false;
    }
    return !d.choices.empty();
}

uint32_t ChoiceValue(const Desc& d, size_t i) {
    return Numeric(d) ? uint32_t(std::strtoll(d.choices[i], nullptr, 10)) : uint32_t(i);
}

int ChoiceIndex(const Desc& d) {
    if (d.flag) return *d.flag ? 1 : 0;
    if (d.text) {
        for (size_t i = 0; i < d.choices.size(); i++)
            if (*d.text == d.choices[i]) return int(i);
        return -1;
    }
    for (size_t i = 0; i < d.choices.size(); i++)
        if (ChoiceValue(d, i) == *d.value) return int(i);
    return -1;
}

const char* TypeName(Type t) {
    switch (t) {
        case Type::Bool: return "bool";
        case Type::Int: return "int";
        case Type::Float: return "float";
        case Type::Choice: return "choice";
        default: return "string";
    }
}

// a value this file does not describe, written back as it was
std::string NodeText(const toml::node& n) {
    if (auto s = n.as_string()) return Quote(s->get());
    if (auto b = n.as_boolean()) return b->get() ? "true" : "false";
    if (auto i = n.as_integer()) return std::to_string(i->get());
    std::ostringstream o;
    o << toml::toml_formatter{n, toml::format_flags::none};
    return o.str();
}

// setting one value from the file; false leaves the default
bool Apply(const Desc& d, const toml::node& n) {
    switch (d.type) {
        case Type::Bool:
            if (auto b = n.as_boolean()) return *d.flag = b->get(), true;
            return false;
        case Type::Int:
            if (auto i = n.as_integer()) {
                int64_t v = i->get();
                if (v < d.min || v > d.max) return false;
                *d.value = uint32_t(v);
                return true;
            }
            return false;
        case Type::Float: {
            double v;
            if (auto f = n.as_floating_point()) v = f->get();
            else if (auto i = n.as_integer()) v = double(i->get());
            else return false;
            if (!std::isfinite(v)) return false;  // TOML allows nan and inf, which no range check stops
            if (v < d.min / 100.0 - 1e-9 || v > d.max / 100.0 + 1e-9) return false;
            *d.real = float(v);
            return true;
        }
        case Type::Choice: {
            std::string pick;
            if (auto s = n.as_string()) pick = s->get();
            else if (auto i = n.as_integer()) pick = std::to_string(i->get());  // an older file's number
            else if (auto b = n.as_boolean()) {  // an older file's flag for a two-way choice
                if (d.flag) *d.flag = b->get();
                else if (d.value && !Numeric(d)) *d.value = b->get() ? 1 : 0;
                else return false;
                return true;
            } else {
                return false;
            }
            if (d.text) {  // a word kept even when this PC does not list it now
                if (pick.empty()) return false;
                *d.text = pick;
                return true;
            }
            for (size_t i = 0; i < d.choices.size(); i++) {
                if (pick != d.choices[i]) continue;
                if (d.flag) *d.flag = i == 1;
                else *d.value = ChoiceValue(d, i);
                return true;
            }
            return false;
        }
        case Type::String:
            if (auto s = n.as_string()) return *d.text = s->get(), true;
            return false;
    }
    return false;
}

}  // namespace

std::string ValueText(const Desc& d) {
    switch (d.type) {
        case Type::Bool: return *d.flag ? "true" : "false";
        case Type::Int: return std::to_string(*d.value);
        case Type::Float: {
            // two decimals, always with a point so TOML reads it as a float: 1.00, -0.50
            char b[32];
            std::snprintf(b, sizeof(b), "%.2f", double(*d.real));
            return b;
        }
        case Type::Choice: {
            if (d.text) return Quote(*d.text);
            int i = ChoiceIndex(d);
            return Quote(i >= 0 && size_t(i) < d.choices.size() ? d.choices[size_t(i)] : std::to_string(*d.value));
        }
        default: return Quote(*d.text);
    }
}

bool Read(const std::string& path, const std::vector<Desc>& list, std::string& error, bool* old_layout) {
    if (old_layout) *old_layout = false;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return true;
    toml::table t;
    try {
        t = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        std::ostringstream o;
        o << e.description() << " (line " << e.source().begin.line << ")";
        error = o.str();
        return false;
    } catch (...) {
        error = "the file could not be read";
        return false;
    }
    for (const Desc& d : list) {
        const toml::node* n = t[d.section][d.key].node();
        const toml::node* old = d.old_key ? t[d.old_key].node() : nullptr;
        if (old && !old->is_table() && old_layout) *old_layout = true;
        if (!n) n = old;
        if (n) Apply(d, *n);
    }
    return true;
}

std::string Text(const std::vector<Desc>& list, const std::string& previous) {
    toml::table old;
    if (!previous.empty()) {
        try {
            old = toml::parse(previous);
        } catch (...) {
        }
    }
    std::set<std::string> known, old_keys;
    std::vector<std::string> sections;
    for (const Desc& d : list) {
        known.insert(std::string(d.section) + "." + d.key);
        if (d.old_key) old_keys.insert(d.old_key);
        if (std::find(sections.begin(), sections.end(), d.section) == sections.end()) sections.push_back(d.section);
    }
    std::string out;
    // top-level keys nobody describes stay at the top; an older file's keys go, their values now live in sections
    for (auto& [k, v] : old) {
        std::string key(k.str());
        if (v.is_table() || old_keys.count(key)) continue;
        out += key + " = " + NodeText(v) + "\n";
    }
    if (!out.empty()) out += "\n";
    auto unknown = [&](const std::string& section) {
        std::string s;
        if (auto tbl = old[section].as_table())
            for (auto& [k, v] : *tbl)
                if (!known.count(section + "." + std::string(k.str()))) s += std::string(k.str()) + " = " + NodeText(v) + "\n";
        return s;
    };
    for (size_t i = 0; i < sections.size(); i++) {
        if (i) out += "\n";
        out += "[" + sections[i] + "]\n";
        for (const Desc& d : list)
            if (sections[i] == d.section) out += std::string(d.key) + " = " + ValueText(d) + "\n";
        out += unknown(sections[i]);
    }
    for (auto& [k, v] : old) {
        std::string section(k.str());
        if (!v.is_table() || std::find(sections.begin(), sections.end(), section) != sections.end()) continue;
        out += "\n[" + section + "]\n" + unknown(section);
    }
    return out;
}

bool Write(const std::string& path, const std::vector<Desc>& list, const std::string& header) {
    std::string previous;
    {
        std::ifstream f(path, std::ios::binary);
        if (f) previous.assign(std::istreambuf_iterator<char>(f), {});
    }
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    // written beside it and moved over it, so a launcher never reads half a file
    std::string tmp = path + ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << header << Text(list, previous);
        if (!f) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string Describe(const std::vector<Desc>& list, const std::vector<Preset>& presets) {
    std::string o = "{ \"settings\": [\n";
    for (size_t i = 0; i < list.size(); i++) {
        const Desc& d = list[i];
        o += "  { \"section\": " + Quote(d.section) + ", \"key\": " + Quote(d.key) + ", \"type\": \"" + TypeName(d.type) +
             "\", \"default\": " + ValueText(d);
        if (d.type == Type::Int) o += ", \"min\": " + std::to_string(d.min) + ", \"max\": " + std::to_string(d.max);
        if (d.type == Type::Float) {
            char b[64];
            std::snprintf(b, sizeof(b), ", \"min\": %.2f, \"max\": %.2f", d.min / 100.0, d.max / 100.0);
            o += b;
        }
        if (d.type == Type::Choice) {
            o += ", \"choices\": [";
            for (size_t c = 0; c < d.choices.size(); c++) o += (c ? ", " : "") + Quote(d.choices[c]);
            o += "]";
            if (d.labels.size() == d.choices.size()) {
                o += ", \"choice_labels\": [";
                for (size_t c = 0; c < d.labels.size(); c++) o += (c ? ", " : "") + Quote(d.labels[c]);
                o += "]";
            }
        }
        o += ", \"label\": " + Quote(d.label) + ", \"help\": " + Quote(d.help) + " }";
        o += i + 1 < list.size() ? ",\n" : "\n";
    }
    o += "]";
    if (!presets.empty()) {
        o += ",\n\"presets\": {\n";
        for (size_t p = 0; p < presets.size(); p++) {
            o += "  " + Quote(presets[p].name) + ": { ";
            for (size_t v = 0; v < presets[p].values.size(); v++)
                o += (v ? ", " : "") + Quote(presets[p].values[v].first) + ": " + presets[p].values[v].second;
            o += p + 1 < presets.size() ? " },\n" : " }\n";
        }
        o += "}";
    }
    return o + " }\n";
}

}  // namespace cfg
