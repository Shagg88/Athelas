// The game in assets: its files, or a disc image (.iso) with this executable's xbe on it. The xbe must be the one
// this executable was recompiled from; another release's would run its own data under this code.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <fstream>

#include "app/app.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/crypto.h"
#include "kernel/disc.h"

extern const char kImageSha1[];  // the xbe recompiled into this executable, written by CMake

namespace app {

namespace {

namespace fs = std::filesystem;

// assets beside the exe or in any folder above it (the build output)
fs::path FindAssets() {
    auto dir = ExeDir();
    std::error_code ec;
    for (int i = 0; i < 6; i++) {
        if (fs::is_directory(dir / "assets", ec)) {
            // assets/redirect.txt: one line naming the folder that holds the game, so a dump elsewhere is used in place
            std::ifstream r(dir / "assets" / "redirect.txt");
            std::string line;
            if (r && std::getline(r, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (!line.empty() && fs::is_directory(fs::path(line), ec)) return fs::path(line);
            }
            return dir / "assets";
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

std::string Sha1(const std::vector<uint8_t>& data) {
    alignas(8) uint8_t ctx[128];
    uint8_t digest[20];
    crypto::ShaInit(ctx);
    crypto::ShaUpdate(ctx, data.data(), data.size());
    crypto::ShaFinal(ctx, digest);
    std::string hex;
    for (uint8_t b : digest) {
        hex += "0123456789abcdef"[b >> 4];
        hex += "0123456789abcdef"[b & 15];
    }
    return hex;
}

bool ReadHostFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

// certificate +8 of an xbe's header: its title id, 0 if it is no xbe
uint32_t TitleId(const std::vector<uint8_t>& x) {
    auto u32 = [&](size_t at) {
        uint32_t v = 0;
        if (at + 4 <= x.size()) std::memcpy(&v, x.data() + at, 4);
        return v;
    };
    if (x.size() < 0x180 || std::memcmp(x.data(), "XBEH", 4)) return 0;
    uint32_t base = u32(0x104), cert = u32(0x118);
    return cert > base ? u32(size_t(cert - base) + 8) : 0;
}

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

}  // namespace

// the game where --game says (a folder with the xbe, or a disc image), else in assets
bool FindGame(GameFiles& game) {
    const fs::path& given = Args().game;
    std::error_code ec;
    fs::path folder;
    std::vector<fs::path> isos;
    if (!given.empty()) {
        if (fs::is_directory(given, ec)) folder = given;
        else if (fs::is_regular_file(given, ec)) isos.push_back(given);
        else Fail(kExitNoGame, L"The game files couldn't be found: " + given.wstring() + L" does not exist.");
    } else {
        folder = FindAssets();
    }
    if (!folder.empty())
        for (auto& e : fs::directory_iterator(folder, ec)) {
            std::wstring ext = e.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
            if (ext == L".iso" && e.is_regular_file(ec)) isos.push_back(e.path());
        }
    std::sort(isos.begin(), isos.end());

    // the first copy with this xbe wins, the extracted files before any image; another release is kept to offer
    GameFiles other;
    std::unique_ptr<kernel::Disc> other_disc;
    fs::path other_name, foreign;  // another release of this game; another game altogether
    auto consider = [&](const fs::path& name, std::vector<uint8_t> xbe, std::unique_ptr<kernel::Disc> disc) {
        std::string sha = Sha1(xbe);
        bool match = sha == kImageSha1;
        uint32_t tid = TitleId(xbe);
        bool same_title = !kGame.title_id || tid == kGame.title_id;
        LOGR("Game: %s in %s, title %08X, sha1 %s%s", IMAGE_XBE, name.string().c_str(), tid, sha.c_str(),
             match ? "" : same_title ? ", another release" : ", another game");
        fs::path dir = fs::is_directory(name, ec) ? name : name.parent_path();
        if (match) {
            game = {dir, std::move(xbe)};
            kernel::MountDisc(std::move(disc));
            return true;
        }
        if (!same_title) {
            if (foreign.empty()) foreign = name;
        } else if (other_name.empty()) {
            other = {dir, std::move(xbe)};
            other_disc = std::move(disc);
            other_name = name;
        }
        return false;
    };

    std::vector<uint8_t> xbe;
    if (!folder.empty() && ReadHostFile(folder / IMAGE_XBE, xbe) && consider(folder, std::move(xbe), nullptr)) return true;
    for (const fs::path& iso : isos) {
        auto disc = std::make_unique<kernel::Disc>();
        std::string error;
        if (!disc->Open(iso, error)) {
            LOGW("game: %s: %s", iso.string().c_str(), error.c_str());
            continue;
        }
        if (!disc->Load(IMAGE_XBE, xbe)) {
            LOGW("game: %s has no %s", iso.string().c_str(), IMAGE_XBE);
            continue;
        }
        if (consider(iso, std::move(xbe), std::move(disc))) return true;
    }

    if (other_name.empty()) {
        if (!foreign.empty()) Fail(kExitNoGame, L"The game files couldn't be found: " + foreign.wstring() + L" holds another game.");
        if (!given.empty())
            Fail(kExitNoGame, L"The game files couldn't be found: " + given.wstring() + L" has no " + Wide(IMAGE_XBE) +
                                  L" and no disc image with one.");
        Fail(kExitNoGame, kGame.missing);
    }
    std::wstring text = other_name.filename().wstring() +
                        L" is a different release of the game than this build was made from (only the US release is "
                        L"supported for now), and will most likely crash.";
    if (Args().no_dialogs) Fail(kExitWrongRelease, text);
    if (MessageBoxW(nullptr, (text + L"\n\nStart it anyway?").c_str(), kGame.short_title,
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
        LOGR("Exit (%d): another release, not started", kExitWrongRelease);
        logx::Flush();
        ExitProcess(kExitWrongRelease);
    }
    game = std::move(other);
    kernel::MountDisc(std::move(other_disc));
    return true;
}

}  // namespace app
