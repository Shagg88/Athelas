// headless host: <exe> <game folder> [--instance N], run from the folder that gets logs, hdd and cap
// the game folder may come from GAME_DIR instead
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "kernel/disc.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/kernel.h"

int main(int argc, char** argv) {
    std::string game;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--instance") && i + 1 < argc) setenv("INSTANCE", argv[++i], 1);
        else game = argv[i];
    }
    // a relaunch into another image comes back without arguments
    if (game.empty() && std::getenv("GAME_DIR")) game = std::getenv("GAME_DIR");
    setenv("GAME_DIR", game.c_str(), 1);
    if (game.empty()) {
        std::fprintf(stderr, "usage: %s <game folder> [--instance N]\n", argv[0]);
        return 2;
    }
    logx::Init((app::ExeDir() / "logs").string(), app::kGame.log);
    app::Load();
    if (app::kGame.init) app::kGame.init();
    if (app::kGame.window) app::kGame.window(nullptr);
    std::string hdd = app::Instance() > 1 ? "hdd" + std::to_string(app::Instance()) : "hdd";
    std::vector<uint8_t> xbe;
    // a disc image (.iso): mounted as the D: drive, the xbe read from it, as the port does
    if (game.size() > 4 && (game.compare(game.size() - 4, 4, ".iso") == 0 || game.compare(game.size() - 4, 4, ".ISO") == 0)) {
        auto disc = std::make_unique<kernel::Disc>();
        std::string error;
        if (!disc->Open(game, error) || !disc->Load(IMAGE_XBE, xbe)) {
            std::fprintf(stderr, "%s: %s\n", game.c_str(), error.empty() ? "no " IMAGE_XBE : error.c_str());
            return 2;
        }
        LOGI("harness: disc image %s", game.c_str());
        kernel::MountDisc(std::move(disc));
        game = std::filesystem::path(game).parent_path().string();
    } else {
        std::ifstream f(game + "/" + IMAGE_XBE, std::ios::binary);
        xbe.assign(std::istreambuf_iterator<char>(f), {});
    }
    if (!kernel::Boot(std::move(xbe), IMAGE_XBE, game, (app::ExeDir() / hdd).string())) return 1;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        logx::Flush();
    }
}
