// the mixer pulled every 10 ms by the clock, its output dropped, or written to AUDIO_WAV as 16 bit stereo
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "audio/audio.h"

namespace audio {

namespace {

void Header(std::FILE* f, uint32_t bytes) {
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fseek(f, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(kRate); u32(kRate * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    std::fseek(f, 0, SEEK_END);
}

}  // namespace

void StartOutput(std::function<void(float*, uint32_t)> render) {
    std::thread([render] {
        std::FILE* wav = nullptr;
        if (const char* p = std::getenv("AUDIO_WAV")) {
            wav = std::fopen(p, "wb");
            if (wav) Header(wav, 0);
        }
        uint32_t bytes = 0;
        std::vector<float> b(kRate / 100 * 2);
        std::vector<int16_t> s(b.size());
        for (uint32_t n = 0;; n++) {
            render(b.data(), kRate / 100);
            if (wav) {
                for (size_t i = 0; i < b.size(); i++) {
                    float x = b[i] < -1 ? -1 : b[i] > 1 ? 1 : b[i];
                    s[i] = int16_t(x * 32767);
                }
                std::fwrite(s.data(), 2, s.size(), wav);
                bytes += uint32_t(s.size() * 2);
                if (n % 100 == 0) {  // a header that stays right when the run is killed
                    Header(wav, bytes);
                    std::fflush(wav);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }).detach();
}

}  // namespace audio
