// headless renderer: counts frames and draws, logs draw lists, draws chosen frames with the reference renderer
// REF=N: every Nth frame drawn on the CPU into guest memory and saved as cap/ref_<frame>.tga (REF_FROM, REF_TO: only
// within those frames)
// CAP=N: every Nth frame's draws listed in cap/draws_<frame>.txt
// snap in the run folder: the next two frames drawn and saved as snap_1.tga and snap_2.tga, then snap is removed
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/shaders.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "harness.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "reference_renderer.h"

namespace gpu {

namespace {

class NullBackend final : public Backend {
public:
    bool Init(uint32_t w, uint32_t h) override {
        LOGI("renderer: headless %ux%u", w, h);
        std::filesystem::create_directories(app::ExeDir() / "cap");
        return true;
    }
    void Clear(const State& s, uint32_t n, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) override {
        if (drawing_) reference::Clear(s, n, rects, flags, color, z, stencil);
    }
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override {
        draws_++;
        if (g_draw_observer) g_draw_observer(s);
        // a 2D draw the game moves sideways (g_ortho_place), as the D3D12 renderer moves it; asked on every frame, as
        // the renderer asks, since a game may keep state across frames
        float x0, x1, y0, y1, a = 1, b = 0;
        bool placed = g_ortho_place && OrthoExtent(s, start, count, indices, x0, x1, y0, y1) && g_ortho_place(s, x0, x1, y0, y1, a, b);
        if (drawing_) {
            if (placed) {
                auto moved = std::make_unique<State>(s);
                auto& p = moved->transforms[3];
                for (int r = 0; r < 4; r++) p[size_t(r * 4)] = a * p[size_t(r * 4)] + b * p[size_t(r * 4 + 3)];
                reference::Draw(*moved, prim, start, count, indices);
            } else {
                reference::Draw(s, prim, start, count, indices);
            }
        }
        if (list_) List(s, prim, start, count);
        static const bool texdump = harness::Env("TEXDUMP");
        if (texdump) DumpTexture(s);
    }
    void Copy(const Surface& src, const Surface& dst, uint32_t n, uint32_t rects, uint32_t points) override {
        if (harness::Env("TEXDUMP") == 2) {  // what is copied onto what, each pair once
            static std::set<uint64_t> seen;
            if (seen.insert(uint64_t(src.data) << 32 | dst.data).second)
                LOGI("copy: %08x fmt %x size %08x -> %08x fmt %x size %08x, %u rects", src.data, src.format, src.size,
                     dst.data, dst.format, dst.size, n);
        }
        if (drawing_) reference::Copy(src, dst, n, rects, points);
        else CopySurfaceMemory(src, dst, n, rects, points);
    }
    void Present(const Surface& front) override {
        if (list_) {
            std::fclose(list_);
            list_ = nullptr;
        }
        if (draws_ == last_draws_) empty_++;
        last_draws_ = draws_;
        if (drawing_ && !snap_) reference::WriteTga(front, File("cap/ref_%05llu.tga", frames_).c_str());
        // snap asks for the next two frames drawn
        std::string snap = (app::ExeDir() / "snap").string();
        if (snap_ == 2) {
            reference::WriteTga(front, (app::ExeDir() / "snap_2.tga").string().c_str());
            std::remove(snap.c_str());
            snap_ = 0;
        } else if (snap_ == 1) {
            reference::WriteTga(front, (app::ExeDir() / "snap_1.tga").string().c_str());
            snap_ = 2;
        } else if (FILE* f = std::fopen(snap.c_str(), "r")) {
            std::fclose(f);
            snap_ = 1;
        }
        static const long scan_at = harness::Env("TEXSCAN");
        if (scan_at && long(frames_) == scan_at) ScanTextures();
        static const long every = harness::Env("REF"), cap = harness::Env("CAP");
        // REF_FROM / REF_TO: only frames in that range drawn, so a short stretch late in a run can be captured closely
        static const long from = harness::Env("REF_FROM"), to = harness::Env("REF_TO");
        long next = long(frames_ + 1);
        bool in_range = next >= from && (!to || next <= to);
        drawing_ = snap_ || (every && in_range && next % every == 0);
        if (drawing_) reference::BeginFrame();
        harness::g_frames = frames_ + 1;
        if (++frames_ % 60 == 0) LOGI("renderer: frame %llu, %llu draws, %llu empty frames", frames_, draws_, empty_);
        if (cap && frames_ % cap == 0) list_ = std::fopen(File("cap/draws_%05llu.txt", frames_).c_str(), "w");
    }
    void Sync(uint32_t, uint32_t) override {}
    void SyncLate(uint32_t) override {}
    void Flush() override {}
    void SetGamma(const uint8_t*) override {}
    // counted only when every frame is drawn (REF=1); otherwise the SDK reads everything as visible
    bool Queries() const override { return harness::Env("REF") == 1 && !harness::Env("REF_FROM"); }
    void BeginQuery() override {
        query_from_ = reference::Passed();
        if (++tests_ == 1 || tests_ % 1000 == 0) LOGI("query: %llu visibility tests by frame %llu", tests_, frames_);
    }
    void EndQuery(uint32_t index) override {
        uint32_t n = uint32_t(reference::Passed() - query_from_);
        results_[index] = n;
        static const bool log = harness::Env("QUERY_LOG");
        if (log) LOGI("query: frame %llu test %u: %u pixels", frames_, index, n);
    }
    bool QueryResult(uint32_t index, uint32_t& pixels) override {
        auto it = results_.find(index);
        if (it == results_.end()) return false;
        pixels = it->second;
        return true;
    }

private:
    // TEXDUMP=1: each texture drawn under an orthographic projection (HUD, menus, prompts) once, as
    // cap/tex/<w>x<h>_<hash>.tga with the hash the renderer's texture packs use (no palette)
    void DumpTexture(const State& s) {
        const auto& pm = s.transforms[3];
        static const bool all = harness::Env("TEXDUMP") == 2;  // TEXDUMP=2: every draw, not only 2D
        if (!all && !(pm[11] == 0.0f && pm[15] == 1.0f)) return;
        for (int stage = 0; stage < (all ? 4 : 1); stage++) DumpStage(s.textures[stage], all);
    }
    void DumpStage(uint32_t t, bool all) {
        TextureDesc d;
        if (!t || !Describe(t, d) || d.cube || d.depth > 1 || (d.linear && !all)) return;
        uint64_t seed = uint64_t(d.format) | uint64_t(d.width) << 8 | uint64_t(d.height) << 24 | uint64_t(d.levels) << 40;
        uint64_t h = Hash(kernel::HostPtr(d.data), TotalSize(d), seed);
        if (!dumped_.insert(h).second) return;
        std::filesystem::create_directories(app::ExeDir() / "cap" / "tex");
        char name[96];
        std::snprintf(name, sizeof name, "cap/tex/%ux%u_%016llx_f%X.tga", d.width, d.height, (unsigned long long)h, d.format);
        reference::WriteTextureTga(t, (app::ExeDir() / name).string().c_str());
        // the guest bytes as stored (all levels), to find them in the game's files
        std::snprintf(name, sizeof name, "cap/tex/%ux%u_%016llx_f%X.bin", d.width, d.height, (unsigned long long)h, d.format);
        if (FILE* f = std::fopen((app::ExeDir() / name).string().c_str(), "wb")) {
            std::fwrite(kernel::HostPtr(d.data), 1, TotalSize(d), f);
            std::fclose(f);
        }
    }
    std::set<uint64_t> dumped_;
    // TEXSCAN=<frame>: at that frame, every 64x64 DXT3 texture header in guest memory (Common type texture) is
    // dumped as by TEXDUMP: textures loaded but not yet drawn, such as button pictures the game shows only sometimes
    void ScanTextures() {
        int n = 0;
        for (uint32_t p = 0x10000; p + 20 < 0x4000000; p += 4) {
            if ((p & 0xFFF) == 0 && !mem::IsCommitted(p)) {  // whole pages the game never touched
                p += 0xFFC;
                continue;
            }
            if ((p & 0xFFF) > 0xFEC && !mem::IsCommitted((p + 20) & ~0xFFFu)) continue;
            uint32_t common = kernel::R32(p);
            if ((common & 0x70000) != 0x40000) continue;
            TextureDesc d;
            // the button pictures' kind: 64x64, format 0xE (DXT3)
            if (!Describe(p, d) || d.cube || d.depth > 1 || d.linear || d.width != 64 || d.height != 64 || d.format != 0xE) continue;
            if (!mem::IsCommitted(d.data) || !mem::IsCommitted(d.data + TotalSize(d) - 1)) continue;
            uint64_t seed = uint64_t(d.format) | uint64_t(d.width) << 8 | uint64_t(d.height) << 24 | uint64_t(d.levels) << 40;
            uint64_t h = Hash(kernel::HostPtr(d.data), TotalSize(d), seed);
            if (!dumped_.insert(h).second) continue;
            std::filesystem::create_directories(app::ExeDir() / "cap" / "scan");
            char name[96];
            std::snprintf(name, sizeof name, "cap/scan/%ux%u_%016llx_f%X.tga", d.width, d.height, (unsigned long long)h, d.format);
            if (reference::WriteTextureTga(p, (app::ExeDir() / name).string().c_str())) n++;
        }
        LOGI("harness: %d textures found in memory", n);
        logx::Flush();
    }

    static std::string File(const char* fmt, unsigned long long n) {
        char name[64];
        std::snprintf(name, sizeof name, fmt, n);
        return (app::ExeDir() / name).string();
    }

    // one draw's state, textures, transforms, declaration and first vertex
    void List(const State& s, uint32_t prim, uint32_t start, uint32_t count) {
        FILE* f = list_;
        std::fprintf(f, "DRAW prim=%u n=%u vs=%X ps=%X rt=%08X tex0=%08X light=%u blend=%u %X %X cw=%X at=%u z=%u\n", prim, count,
                     s.vs_handle, s.ps ? s.ps->handle : 0, s.rt.data, s.textures[0], s.rs[kRsLighting], s.simple[kMBlendEnable >> 2],
                     s.simple[kMSrcBlend >> 2], s.simple[kMDestBlend >> 2], s.simple[kMColorWriteEnable >> 2],
                     s.simple[kMAlphaTestEnable >> 2], s.rs[kRsZEnable]);
        const auto& t0 = s.tss[0];
        TextureDesc d;
        if (s.textures[0] && Describe(s.textures[0], d))
            std::fprintf(f, "  tex %ux%u fmt %X cop %u %X %X aop %u %X %X\n", d.width, d.height, d.format, t0[kTssColorOp],
                         t0[kTssColorArg1], t0[kTssColorArg2], t0[kTssAlphaOp], t0[kTssAlphaArg1], t0[kTssAlphaArg2]);
        for (int st = 1; st < 4; st++) {  // the other stages, when bound
            const auto& ts = s.tss[size_t(st)];
            if (s.textures[size_t(st)] && Describe(s.textures[size_t(st)], d))
                std::fprintf(f, "  tex%d %08X %ux%u fmt %X cop %u %X %X aop %u %X %X tci %X\n", st, s.textures[size_t(st)], d.width,
                             d.height, d.format, ts[kTssColorOp], ts[kTssColorArg1], ts[kTssColorArg2], ts[kTssAlphaOp],
                             ts[kTssAlphaArg1], ts[kTssAlphaArg2], ts[kTssTexCoordIndex]);
        }
        std::fprintf(f, "  ttf %X tci %X addr %u %u t16", t0[kTssTextureTransformFlags], t0[kTssTexCoordIndex], t0[kTssAddressU],
                     t0[kTssAddressV]);
        auto matrix = [&](const char* name, uint32_t which) {
            std::fprintf(f, "%s", name);
            for (float v : s.transforms[which]) std::fprintf(f, " %g", v);
        };
        matrix("", 16);
        matrix("\n  world", 256);
        matrix("\n  view", 2);
        matrix("\n  proj", 3);
        std::fprintf(f, "\n");
        if (s.vs) {
            std::fprintf(f, "  decl");
            for (int r = 0; r < 16; r++) {
                const auto& in = s.vs->inputs[size_t(r)];
                if (in.stream >= 0) std::fprintf(f, " v%d:%d+%u/%X", r, in.stream, in.offset, in.type);
            }
            std::fprintf(f, "\n");
        }
        if (s.streams[0].vb) {
            // the vertex buffer's Data at +4 is a physical address
            uint32_t data = mem::VirtualFromPhysical(kernel::R32(s.streams[0].vb + 4)) + start * s.streams[0].stride;
            std::fprintf(f, "  v0 stride %u:", s.streams[0].stride);
            for (uint32_t i = 0; i < s.streams[0].stride / 4 && i < 10; i++) {
                uint32_t v = kernel::R32(data + i * 4);
                float x;
                std::memcpy(&x, &v, 4);
                std::fprintf(f, " %g(%08X)", x, v);
            }
            std::fprintf(f, "\n");
        }
    }

    unsigned long long frames_ = 0, draws_ = 0, last_draws_ = 0, empty_ = 0;
    bool drawing_ = false;
    int snap_ = 0;
    FILE* list_ = nullptr;
    uint64_t query_from_ = 0;
    unsigned long long tests_ = 0;
    std::map<uint32_t, uint32_t> results_;
};

}  // namespace

std::unique_ptr<Backend> CreateBackend() { return std::make_unique<NullBackend>(); }

}  // namespace gpu
