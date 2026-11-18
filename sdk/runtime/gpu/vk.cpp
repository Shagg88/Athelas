// The Vulkan backend ([graphics] renderer = 1), phase 1 of notes/vulkan-plan.md: instance, device, swapchain, a frame
// ring, render targets by console address, Clear, and Present showing the front buffer. Draws are not done yet: every
// other call is a safe no-op, logged once.
// Init failing (no Vulkan, no 1.2 device, no swapchain) leaves the game on Direct3D 12 (xdk/d3d8.cpp).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>  // S11: whether the window's screen shows HDR (as Direct3D 12 asks)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <thread>
#include <unordered_set>
#include <vector>

#include "gpu/vk_loader.h"  // first: the platform define before vulkan.h is read
#ifndef IMGUI_IMPL_VULKAN_NO_PROTOTYPES
#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES
#endif
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_win32.h>

#include "app/app.h"
#include "core/diag.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/common.h"
#include "gpu/ext.h"
#include "third_party/smaa/AreaTex.h"
#include "third_party/smaa/SearchTex.h"  // a game's renderer hooks (S7: the ones that are not Direct3D 12's own)
#include "gpu/shaders.h"
#include "gpu/spirv.h"
#include "gpu/vertex.h"
#include "gpu/texture.h"
#include "gpu/texture_pack.h"
#include "gpu/vk_loader.h"
#include "gpu/vk_mem.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace gpu {

namespace {

using namespace gpu::vk;

constexpr uint32_t kFrames = 2;  // frames in flight

// XBE_PERF: the render thread's time in Draw and Present, added up (S16), for the 5 s perf line
const bool g_vk_perf = [] { const char* e = std::getenv("XBE_PERF"); return e && *e && *e != '0'; }();
struct PerfAdd {
    double* acc;
    LARGE_INTEGER t0{};
    explicit PerfAdd(double& a) : acc(g_vk_perf ? &a : nullptr) {
        if (acc) QueryPerformanceCounter(&t0);
    }
    ~PerfAdd() {
        if (!acc) return;
        static const double per_ms = [] {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return double(f.QuadPart) / 1000.0;
        }();
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        *acc += double(t1.QuadPart - t0.QuadPart) / per_ms;
    }
};

// S15: a shaders.seed entry (Direct3D 12's layout: entry point, profile, HLSL); vs_* and ps_* compile to SPIR-V too,
// as both renderers make their game shaders from the same generators
struct SeedSrc {
    std::string entry, profile, src;
};

bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const VkExtensionProperties& e : list)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

const char* ResultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VkResult";
    }
}

class VkBackend final : public Backend {
public:
    ~VkBackend() override { Destroy(); }

    bool Init(uint32_t width, uint32_t height) override;
    const char* InitError() const override { return error_.empty() ? nullptr : error_.c_str(); }

    void Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
               uint32_t stencil) override;
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override;
    void Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) override;
    void Present(const Surface& front) override;
    // the screen's pass (kBlitHlsl, as Direct3D 12's Blit): src sampled into the rendering begun by the caller, with
    // the 32 constants of BlitConstants; pipelines by target format
    bool BlitPass(VkImageView src, const float c[32], VkFormat dst_fmt) {
        return BlitPass(src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, c, dst_fmt);
    }
    // over: blended over what the target holds (ONE, INV_SRC_ALPHA; its alpha kept), as Direct3D 12's over pipeline
    // point: s0 reads unfiltered too (a depth format without linear filtering)
    bool BlitPass(VkImageView src, VkImageLayout src_layout, const float c[32], VkFormat dst_fmt, bool over = false,
                  bool point = false);
    VkPipeline BlitPipeline(VkFormat fmt, bool over = false);
    std::unordered_map<uint32_t, VkPipeline> blit_pipes_;
    VkSampler blit_smp_[2] = {};  // linear clamp (s0), point clamp (s1..s3)
    bool EnsureBlitSamplers();
    bool EnsureReadback(VkDeviceSize bytes);
    // the game's gamma ramp (red, green, blue: 256 each) with [display] brightness over it, as Direct3D 12: a 256 x 1
    // lookup the screen pass reads at t1 when it is not the identity
    uint8_t ramp_[768] = {};
    bool ramp_set_ = false, ramp_dirty_ = false, gamma_ = false;
    float brightness_ = 1.0f;
    uint8_t Brighten(uint8_t v) const;
    void UpdateGammaRamp();
    void ReadbackToHost();  // the copies into readback_ made visible to the CPU (before the Submit that waits)
    void Sync(uint32_t data, uint32_t size) override;
    // S12, a NOOVERWRITE lock (as Direct3D 12): the newest finished copy of the surface (a frame or two old) goes to
    // the title without waiting for the GPU; a copy for a later lock is started. Without timeline semaphores (Vulkan
    // 1.2) DeliverLate has nothing and the lock waits in Sync as before
    void SyncLate(uint32_t data) override {
        if (!DeliverLate(data)) Sync(data, 0);
        RecordLate(data);
    }
    bool DeliverLate(uint32_t data) override;
    void RecordLate(uint32_t data) override;
    void Flush() override;
    void SetGamma(const uint8_t* ramp) override;
    // S13, the visibility tests (as Direct3D 12's): occlusion queries counted at our resolution, given back in the
    // title's pixels. Fences and busy checks stay the Backend defaults (the render thread does them)
    void BeginQuery() override;
    void EndQuery(uint32_t index) override;
    bool QueryResult(uint32_t index, uint32_t& pixels) override;
    bool Queries() const override { return !query_failed_; }

private:
    bool Fail(const std::string& why) {
        error_ = why;
        LOGR("vulkan: %s", why.c_str());
        return false;
    }
    bool Fail(const char* what, VkResult r) { return Fail(std::string(what) + " failed (" + ResultName(r) + ")"); }

    bool CreateInstance();
    bool CreateSurface();
    bool PickDevice();
    bool CreateDevice();
    bool CreateSwapchain();
    void DestroySwapchain();
    bool CreateFrames();
    void Destroy();
    bool PickDepthFormat();
    bool CreateDrawResources();

    // an image with its own memory and one view
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;  // its memory, a piece of one of the allocator's blocks
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t levels = 1, layers = 1;
    };
    bool MakeImage(Image& im, const VkImageCreateInfo& ci, VkImageViewType view, VkImageAspectFlags view_aspect);
    void DestroyImage(Image& im);
    void Retire(Image& im);
    bool CreateDummy(Image& im, VkImageType type, VkImageViewType view, VkFormat fmt, uint32_t layers);
    void Barrier(VkImage image, const VkImageSubresourceRange& range, VkImageLayout& cur, VkImageLayout want);
    // S4 (D3): a layout change orders an image's earlier writes before its next use, but an image used twice in the
    // same layout (a target drawn, another drawn, the first drawn again; two copies into one image) had nothing
    // between. writes_open_: something was written since the last full barrier; OrderWrites puts one in
    bool writes_open_ = false;
    void OrderWrites();
    bool Upload(VkDeviceSize size, VkDeviceSize align, bool may_flush, VkDeviceSize& off, uint8_t*& cpu);
    // S16: a draw's constants, the last copy's place again when they are the same bytes (as Direct3D 12's last_cb_)
    struct LastUpload {
        uint64_t gen = 0;  // ring_gen_ when it was written; another generation's bytes are gone
        VkDeviceSize off = 0;
        std::vector<uint8_t> bytes;  // a CPU copy to compare with (the ring is write-combined memory, slow to read)
    };
    LastUpload last_vc_, last_dc_;
    // S16, the repeat path (as Direct3D 12's rep_): a draw in the same state as the draw before (g_draw_same_state),
    // with the same vertex layout, into the rendering that draw left open with nothing bound since, is drawn with what
    // that draw bound (pipeline, set and offsets, viewport, scissor): only its vertices go into the ring
    struct Repeat {
        bool ok = false;
        const void *rt = nullptr, *ds = nullptr;  // the draw's targets (Target is declared further down)
        bool ms = false;
        float place_a = 1, place_b = 0;
        uint16_t regs = 0;
        uint64_t decode = 0;
        bool pretransformed = false, strip = false, quad = false;
        Topology topology = Topology::Triangles;
        uint32_t buffer_count = 0;
        std::array<VertexElement, 16> elements{};
        uint32_t strides[16]{};
    } rep_;
    bool SameLayout(const Assembled& a, bool quad) const {
        if (a.regs != rep_.regs || a.decode != rep_.decode || a.pretransformed != rep_.pretransformed ||
            a.strip != rep_.strip || quad != rep_.quad || a.topology != rep_.topology ||
            a.buffer_count != rep_.buffer_count)
            return false;
        for (uint32_t b = 0; b < a.buffer_count; b++)
            if (a.buffers[b].stride != rep_.strides[b]) return false;
        return std::memcmp(a.elements.data(), rep_.elements.data(), sizeof(a.elements)) == 0;
    }
    // a draw's vertices (and indices) into the ring, bound, drawn: false when the ring is full
    bool DrawGeometry(VkCommandBuffer cb, const Assembled& a);
    uint64_t repeats_ = 0, draws_ = 0, waits_ = 0;
    uint64_t no_rep_[4] = {};  // state changed, nothing to repeat, rendering or targets differ, 2D place or layout
    double draw_ms_ = 0, present_ms_ = 0, wait_ms_ = 0;
    uint64_t ring_gen_ = 1;  // a new one each time a frame slot's ring starts again
    bool UploadConstants(const void* data, VkDeviceSize size, LastUpload& last, VkDeviceSize& off);

    // a console surface drawn into: a colour or depth image at the surface's console size (render scale comes later)
    struct Target {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;  // its memory, a piece of one of the allocator's blocks
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool depth = false;
        uint32_t gw = 0, gh = 0;  // the console's size
        uint32_t w = 0, h = 0;    // drawn at render scale
        uint32_t data = 0, bytes = 0;  // guest memory under it
        uint64_t epoch = 0, sig = 0;   // the last lock seen, its memory as last matched
        bool drawn = false;            // the GPU wrote it since the last read-back
        std::vector<uint8_t> read;     // what the last read-back left in guest memory (S6: the CPU's changes to it)
        VkImageView sample_view = VK_NULL_HANDLE;  // a depth target read by a shader: its depth alone
        Image fb;  // the copy a draw reads of the target it draws into
        // S14, anti-aliasing as Direct3D 12's: the frame's own targets have a multisampled twin the draws go into
        // when every bound target has one; each side is brought up to date only when the other is used (lazily):
        // ms_new, the twin was drawn since image last matched it; res_new, image was written since the twin did
        VkImage ms = VK_NULL_HANDLE;
        VmaAllocation ms_alloc = VK_NULL_HANDLE;
        VkImageView ms_view = VK_NULL_HANDLE;
        VkImageLayout ms_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool ms_new = false, res_new = false;
    };
    Target* EnsureTarget(const Surface& s, bool depth);
    // S6 leftovers, as Direct3D 12: a smaller depth surface laid on the depth buffer's memory is a target of its own
    // (SizedTargetKey), so it no longer replaces the frame's depth target and has it made again every frame
    uint64_t SizedKey(uint32_t data, bool depth, uint32_t gw, uint32_t gh) const {
        uint64_t key = uint64_t(data) << 1 | (depth ? 1 : 0);
        if (!depth || (gw == guest_w_ && gh == guest_h_)) return key;
        return key | uint64_t(gw & 0x3FFF) << 34 | uint64_t(gh & 0xFFFF) << 48;
    }
    // a depth buffer drawn (read as colour) into memory that is another depth buffer: copied as depth (DepthCopy)
    bool DepthCopy(const State& s);
    VkPipeline depth_copy_pipe_ = VK_NULL_HANDLE;
    // the frame as the screen pass draws it, to a PNG; suffix: a F7 pair's name after the game's
    void Screenshot(Target& t, const std::string& suffix = {});
    std::string pair_name_;
    int pair_step_ = 0;
    // S9, movies as Direct3D 12 shows them: each movie frame kept, averaged to up to twice the console's size, in a
    // ring; the screen shows the newest one old enough for [graphics] cutscene sound delay, placed by movie_display
    // and sharpened at its own size; the launcher's art shot taken from it
    struct MovieFrame {
        Image img;
        uint32_t w = 0, h = 0;
        double time = 0;
    };
    static constexpr uint32_t kMovieFrames = 32;
    MovieFrame movie_ring_[kMovieFrames];
    uint32_t movie_n_ = 0;
    bool movie_on_ = false;
    float frame_sides_ = 1.0f;  // g_frame_sides for the frame shown
    // render_percent over 100: the frame averaged down to the size shown first (S10, Direct3D 12's shown_)
    Image shown_;
    uint32_t shown_w_ = 0, shown_h_ = 0;
    // a full-screen pass of hlsl (its vs and entry) into a rendering the caller began: src at t0..t2 (null: a stand-in),
    // p at b1, s0 linear and s1 point; pipelines by (key, entry, format)
    bool PassDraw(const std::string& hlsl, uint64_t key, const char* entry, VkFormat dst_fmt, const VkImageView src[3],
                  const float p[8]);
    std::unordered_map<uint64_t, VkPipeline> pass_pipes_;
    // SMAA ([graphics] smaa; S10): edges, weights and blend at the frame's size before the screen pass, as Direct3D 12
    Image smaa_area_, smaa_search_, smaa_edges_, smaa_weights_, smaa_out_;
    uint32_t smaa_w_ = 0, smaa_h_ = 0;
    bool smaa_failed_ = false;
    // view (w x h, shader-readable) anti-aliased into smaa_out_; its view, or null when SMAA can't run
    VkImageView Smaa(VkCommandBuffer cb, VkImageView view, uint32_t w, uint32_t h);
    void KeepMovieFrame(Target& t);
    MovieFrame* PickMovieFrame();
    void ArtShot();
    // the frame's sides painted black inside a rendering of w x h (g_frame_sides), the frame at aspect
    void BlackSides(VkCommandBuffer cb, uint32_t w, uint32_t h, float aspect);
    // F3 frame captures (S2), as Direct3D 12's: frame.txt lists every clear, copy and draw with its state; each target
    // is saved as NNNN_ADDR.bmp (and _z) when the frame moves off it, each texture as tex_ADDR.bmp, each shader once
    // as vs_/ps_KEY.hlsl, the front buffer as final.bmp; the same names, so a script can pair the two renderers' files
    FILE* cap_ = nullptr;
    std::filesystem::path cap_dir_;
    uint32_t cap_i_ = 0, cap_rt_ = 0;
    std::unordered_set<uint32_t> cap_seen_;
    std::unordered_set<uint64_t> cap_sh_;
    struct CapTex {
        VkImageView view;
        VkImageLayout layout;
        uint32_t data, w, h;  // the console's size (the BMP's)
        uint32_t sw, sh;       // the bound image's
    };
    std::vector<CapTex> cap_tex_;
    std::vector<Image> cap_hold_;  // images retired during a capture: kept until it ends (cap_tex_ reads them)
    void StartCapture();
    void FinishCapture(const Surface& front);
    void CapStep(const Surface& rt);
    // view (in layout) shrunk to w x h, colour times mul, written as name.bmp; the GPU waited for (captures only)
    void CapSave(VkImageView view, VkImageLayout layout, uint32_t src_w, uint32_t src_h, uint32_t w, uint32_t h,
                 float mul, const char* name, bool point = false);
    void CapSave(Target& t, const char* name);
    void CaptureShader(const char* kind, uint64_t key, const std::string& src);
    Image lut_;                  // the gamma lookup (UpdateGammaRamp)
    Target* FindTarget(const Surface& s, bool depth);
    void DestroyTarget(Target& t);
    // a console rect in the target's own pixels, clamped to it
    static VkRect2D ScaledRect(const Target& t, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void MaskedClear(const State& s, Target& t, uint32_t count, const uint8_t* rects, uint32_t color, uint32_t mask,
                     bool ms = false);
    void ClearInto(const State& s, Target* rt, Target* ds, bool ms, uint32_t count, const uint8_t* rects, uint32_t flags,
                   uint32_t color, float z, uint32_t stencil);
    bool CpuWrote(Target& t);
    static bool ChangedSinceRead(const Target& t);
    static std::vector<uint8_t> LatchCpuChanges(Target& t);
    // what the CPU wrote, decoded and drawn over the target at its render scale (S6, as Direct3D 12's UploadTarget;
    // s: the surface or texture header that describes it)
    void UploadTarget(Target& t, const Surface& s);
    // t drawn into scratch_ at the console's size (one bilinear tap a pixel, as Direct3D 12's Shrink), left as a
    // transfer source; false on failure
    bool ShrinkToScratch(Target& t);
    // frames a target was last looked up in; a target unused for long may be the title's memory for something else
    std::unordered_map<uint32_t, uint64_t> target_used_;
    bool FindMemory(uint32_t bits, VkMemoryPropertyFlags want, uint32_t& index) const;

    // recording: one command buffer per frame, open from the first command to Present (or Flush)
    VkCommandBuffer Cmd();
    void Submit(bool wait);
    bool WaitGpu(VkFence fence, const char* where);
    void WaitSerial(uint64_t value);  // the timeline semaphore at value (a submitted serial), 2 s checks
    void DeviceLost(const char* where, VkResult r);
    void ToLayout(VkImage image, bool depth, VkImageLayout& cur, VkImageLayout want);
    // a target's own (single-sample) image to a layout for a read or a write outside a draw into its twin: the twin's
    // newer pixels resolved into it first (S14), and a write marks it newer than the twin
    void ToLayout(Target& t, VkImageLayout want);
    // S14
    uint32_t samples_ = 1;  // anti-aliasing samples, 1 off
    std::unordered_map<uint32_t, bool> ms_ok_;  // formats that take samples_
    uint32_t PickSamples();
    bool MsFormatOk(VkFormat fmt, bool depth);
    bool WantMs(const Target& t);
    bool MakeTwin(Target& t);
    bool clearing_ = false;  // a clear into both a target and its twin: neither brought up to date, nor marked newer
    VkPipeline MsUpPipeline(bool depth);
    void ReadyRes(Target& t);  // image as new as the twin: before anything but a draw into the twin
    void ReadyMs(Target& t);   // the twin as new as image: before a draw into the twin
    VkPipeline ms_up_[2] = {};  // image into the twin: colour, depth (at samples_)
    // a full-screen pass's descriptor set (b0/b1 p, t0..t2 src in their layouts) bound for the pipeline bound
    bool PassSet(const VkImageView src[3], const VkImageLayout layout[3], const float p[8]);
    // S16 (D19): this frame's set for b0/b1 of these sizes (dynamic offsets at the bind) and these images and
    // samplers, made once per frame; null when the pool is out
    VkDescriptorSet CachedSet(VkDeviceSize range0, VkDeviceSize range1, const VkDescriptorImageInfo imgs[4],
                              const VkDescriptorImageInfo smps[4]);
    // dynamic rendering into rt / ds (either may be null), kept open across draws until a transfer needs it closed
    // ms: into the targets' multisampled twins (S14)
    bool BeginRendering(Target* rt, Target* ds, bool ms = false);
    void EndRendering();
    // S13: a test is counted in pieces, one an open rendering (a query may not span renderings, and none can be
    // begun or reset between draws of one); its count is the sum. Entries are reset a few dozen at a time ahead of
    // use, outside a rendering, so a test begun between two draws starts without closing the rendering
    static constexpr uint32_t kQueryEntries = 2048, kQueryIndexes = 256, kQueryRing = 4, kQueryPieces = 16;
    struct QueryRec {
        uint32_t entry[kQueryPieces] = {};
        uint64_t tag[kQueryPieces] = {};  // the entry's use: a later test that took the entry changes it
        float ratio[kQueryPieces] = {};  // the title's pixels over ours (or samples) on the target each piece drew to
        uint32_t pieces = 0;
        uint64_t seq = 0;     // 0: none
        uint64_t serial = 0;  // the submit carrying its last piece (review of S13: read only once that finished)
    };
    struct QuerySlot {
        QueryRec rec[kQueryRing];
        uint32_t next = 0;
        uint64_t got = 0;  // the newest record read
        uint32_t pixels = 0;
        bool have = false;
    };
    bool EnsureQueries();
    void QueryTopUp(VkCommandBuffer cb);  // outside a rendering
    void QueryPieceBegin(VkCommandBuffer cb);  // inside one
    void QueryPieceEnd(VkCommandBuffer cb);
    VkQueryPool query_pool_ = VK_NULL_HANDLE;
    bool query_failed_ = false, precise_ = false;
    bool query_open_ = false;  // a test is between BeginQuery and EndQuery
    bool query_hold_ = false;  // a drawn clear: not part of the test (Direct3D clears are not counted)
    int64_t query_piece_ = -1;  // the entry begun in the open rendering
    uint32_t query_next_ = 0, query_ready_ = 0;  // entries query_next_ onwards, query_ready_ of them, are reset
    QueryRec query_cur_;
    std::unique_ptr<std::atomic<uint64_t>[]> query_tag_;
    uint64_t query_tags_ = 0, query_seq_ = 0;
    QuerySlot queries_[kQueryIndexes];
    std::mutex query_lock_;  // QueryResult runs on the title's thread
    void BlitToScreen(VkCommandBuffer cb, Target* front, uint32_t index);
    bool InitUi();
    void RenderUi(VkCommandBuffer cb, uint32_t index);
    void CountFps();
    bool ui_ = false;  // ImGui's Vulkan backend is set up
    VkDescriptorPool ui_pool_ = VK_NULL_HANDLE;
    double fps_ = 0;
    uint64_t fps_frames_ = 0;
    uint64_t fps_start_ = 0;

    // draws
    struct Tex {
        Image img;
        TextureDesc d;
        uint32_t levels = 1, host_w = 0, host_h = 0;
        uint64_t sig = 0, frame = 0, palette = 0;
        // a texture pack's file drawn in its place (TexturePack): its view, owned by pack_cache_ or by own below
        VkImageView repl = VK_NULL_HANDLE;
        uint32_t repl_w = 0, repl_h = 0, repl_levels = 1;
        uint64_t pack_hash = 0;  // the contents' hash a texture pack names it by, 0 if it cannot be in one
        // a game's own picture for this linear texture (g_builtin_contents), with the contents' hash it was made for:
        // drawn again while the game uploads the same contents (ROTK's menu pictures, every frame)
        Image own;
        uint32_t own_w = 0, own_h = 0;
        uint64_t own_hash = 0;
    };
    // a texture pack file loaded, or a failed load remembered (no image)
    struct PackTex {
        Image img;
        uint32_t w = 0, h = 0;
    };
    struct StageBind {
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool target = false;
        uint32_t width = 1, height = 1;
        uint32_t host_w = 1, host_h = 1;  // the bound image's own size: a replacement's may differ from the console's
        uint32_t levels = 1;              // the bound image's mip levels
        float wide = 0;                   // kTexWide: host texels per console texel of an upscaled target shrunk
        float sharp = 0;                  // kTexSharp: HUD sharpening's strength
    };
    // S15 (D15): the shaders by their SPIR-V cache hash, so a key means the same pipeline in a later run (warm-up)
    struct PipeKey {
        uint64_t vs, ps;
        uint32_t regs, buffers, strides[16], elements[16];
        uint32_t topo, strip, has_rt, has_ds;
        uint32_t blend, src, dst, op, wmask;
        uint32_t zen, zwrite, zfunc, sten, sfunc, smask, swmask, sfail, szfail, spass;
        uint32_t cull, ccw, fill;
        int32_t zbias;
        float zslope;
        uint32_t samples, a2c;  // S14: anti-aliased targets; alpha to coverage for a cut-out (g_alpha_edges)
    };
    static_assert(sizeof(PipeKey) == 256, "PipeKey has no padding (its bytes are hashed and saved)");
    VkShaderModule ShaderModule(uint64_t key, const std::string& hlsl, spirv::Stage stage, const char* entry = "main");
    void BindTexture(const State& s, int i, Target* rt, Target* ds, StageBind& sb, TexInfo& info);
    VkImageView GuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi, StageBind& sb);
    bool UploadTexture(Tex& tx, const uint8_t* p, const uint32_t* colours);  // false: not all of it (tried again)
    // texture packs, as D3D12's: textures/ beside the exe holds replacements named by content hash, textures/dump/
    // what texture_dump writes; the game's own replacements come through pack::g_builtin and friends
    void TexturePack(Tex& tx, const uint8_t* p, const uint32_t* colours, const std::vector<std::vector<uint8_t>>& levels);
    bool LoadPackTexture(const std::filesystem::path& file, PackTex& out);
    bool CreatePackTexture(const pack::Image& img, const std::string& name, Image& out);
    pack::Library pack_lib_;
    std::unordered_map<uint64_t, PackTex> pack_cache_;
    std::unordered_set<uint64_t> dumped_;
    // a stage's sampler as Direct3D 12's StageSampler and EnsureStageSamplers make it (S5): clamp the draw's
    // coordinates all in 0..1; extra the console_textures mip bias; enlarge_smooth above 1.2 x 480 lines in a 3D draw
    VkSampler StageSampler(const State& s, int i, const TexInfo& info, const StageBind& sb, bool clamp, float extra,
                           bool enlarge_smooth);
    VkPipeline Pipeline(const PipeKey& k, VkShaderModule vs, VkShaderModule ps);
    // the pipeline for k alone (no maps): the draw thread's misses and the warm-up thread's keys
    VkPipeline MakePipeline(const PipeKey& k, VkShaderModule vs, VkShaderModule ps);
    // S15: SPIR-V kept in the shader cache by content (D20); pipelines listed in vk_pipelines.bin as they are made
    // and made again at the next start on a warm-up thread; the driver's VkPipelineCache saved beside them
    std::unordered_map<VkShaderModule, uint64_t> module_hash_;
    void StartWarmUp();
    void WarmUp(std::vector<PipeKey> keys, std::vector<SeedSrc> seed);
    void SavePipelineCache();
    std::thread warm_;
    std::atomic<bool> warm_stop_{false};
    std::mutex warm_mu_;
    std::unordered_map<uint64_t, VkPipeline> warm_pipes_;  // made by the warm-up, not yet taken
    std::unordered_set<uint64_t> warm_taken_;              // asked for by a draw (the warm-up skips them)
    FILE* pipe_list_ = nullptr;
    std::unordered_set<uint64_t> listed_;
    uint32_t made_drawing_ = 0, made_warm_ = 0, unsaved_ = 0;
    uint64_t saved_at_ = 0;
    // F3: a draw's line in frame.txt (S2)
    void CaptureDraw(const State& s, uint32_t prim, uint32_t count, const PipeKey& k, const DrawConstants& c, Target* ds,
                     const StageBind sb[4], const TexInfo info[4]);

    std::string error_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VmaAllocator vma_ = VK_NULL_HANDLE;  // images' memory (S3: vk_mem.h)
    // the GPU's progress (S12): a timeline semaphore every submit signals with the next serial_
    VkSemaphore progress_ = VK_NULL_HANDLE;
    std::atomic<uint64_t> serial_{0};
    uint64_t Done() const {
        uint64_t v = 0;
        if (progress_ && vkGetSemaphoreCounterValue) vkGetSemaphoreCounterValue(device_, progress_, &v);
        return v;
    }
    struct Late {
        VkBuffer buf[kFrames] = {};
        VmaAllocation mem[kFrames] = {};
        uint8_t* cpu[kFrames] = {};
        uint64_t value[kFrames] = {};  // the serial its copy finishes with, 0 none
        uint64_t frame[kFrames] = {};  // presents_ when it was recorded
        uint32_t next = 0, gw = 0, gh = 0;
        uint64_t delivered = 0;
    };
    std::unordered_map<uint32_t, Late> late_;
    std::mutex late_lock_;                     // DeliverLate runs on the title's thread
    std::atomic<uint64_t> presents_seen_{0};  // presents_ for DeliverLate
    void FreeLate(Late& l);
    uint32_t sampler_cap_ = 3000;        // draws' samplers at most: the device's limit, less some for the passes
    void ReleaseOldTextures();
    // an image's memory from the allocator, bound to it; false when there is none
    bool AllocImage(VkImage image, VmaAllocation& alloc);
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    bool dynamic_rendering_ = false;  // VK 1.3 or VK_KHR_dynamic_rendering; else render passes (for later phases)
    bool validate_ = false;           // XBE_VK_VALIDATE: the validation layer is on (test runs)
    bool debug_utils_ = false;        // VK_EXT_debug_utils enabled (object names, the messenger)
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                                                      VkDebugUtilsMessageTypeFlagsEXT type,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* d, void* user);
    bool timeline_ = false;           // VK 1.2 timeline semaphores

    VkSwapchainKHR swap_ = VK_NULL_HANDLE;
    VkFormat swap_fmt_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    bool vsync_ = true;
    // S11 HDR
    bool colorspace_ext_ = false, hdr_ = false, hdr_wanted_ = false;
    VkFormat ui_fmt_ = VK_FORMAT_UNDEFINED;  // the format the menu's pipeline was made for
    VkColorSpaceKHR swap_cs_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    float hdr_paper_ = 200.0f / 80.0f;  // normal scenes and the HUD at 200 nits, in scRGB units (as Direct3D 12)
    float hdr_peak_ = 600.0f / 80.0f;   // the screen's peak, read from it
    static bool HdrScreen(HWND hwnd, float& max_nits);
    // the screen pass's hdr row (Direct3D 12's: on, paper white, peak over paper), for the swapchain blits only
    void HdrConstants(float c[32]) const {
        c[12] = hdr_ ? 1.0f : 0.0f;
        c[13] = hdr_paper_;
        c[14] = hdr_peak_ / hdr_paper_;
    }
    bool MakeUiBackend();
    bool bias_unorm_ = false;  // VK_EXT_depth_bias_control's forced 24-bit unit (D16)
    bool present_wait_ = false, device_fault_ = false;
    uint64_t present_id_ = 0;  // the last present's id (VK_KHR_present_id)
    uint64_t swap_first_id_ = 0;  // the first id presented on this swapchain (0: none yet)
    static bool WantVsync() { return app::Get().vsync && !app::FastForward(); }
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;  // the menu draws into them
    std::vector<VkSemaphore> rendered_;  // one per swapchain image: presentation waits on it

    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence done = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkBuffer upload = VK_NULL_HANDLE;  // Upload's ring
        VkDeviceMemory upload_mem = VK_NULL_HANDLE;
        uint8_t* upload_cpu = nullptr;
        VkDeviceSize used = 0;
        VkDescriptorPool sets = VK_NULL_HANDLE;
        uint32_t sets_used = 0;      // taken from `sets` since its reset
        // S16 (D19): sets by their images and samplers (the constants are dynamic offsets), reused within the frame
        std::unordered_map<uint64_t, VkDescriptorSet> set_cache;
        std::vector<Image> garbage;  // destroyed when the slot comes round again
        // D11: more rings and set pools when a frame fills the first (as Direct3D 12's overflow buffers), so a busy
        // frame goes on without waiting for the GPU; upload/upload_mem/upload_cpu and sets are the ones in use
        struct Ring {
            VkBuffer buf = VK_NULL_HANDLE;
            VkDeviceMemory mem = VK_NULL_HANDLE;
            uint8_t* cpu = nullptr;
        };
        std::vector<Ring> rings;  // [0] the first
        uint32_t ring = 0;
        std::vector<VkDescriptorPool> pools;  // [0] the first
        uint32_t pool_i = 0;
        uint32_t idle = 0;  // slot uses in a row that needed no extra ring or pool
    };
    bool MakeRing(Frame::Ring& r);
    bool MakePool(VkDescriptorPool& p);
    // room for a draw (or the screen's passes): a fuller ring or set pool than `sets` allows moves on to the next one,
    // made when needed; only when one can't be made does the frame finish and wait (as before D11)
    void Room(uint32_t sets);
    static constexpr VkDeviceSize kUploadSize = 64ull << 20;
    static constexpr VkDeviceSize kUploadReserve = 16ull << 20;
    // (The Third Age draws ~13,600 times a frame: 8192 ran out and the rest of the frame was dropped, textures
    // "randomly dropping out"; a full pool now finishes the frame's work and starts over, as a full upload ring does)
    static constexpr uint32_t kSetsPerFrame = 32768;
    Frame frames_[kFrames];
    uint32_t frame_ = 0;
    bool lost_ = false;
    bool recording_ = false;  // frames_[frame_].cmd is open

    VkPhysicalDeviceMemoryProperties mem_props_{};
    VkFormat depth_fmt_ = VK_FORMAT_D24_UNORM_S8_UINT;
    std::unordered_map<uint64_t, Target> targets_;  // by console address << 1 | depth
    Target* render_rt_ = nullptr;  // what the open dynamic rendering draws into
    Target* render_ds_ = nullptr;
    bool render_ms_ = false;
    bool rendering_ = false;
    uint64_t presents_ = 0;
    // render scale ([display] render_percent of the window's height), wider by xmul_ for a game with wide targets
    float scale_ = 1.0f, xmul_ = 1.0f;
    uint32_t guest_w_ = 640, guest_h_ = 480;
    void PickScale();
    bool ScaleChanged();
    uint32_t scratch_w_ = 0, scratch_h_ = 0;
    Image scratch_;  // console sized: read-backs shrink into it, CPU pixels are enlarged from it
    bool EnsureScratch(uint32_t w, uint32_t h);

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipe_layout_ = VK_NULL_HANDLE;
    VkPipelineCache pipe_cache_ = VK_NULL_HANDLE;
    VkPipelineLayout clear_layout_ = VK_NULL_HANDLE;  // MaskedClear's: the colour as a push constant
    VkPipeline clear_pipes_[32] = {};                 // by colour write mask, + 16 multisampled
    VkDeviceSize ubo_align_ = 256;
    bool anisotropy_ = false;  // the card's samplerAnisotropy, asked for at device creation
    bool fill_lines_ = false;  // fillModeNonSolid: wireframe
    Image dummy_[4];           // 2D, cube, 3D, depth
    VkBuffer readback_ = VK_NULL_HANDLE;  // Sync's, host visible, grown as needed
    VkDeviceMemory readback_mem_ = VK_NULL_HANDLE;
    uint8_t* readback_cpu_ = nullptr;
    VkDeviceSize readback_size_ = 0;
    VkSampler plain_sampler_ = VK_NULL_HANDLE, compare_sampler_ = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, VkShaderModule> modules_;
    std::unordered_map<uint64_t, VkPipeline> pipelines_;
    std::unordered_map<uint64_t, VkSampler> samplers_;
    std::unordered_map<uint64_t, Tex> textures_;
    Assembled asm_;
};

constexpr VkFormat kColorFormat = VK_FORMAT_B8G8R8A8_UNORM;  // the console's A8R8G8B8, as D3D12's B8G8R8A8

uint64_t TargetKey(uint32_t data, bool depth) { return uint64_t(data) << 1 | (depth ? 1 : 0); }

// a surface hashed whole up to 64 KB, a bigger one as 64 evenly spaced samples of 64 bytes
uint64_t MemSig(uint32_t data, uint32_t total) {
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(data));
    uint64_t sig = Hash(&total, sizeof(total));
    if (total <= 65536) return Hash(p, total, sig);
    for (uint32_t i = 0; i < 64; i++) sig = Hash(p + uint64_t(total - 64) * i / 63, 64, sig);
    return sig;
}

// ---- S15: the shader cache's SPIR-V, the pipeline list, seeds

// a compiled shader's file, by source, entry point and stage; a new compiler or legaliser takes a new salt
constexpr char kSpvSalt[] = "spv glslang-16.6.0 spirv-tools-legalise v1";
uint64_t SpvHash(const std::string& src, const char* entry, spirv::Stage stage) {
    uint64_t seed = Hash(entry, std::strlen(entry), stage == spirv::Stage::Vertex ? 0x5653ull : 0x5053ull);
    return Hash(src.data(), src.size(), seed);
}

std::filesystem::path SpvPath(uint64_t hash) {
    return app::ShaderCacheDir() / (pack::Name(Hash(kSpvSalt, sizeof(kSpvSalt), hash)) + ".spv");
}

// whole SPIR-V words with the magic number first; anything else is compiled again
bool ReadSpv(uint64_t hash, std::vector<uint32_t>& code) {
    FILE* f = _wfopen(SpvPath(hash).c_str(), L"rb");
    if (!f) return false;
    bool ok = false;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        long n = std::ftell(f);
        if (n >= 20 && n % 4 == 0 && n < (64l << 20)) {
            code.resize(size_t(n) / 4);
            std::fseek(f, 0, SEEK_SET);
            ok = std::fread(code.data(), 4, code.size(), f) == code.size() && code[0] == 0x07230203u;
        }
    }
    std::fclose(f);
    if (!ok) code.clear();
    return ok;
}

// through a temporary file, so another thread (the seed's) never reads half of one
void WriteFileAtomic(const std::filesystem::path& file, const void* data, size_t size) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    static std::atomic<uint32_t> seq{0};
    std::filesystem::path tmp = file;
    tmp += ".tmp" + std::to_string(GetCurrentThreadId()) + "_" + std::to_string(seq++);
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return;
    bool ok = std::fwrite(data, 1, size, f) == size;
    ok = std::fclose(f) == 0 && ok;
    if (ok) std::filesystem::rename(tmp, file, ec);
    if (!ok || ec) std::filesystem::remove(tmp, ec);
}

void WriteSpv(uint64_t hash, const std::vector<uint32_t>& code) {
    WriteFileAtomic(SpvPath(hash), code.data(), code.size() * 4);
}

bool ReadSeedString(FILE* f, std::string& out) {
    uint32_t n = 0;
    if (std::fread(&n, 4, 1, f) != 1 || n > (16u << 20)) return false;
    out.resize(n);
    return n == 0 || std::fread(out.data(), 1, n, f) == n;
}

std::vector<SeedSrc> ReadShaderSeed(const std::filesystem::path& file) {
    std::vector<SeedSrc> v;
    FILE* f = _wfopen(file.c_str(), L"rb");
    if (!f) return v;
    char magic[4];
    if (std::fread(magic, 4, 1, f) == 1 && !std::memcmp(magic, "ASS1", 4)) {
        SeedSrc s;
        while (ReadSeedString(f, s.entry) && ReadSeedString(f, s.profile) && ReadSeedString(f, s.src))
            if (!s.profile.compare(0, 3, "vs_") || !s.profile.compare(0, 3, "ps_")) v.push_back(s);
    }
    std::fclose(f);
    return v;
}

// vk_pipelines.bin: a header, then PipeKeys as Pipeline made them (no padding: equal keys are equal bytes)
struct PipeListHeader {
    char magic[4] = {'V', 'P', 'L', '1'};
    uint32_t key_size = 0;
};

bool VkBackend::Init(uint32_t width, uint32_t height) {
    guest_w_ = width;
    guest_h_ = height;
    PickScale();
    const char* why = nullptr;
    if (!OpenLibrary(&why)) return Fail(why ? why : "no Vulkan loader");
    if (!CreateInstance() || !CreateSurface() || !PickDevice() || !CreateDevice() || !CreateSwapchain() ||
        !CreateFrames() || !PickDepthFormat() || !CreateDrawResources() || !InitUi()) {
        Destroy();  // nothing left holding the window, so Direct3D 12 can take it
        return false;
    }
    g_colour_look = std::min(app::Get().colour_look, kColourLooks - 1);  // (as Direct3D 12's LoadSettings)
    samples_ = PickSamples();
    StartWarmUp();
    if (app::Get().texture_replace) pack_lib_.Scan(app::TextureDir(), "dump");
    if (pack_lib_.Size()) LOGR("texture pack: %zu replacement files under %s", pack_lib_.Size(), app::TextureDir().string().c_str());
    if (app::Get().texture_dump) LOGR("texture pack: dumping textures to %s", (app::TextureDir() / "dump").string().c_str());
    LOGR("vulkan: ready");
    if (app::Get().upscaler) LOGR("vulkan: the upscaler setting is ignored (FSR, DLSS and XeSS are Direct3D 12 only)");
    return true;
}

bool VkBackend::CreateInstance() {
    uint32_t version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&version);
    if (version < VK_API_VERSION_1_2)
        return Fail("the Vulkan loader is " + std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
                    std::to_string(VK_API_VERSION_MINOR(version)) + ", 1.2 is needed");
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> ext(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, ext.data());
    ext.resize(n);
    for (const char* need : {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME})
        if (!HasExtension(ext, need)) return Fail(std::string("the instance has no ") + need);
    std::vector<const char*> names = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    // S11: the HDR colour space (scRGB, EXTENDED_SRGB_LINEAR) is offered only with this extension
    colorspace_ext_ = HasExtension(ext, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
    if (colorspace_ext_) names.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
    // Test runs: XBE_VK_VALIDATE=1 turns on the Khronos validation layer (the Vulkan SDK's, when installed) and sends
    // its messages to the log; =sync adds synchronisation checks (missing barriers: what NVIDIA forgives and AMD,
    // Intel and phones don't), =gpu GPU-assisted checks (slow). Players never set it.
    const char* want_validate = std::getenv("XBE_VK_VALIDATE");
    std::vector<const char*> layers;
    VkValidationFeaturesEXT vf{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    static VkValidationFeatureEnableEXT vf_on[2];
    if (want_validate && *want_validate && std::strcmp(want_validate, "0")) {
        uint32_t ln = 0;
        vkEnumerateInstanceLayerProperties(&ln, nullptr);
        std::vector<VkLayerProperties> lp(ln);
        vkEnumerateInstanceLayerProperties(&ln, lp.data());
        lp.resize(ln);
        bool have = false;
        for (const VkLayerProperties& l : lp) have = have || !std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation");
        if (!have) LOGR("vulkan: XBE_VK_VALIDATE set, but the validation layer isn't installed (the Vulkan SDK)");
        else {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            validate_ = true;
            uint32_t k = 0;
            if (!std::strcmp(want_validate, "sync")) vf_on[k++] = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
            if (!std::strcmp(want_validate, "gpu")) vf_on[k++] = VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT;
            vf.enabledValidationFeatureCount = k;
            vf.pEnabledValidationFeatures = vf_on;
            LOGR("vulkan: validation layer on (%s)", want_validate);
        }
    }
    // debug utils (the layer brings it when validating; otherwise only if the loader lists it): object names for
    // RenderDoc, and the messenger
    if (validate_ || HasExtension(ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        names.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        debug_utils_ = true;
    }
    VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = app::kGame.log;
    app_info.pEngineName = "taxbox";
    app_info.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app_info;
    ci.enabledExtensionCount = uint32_t(names.size());
    ci.ppEnabledExtensionNames = names.data();
    ci.enabledLayerCount = uint32_t(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    if (validate_ && vf.enabledValidationFeatureCount) ci.pNext = &vf;
    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS) return Fail("vkCreateInstance", r);
    LoadInstance(instance_);
    if (!vkCreateWin32SurfaceKHR || !vkEnumeratePhysicalDevices || !vkGetDeviceProcAddr)
        return Fail("the instance functions could not be loaded");
    if (validate_ && vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = &VkBackend::DebugMessage;
        if (vkCreateDebugUtilsMessengerEXT(instance_, &mi, nullptr, &messenger_) != VK_SUCCESS) messenger_ = VK_NULL_HANDLE;
    }
    return true;
}

// the validation layer's messages: each kind (its message ID) logged the first 5 times, then counted
VKAPI_ATTR VkBool32 VKAPI_CALL VkBackend::DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                                                      VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* d,
                                                      void*) {
    static std::mutex mu;
    static std::unordered_map<int32_t, uint32_t> seen;
    std::lock_guard<std::mutex> lock(mu);
    uint32_t& n = seen[d ? d->messageIdNumber : 0];
    if (++n > 5) return VK_FALSE;
    bool err = sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    const char* msg = d && d->pMessage ? d->pMessage : "";
    if (err) LOGE("vulkan validation: %s%s", msg, n == 5 ? " (this kind no longer logged)" : "");
    else LOGW("vulkan validation: %s%s", msg, n == 5 ? " (this kind no longer logged)" : "");
    return VK_FALSE;
}

bool VkBackend::CreateSurface() {
    VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    ci.hinstance = ::GetModuleHandleW(nullptr);
    ci.hwnd = static_cast<HWND>(app::Window());
    if (!ci.hwnd) return Fail("there is no game window");
    VkResult r = vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_);
    if (r != VK_SUCCESS) return Fail("vkCreateWin32SurfaceKHR", r);
    return true;
}

bool VkBackend::PickDevice() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    std::vector<VkPhysicalDevice> list(n);
    if (n) vkEnumeratePhysicalDevices(instance_, &n, list.data());
    list.resize(n);
    if (list.empty()) return Fail("no Vulkan graphics card");
    int best_score = -1;
    std::string skipped;
    for (VkPhysicalDevice d : list) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        auto skip = [&](const char* why) {
            LOGR("vulkan: %s skipped: %s", p.deviceName, why);
            if (skipped.empty()) skipped = std::string(p.deviceName) + ": " + why;
        };
        if (p.apiVersion < VK_API_VERSION_1_2) {
            skip("Vulkan 1.2 is needed");
            continue;
        }
        uint32_t en = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &en, nullptr);
        std::vector<VkExtensionProperties> ext(en);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &en, ext.data());
        ext.resize(en);
        if (!HasExtension(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            skip("no swapchain");
            continue;
        }
        // every draw is recorded with dynamic rendering (render passes aren't written): a card without it would show
        // a black screen, so it is skipped and Direct3D 12 (or the clear message) takes over (review D1)
        if (!HasExtension(ext, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
            skip("no dynamic rendering (VK_KHR_dynamic_rendering)");
            continue;
        }
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qf.data());
        int family = -1;
        for (uint32_t i = 0; i < qn && family < 0; i++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
            if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) family = int(i);
        }
        if (family < 0) {
            skip("no queue that draws and presents to the game window");
            continue;
        }
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU            ? 0
                                                                              : 1;
        if (score > best_score) {
            best_score = score;
            gpu_ = d;
            family_ = uint32_t(family);
        }
    }
    if (!gpu_) return Fail(skipped.empty() ? std::string("no usable graphics card") : skipped);
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(gpu_, &p);
    LOGR("vulkan: %s, Vulkan %u.%u.%u, driver %08X", p.deviceName, VK_API_VERSION_MAJOR(p.apiVersion),
         VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion), p.driverVersion);
    // the driver's limits on separate memory allocations and samplers (S3: D2, D14)
    sampler_cap_ = std::clamp<uint32_t>(p.limits.maxSamplerAllocationCount > 64 ? p.limits.maxSamplerAllocationCount - 64 : 64,
                                        64, 3000);
    LOGR("vulkan: limits: %u memory allocations, %u samplers (%u kept for draws)", p.limits.maxMemoryAllocationCount,
         p.limits.maxSamplerAllocationCount, sampler_cap_);
    return true;
}

bool VkBackend::CreateDevice() {
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(gpu_, &p);
    uint32_t en = 0;
    vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &en, nullptr);
    std::vector<VkExtensionProperties> ext(en);
    vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &en, ext.data());
    ext.resize(en);

    // what the card has: timeline semaphores (1.2 core) and dynamic rendering (1.3 core or the KHR extension)
    VkPhysicalDeviceDynamicRenderingFeatures dr_have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    VkPhysicalDeviceVulkan12Features v12_have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    bool dr_ext = HasExtension(ext, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    bool dr_core = p.apiVersion >= VK_API_VERSION_1_3;
    v12_have.pNext = (dr_ext || dr_core) ? &dr_have : nullptr;
    // S18 (D16): depth bias in 24-bit steps on a float depth buffer (AMD's D32S8), as Direct3D 12 counts it
    VkPhysicalDeviceDepthBiasControlFeaturesEXT bias_have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT};
    bool bias_ext = HasExtension(ext, VK_EXT_DEPTH_BIAS_CONTROL_EXTENSION_NAME);
    if (bias_ext) {
        bias_have.pNext = v12_have.pNext;
        v12_have.pNext = &bias_have;
    }
    // S17: present_id + present_wait (pacing as Direct3D 12's latency 2), device_fault (what a lost device says)
    VkPhysicalDevicePresentIdFeaturesKHR pid_have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR pw_have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    VkPhysicalDeviceFaultFeaturesEXT fault_have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    bool pace_ext = HasExtension(ext, VK_KHR_PRESENT_ID_EXTENSION_NAME) && HasExtension(ext, VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
    bool fault_ext = HasExtension(ext, VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
    if (pace_ext) {
        pw_have.pNext = v12_have.pNext;
        pid_have.pNext = &pw_have;
        v12_have.pNext = &pid_have;
    }
    if (fault_ext) {
        fault_have.pNext = v12_have.pNext;
        v12_have.pNext = &fault_have;
    }
    VkPhysicalDeviceFeatures2 have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    have.pNext = &v12_have;
    if (vkGetPhysicalDeviceFeatures2) vkGetPhysicalDeviceFeatures2(gpu_, &have);
    // a 1.3 device on our 1.2 instance: the extension is the safe way to ask for it
    dynamic_rendering_ = dr_ext && dr_have.dynamicRendering;
    timeline_ = v12_have.timelineSemaphore;

    VkPhysicalDeviceDynamicRenderingFeatures dr_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    dr_on.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceVulkan12Features v12_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    v12_on.timelineSemaphore = timeline_ ? VK_TRUE : VK_FALSE;
    v12_on.pNext = dynamic_rendering_ ? &dr_on : nullptr;
    bias_unorm_ = bias_ext && bias_have.depthBiasControl && bias_have.leastRepresentableValueForceUnormRepresentation;
    VkPhysicalDeviceDepthBiasControlFeaturesEXT bias_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT};
    if (bias_unorm_) {
        bias_on.depthBiasControl = VK_TRUE;
        bias_on.leastRepresentableValueForceUnormRepresentation = VK_TRUE;
        bias_on.pNext = v12_on.pNext;
        v12_on.pNext = &bias_on;
    }
    present_wait_ = pace_ext && pid_have.presentId && pw_have.presentWait;  // (and the function, after LoadDevice)
    VkPhysicalDevicePresentIdFeaturesKHR pid_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR pw_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    if (present_wait_) {
        pid_on.presentId = VK_TRUE;
        pw_on.presentWait = VK_TRUE;
        pw_on.pNext = v12_on.pNext;
        pid_on.pNext = &pw_on;
        v12_on.pNext = &pid_on;
    }
    device_fault_ = fault_ext && fault_have.deviceFault;
    VkPhysicalDeviceFaultFeaturesEXT fault_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    if (device_fault_) {
        fault_on.deviceFault = VK_TRUE;
        fault_on.pNext = v12_on.pNext;
        v12_on.pNext = &fault_on;
    }
    VkPhysicalDeviceFeatures2 on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    on.pNext = &v12_on;
    anisotropy_ = have.features.samplerAnisotropy;
    fill_lines_ = have.features.fillModeNonSolid;
    on.features.samplerAnisotropy = have.features.samplerAnisotropy;
    on.features.fillModeNonSolid = have.features.fillModeNonSolid;
    // exact counts, as Direct3D 12's occlusion queries (without it a count may be any number above 0 when visible)
    precise_ = have.features.occlusionQueryPrecise;
    on.features.occlusionQueryPrecise = have.features.occlusionQueryPrecise;
    ubo_align_ = std::max<VkDeviceSize>(p.limits.minUniformBufferOffsetAlignment, 16);

    std::vector<const char*> names = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (dynamic_rendering_) names.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    if (bias_unorm_) names.push_back(VK_EXT_DEPTH_BIAS_CONTROL_EXTENSION_NAME);
    if (present_wait_) {
        names.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
        names.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
    }
    if (device_fault_) names.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = vkGetPhysicalDeviceFeatures2 ? &on : nullptr;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = uint32_t(names.size());
    ci.ppEnabledExtensionNames = names.data();
    VkResult r = vkCreateDevice(gpu_, &ci, nullptr, &device_);
    if (r != VK_SUCCESS) return Fail("vkCreateDevice", r);
    LoadDevice(device_);
    if (!vkWaitForPresentKHR) present_wait_ = false;  // listed but not loaded (a layer or loader mismatch)
    if (!vkCreateSwapchainKHR || !vkQueueSubmit || !vkCmdClearColorImage || !vkCmdPipelineBarrier)
        return Fail("the device functions could not be loaded");
    vkGetDeviceQueue(device_, family_, 0, &queue_);
    vma_ = vk::CreateAllocator(instance_, gpu_, device_);
    if (!vma_) return Fail("the memory allocator could not be made");
    LOGR("vulkan: dynamic rendering %s, timeline semaphores %s", dynamic_rendering_ ? "on" : "off (render passes)",
         timeline_ ? "on" : "off");
    return true;
}

// the window's screen shows HDR (Windows HDR on): max_nits its peak brightness (Direct3D 12's HdrScreen, through DXGI)
bool VkBackend::HdrScreen(HWND hwnd, float& max_nits) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&f)))) return false;
    bool hdr = false, found = false;
    IDXGIAdapter1* ad = nullptr;
    for (UINT a = 0; !found && f->EnumAdapters1(a, &ad) != DXGI_ERROR_NOT_FOUND; a++) {
        IDXGIOutput* out = nullptr;
        for (UINT o = 0; !found && ad->EnumOutputs(o, &out) != DXGI_ERROR_NOT_FOUND; o++) {
            DXGI_OUTPUT_DESC od{};
            out->GetDesc(&od);
            if (od.Monitor == mon) {
                found = true;
                IDXGIOutput6* o6 = nullptr;
                DXGI_OUTPUT_DESC1 d1{};
                if (SUCCEEDED(out->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&o6)))) {
                    if (SUCCEEDED(o6->GetDesc1(&d1))) {
                        max_nits = d1.MaxLuminance;
                        hdr = d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                    }
                    o6->Release();
                }
            }
            out->Release();
        }
        ad->Release();
    }
    f->Release();
    return hdr;
}

bool VkBackend::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps);
    if (r == VK_ERROR_SURFACE_LOST_KHR && !::IsWindow(static_cast<HWND>(app::Window()))) {
        // the window closed while a frame was on its way (the game is exiting): nothing to show it in, not an error
        DestroySwapchain();
        extent_ = {0, 0};
        return true;
    }
    if (r != VK_SUCCESS) return Fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", r);
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {
        RECT rc{};
        ::GetClientRect(static_cast<HWND>(app::Window()), &rc);
        ext = {uint32_t(std::max<LONG>(rc.right - rc.left, 1)), uint32_t(std::max<LONG>(rc.bottom - rc.top, 1))};
        ext.width = std::clamp(ext.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (ext.width == 0 || ext.height == 0) {  // minimised: none until the next Present finds a size
        DestroySwapchain();
        extent_ = ext;
        return true;
    }
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return Fail("the swapchain images can't be cleared (no transfer destination use)");

    // S11, as Direct3D 12: [graphics] hdr on a screen Windows shows in HDR gives an FP16 scRGB swapchain (linear,
    // 1.0 = 80 nits), the screen pass raising highlights; otherwise, or when the card offers no such format, the
    // normal picture
    hdr_wanted_ = app::Get().hdr;
    float nits = 0;
    bool hdr = hdr_wanted_ && colorspace_ext_ && HdrScreen(static_cast<HWND>(app::Window()), nits);
    {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(n);
        if (n) vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, fmts.data());
        fmts.resize(n);
        if (fmts.empty()) return Fail("the window has no surface formats");
        swap_fmt_ = fmts[0].format;
        swap_cs_ = fmts[0].colorSpace;
        for (const VkSurfaceFormatKHR& f : fmts)
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                swap_fmt_ = f.format;
                swap_cs_ = f.colorSpace;
                break;
            }
        bool found = false;
        if (hdr)
            for (const VkSurfaceFormatKHR& f : fmts)
                if (f.format == VK_FORMAT_R16G16B16A16_SFLOAT && f.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
                    swap_fmt_ = f.format;
                    swap_cs_ = f.colorSpace;
                    found = true;
                    break;
                }
        if (hdr && !found) LOG_FIRST(1, "vulkan: HDR could not be set up on this screen (no scRGB format): the normal picture");
        if (hdr_wanted_ && !hdr) LOG_FIRST(1, "vulkan: HDR is on in the settings, but this screen has HDR off (or none): the normal picture");
        bool was = hdr_;
        hdr_ = hdr && found;
        if (hdr_) {
            // a screen that reports less than twice paper white still gets some headroom; very bright ones are held
            // to 1000 nits, past which the 2003 picture only glares (Direct3D 12's numbers); read again with every
            // swapchain, as the window may have moved to another screen (review)
            float peak = std::clamp(nits, 400.0f, 1000.0f);
            hdr_peak_ = peak / 80.0f;
            if (!was)
                LOGR("vulkan: HDR on (screen peak %.0f nits, highlights up to %.0f, normal scenes at %.0f)", nits, peak,
                     hdr_paper_ * 80.0f);
        }
    }

    vsync_ = WantVsync();
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // always there
    if (!vsync_) {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, nullptr);
        std::vector<VkPresentModeKHR> modes(n);
        if (n) vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, modes.data());
        modes.resize(n);
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
            mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
            mode = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    uint32_t count = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount) count = std::min(count, caps.maxImageCount);
    VkSwapchainKHR old = swap_;
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = count;
    ci.imageFormat = swap_fmt_;
    ci.imageColorSpace = swap_cs_;
    ci.imageExtent = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                          : caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
        for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                              VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
            if (caps.supportedCompositeAlpha & a) {
                ci.compositeAlpha = a;
                break;
            }
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    VkSwapchainKHR made = VK_NULL_HANDLE;
    r = vkCreateSwapchainKHR(device_, &ci, nullptr, &made);
    DestroySwapchain();  // the old one (retired by oldSwapchain either way) and its per-image semaphores
    if (r != VK_SUCCESS) return Fail("vkCreateSwapchainKHR", r);
    swap_ = made;
    swap_first_id_ = 0;  // (ids presented on the old swapchain are not this one's to wait for)
    extent_ = ext;

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(device_, swap_, &n, nullptr);
    images_.resize(n);
    vkGetSwapchainImagesKHR(device_, swap_, &n, images_.data());
    images_.resize(n);
    rendered_.assign(n, VK_NULL_HANDLE);
    image_views_.assign(n, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swap_fmt_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        r = vkCreateImageView(device_, &vi, nullptr, &image_views_[i]);
        if (r != VK_SUCCESS) return Fail("vkCreateImageView (swapchain)", r);
    }
    if (ui_) ImGui_ImplVulkan_SetMinImageCount(std::max(caps.minImageCount, 2u));
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore& s : rendered_) {
        r = vkCreateSemaphore(device_, &si, nullptr, &s);
        if (r != VK_SUCCESS) return Fail("vkCreateSemaphore", r);
    }
    LOGI("vulkan: swapchain %ux%u, %u images, %s", ext.width, ext.height, n,
         mode == VK_PRESENT_MODE_FIFO_KHR ? "v-sync" : mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "immediate");
    // HDR switched in the menu: the menu's pipeline made again for the new format (the callers waited for the GPU)
    if (ui_ && ui_fmt_ != swap_fmt_) {
        std::lock_guard ui_lock(app::UiLock());  // (the window thread uses the same ImGui context, review)
        ImGui_ImplVulkan_Shutdown();
        ui_ = false;
        if (!MakeUiBackend()) LOGE("vulkan: the menu could not be set up for the new screen format: %s", error_.c_str());
    }
    return true;
}

// the swapchain and its semaphores; the device must be idle
void VkBackend::DestroySwapchain() {
    for (VkSemaphore s : rendered_)
        if (s) vkDestroySemaphore(device_, s, nullptr);
    rendered_.clear();
    for (VkImageView v : image_views_)
        if (v) vkDestroyImageView(device_, v, nullptr);
    image_views_.clear();
    images_.clear();
    if (swap_) vkDestroySwapchainKHR(device_, swap_, nullptr);
    swap_ = VK_NULL_HANDLE;
}

bool VkBackend::CreateFrames() {
    if (timeline_ && vkGetSemaphoreCounterValue) {
        VkSemaphoreTypeCreateInfo ti{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        ti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        si.pNext = &ti;
        if (vkCreateSemaphore(device_, &si, nullptr, &progress_) != VK_SUCCESS) progress_ = VK_NULL_HANDLE;
    }
    for (Frame& f : frames_) {
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pi.queueFamilyIndex = family_;
        VkResult r = vkCreateCommandPool(device_, &pi, nullptr, &f.pool);
        if (r != VK_SUCCESS) return Fail("vkCreateCommandPool", r);
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        r = vkAllocateCommandBuffers(device_, &ai, &f.cmd);
        if (r != VK_SUCCESS) return Fail("vkAllocateCommandBuffers", r);
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        r = vkCreateFence(device_, &fi, nullptr, &f.done);
        if (r != VK_SUCCESS) return Fail("vkCreateFence", r);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        r = vkCreateSemaphore(device_, &si, nullptr, &f.acquired);
        if (r != VK_SUCCESS) return Fail("vkCreateSemaphore", r);
    }
    return true;
}

void VkBackend::Destroy() {
    warm_stop_ = true;
    if (warm_.joinable()) warm_.join();
    if (pipe_list_) std::fclose(pipe_list_);
    pipe_list_ = nullptr;
    if (device_) {
        vkDeviceWaitIdle(device_);
        SavePipelineCache();
        for (auto& [key, pl] : warm_pipes_)
            if (pl) vkDestroyPipeline(device_, pl, nullptr);
        warm_pipes_.clear();
        if (ui_) ImGui_ImplVulkan_Shutdown();
        ui_ = false;
        if (ui_pool_) vkDestroyDescriptorPool(device_, ui_pool_, nullptr);
        ui_pool_ = VK_NULL_HANDLE;
        for (Frame& f : frames_) {
            if (f.acquired) vkDestroySemaphore(device_, f.acquired, nullptr);
            if (f.done) vkDestroyFence(device_, f.done, nullptr);
            if (f.pool) vkDestroyCommandPool(device_, f.pool, nullptr);  // frees its command buffer
            f.acquired = VK_NULL_HANDLE;
            f.done = VK_NULL_HANDLE;
            f.pool = VK_NULL_HANDLE;
            f.cmd = VK_NULL_HANDLE;
        }
        for (auto& [key, m] : modules_)
            if (m) vkDestroyShaderModule(device_, m, nullptr);
        modules_.clear();
        for (auto& [fmt, pl] : blit_pipes_)
            if (pl) vkDestroyPipeline(device_, pl, nullptr);
        blit_pipes_.clear();
        for (VkSampler& sm : blit_smp_) {
            if (sm) vkDestroySampler(device_, sm, nullptr);
            sm = VK_NULL_HANDLE;
        }
        for (auto& [key, pl] : pipelines_)
            if (pl) vkDestroyPipeline(device_, pl, nullptr);
        pipelines_.clear();
        for (auto& [key, sm] : samplers_) vkDestroySampler(device_, sm, nullptr);
        samplers_.clear();
        for (auto& [key, tx] : textures_) {
            DestroyImage(tx.img);
            DestroyImage(tx.own);
        }
        textures_.clear();
        for (auto& [key, pt] : pack_cache_) DestroyImage(pt.img);
        pack_cache_.clear();
        for (Image& im : dummy_) DestroyImage(im);
        DestroyImage(lut_);
        DestroyImage(shown_);
        {
            std::lock_guard lock(late_lock_);
            for (auto& [d, l] : late_) FreeLate(l);
            late_.clear();
        }
        if (progress_) vkDestroySemaphore(device_, progress_, nullptr);
        progress_ = VK_NULL_HANDLE;
        {
            std::lock_guard lock(query_lock_);
            if (query_pool_) vkDestroyQueryPool(device_, query_pool_, nullptr);
            query_pool_ = VK_NULL_HANDLE;
        }
        for (Image* im : {&smaa_area_, &smaa_search_, &smaa_edges_, &smaa_weights_, &smaa_out_}) DestroyImage(*im);
        for (auto& [key, pl] : pass_pipes_)
            if (pl) vkDestroyPipeline(device_, pl, nullptr);
        pass_pipes_.clear();
        for (MovieFrame& m : movie_ring_) DestroyImage(m.img);
        for (Image& im : cap_hold_) DestroyImage(im);
        cap_hold_.clear();
        if (cap_) fclose(cap_);
        cap_ = nullptr;
        DestroyImage(scratch_);
        if (readback_) vkDestroyBuffer(device_, readback_, nullptr);
        if (readback_mem_) vkFreeMemory(device_, readback_mem_, nullptr);
        readback_ = VK_NULL_HANDLE;
        readback_mem_ = VK_NULL_HANDLE;
        readback_cpu_ = nullptr;
        readback_size_ = 0;
        if (plain_sampler_) vkDestroySampler(device_, plain_sampler_, nullptr);
        if (compare_sampler_) vkDestroySampler(device_, compare_sampler_, nullptr);
        plain_sampler_ = compare_sampler_ = VK_NULL_HANDLE;
        if (pipe_cache_) vkDestroyPipelineCache(device_, pipe_cache_, nullptr);
        if (pipe_layout_) vkDestroyPipelineLayout(device_, pipe_layout_, nullptr);
        for (VkPipeline& cp : clear_pipes_) {
            if (cp) vkDestroyPipeline(device_, cp, nullptr);
            cp = VK_NULL_HANDLE;
        }
        for (VkPipeline& mp : ms_up_) {
            if (mp) vkDestroyPipeline(device_, mp, nullptr);
            mp = VK_NULL_HANDLE;
        }
        if (depth_copy_pipe_) vkDestroyPipeline(device_, depth_copy_pipe_, nullptr);
        depth_copy_pipe_ = VK_NULL_HANDLE;
        if (clear_layout_) vkDestroyPipelineLayout(device_, clear_layout_, nullptr);
        clear_layout_ = VK_NULL_HANDLE;
        if (set_layout_) vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
        pipe_cache_ = VK_NULL_HANDLE;
        pipe_layout_ = VK_NULL_HANDLE;
        set_layout_ = VK_NULL_HANDLE;
        for (Frame& f : frames_) {
            for (Image& im : f.garbage) DestroyImage(im);
            f.garbage.clear();
            for (VkDescriptorPool p : f.pools)
                if (p) vkDestroyDescriptorPool(device_, p, nullptr);
            for (Frame::Ring& r : f.rings) {
                if (r.buf) vkDestroyBuffer(device_, r.buf, nullptr);
                if (r.mem) vkFreeMemory(device_, r.mem, nullptr);  // unmapped with it
            }
            f.pools.clear();
            f.rings.clear();
            f.sets = VK_NULL_HANDLE;
            f.upload = VK_NULL_HANDLE;
            f.upload_mem = VK_NULL_HANDLE;
            f.upload_cpu = nullptr;
        }
        for (auto& [key, t] : targets_) DestroyTarget(t);
        targets_.clear();
        render_rt_ = render_ds_ = nullptr;
        rendering_ = recording_ = false;
        DestroySwapchain();
        if (vma_) vmaDestroyAllocator(vma_);
        vma_ = VK_NULL_HANDLE;
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (messenger_ && vkDestroyDebugUtilsMessengerEXT) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    messenger_ = VK_NULL_HANDLE;
    if (instance_) vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    gpu_ = VK_NULL_HANDLE;
}

// D24S8 as the console's depth buffer; AMD cards lack it as an attachment, D32S8 then
bool VkBackend::PickDepthFormat() {
    vkGetPhysicalDeviceMemoryProperties(gpu_, &mem_props_);
    for (VkFormat f : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(gpu_, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depth_fmt_ = f;
            LOGI("vulkan: depth buffers %s", f == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32S8");
            return true;
        }
    }
    return Fail("no depth and stencil format the card can draw into");
}

bool VkBackend::FindMemory(uint32_t bits, VkMemoryPropertyFlags want, uint32_t& index) const {
    for (uint32_t i = 0; i < mem_props_.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mem_props_.memoryTypes[i].propertyFlags & want) == want) {
            index = i;
            return true;
        }
    return false;
}

void VkBackend::DestroyTarget(Target& t) {
    frames_[frame_].set_cache.clear();  // (as DestroyImage)
    if (t.ms_view) vkDestroyImageView(device_, t.ms_view, nullptr);
    if (t.ms) vkDestroyImage(device_, t.ms, nullptr);
    if (t.ms_alloc) vmaFreeMemory(vma_, t.ms_alloc);
    if (t.sample_view) vkDestroyImageView(device_, t.sample_view, nullptr);
    DestroyImage(t.fb);
    if (t.view) vkDestroyImageView(device_, t.view, nullptr);
    if (t.image) vkDestroyImage(device_, t.image, nullptr);
    if (t.alloc) vmaFreeMemory(vma_, t.alloc);
    t = Target{};
}

VkBackend::Target* VkBackend::FindTarget(const Surface& s, bool depth) {
    if (!s.header) return nullptr;
    uint32_t gw, gh;
    SurfaceSize(s, gw, gh);
    uint32_t data = mem::VirtualFromPhysical(s.data);
    auto it = targets_.find(SizedKey(data, depth, gw, gh));
    if (it == targets_.end() || it->second.gw != gw || it->second.gh != gh) return nullptr;
    target_used_[data] = presents_;
    return &it->second;
}

// the surface's target, made again when its size changed; new targets start cleared (black, depth 1)
VkBackend::Target* VkBackend::EnsureTarget(const Surface& s, bool depth) {
    if (!s.header || lost_) return nullptr;
    uint32_t gw, gh;
    SurfaceSize(s, gw, gh);
    uint64_t key = SizedKey(mem::VirtualFromPhysical(s.data), depth, gw, gh);
    target_used_[mem::VirtualFromPhysical(s.data)] = presents_;
    auto it = targets_.find(key);
    if (it != targets_.end()) {
        if (it->second.gw == gw && it->second.gh == gh) return &it->second;
        // the GPU may still read the old one: finish what is recorded first
        Submit(true);
        DestroyTarget(it->second);
        targets_.erase(it);
    }
    Target t;
    t.depth = depth;
    t.gw = gw;
    t.gh = gh;
    // a game's own scale for some targets (S7: The Two Towers keeps its 128x128 water simulation at the console's
    // size), through the extension's MakeTarget as Direct3D 12 asks it; its format choice is Direct3D 12's alone
    float sc = scale_;
    if (g_extension) {
        DXGI_FORMAT unused = DXGI_FORMAT_B8G8R8A8_UNORM;
        g_extension->MakeTarget(s, depth, gw, gh, sc, unused);
    }
    float sx = sc == scale_ ? scale_ * xmul_ : sc;  // a scale the extension chose holds both ways
    t.w = std::max(1u, uint32_t(std::lround(float(gw) * sx)));
    t.h = std::max(1u, uint32_t(std::lround(float(gh) * sc)));
    t.data = mem::VirtualFromPhysical(s.data);
    t.bytes = depth ? gw * gh * 4 : GuestPitch(gw) * gh;
    t.epoch = DirtyEpoch(t.data);
    t.sig = MemSig(t.data, t.bytes);
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = depth ? depth_fmt_ : kColorFormat;
    ci.extent = {t.w, t.h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
               (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(device_, &ci, nullptr, &t.image);
    if (r != VK_SUCCESS) {
        LOG_FIRST(4, "vulkan: a %ux%u target could not be made (%s)", gw, gh, ResultName(r));
        return nullptr;
    }
    if (!AllocImage(t.image, t.alloc)) r = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (r == VK_SUCCESS) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ci.format;
        vi.subresourceRange = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                        : VK_IMAGE_ASPECT_COLOR_BIT),
                               0, 1, 0, 1};
        r = vkCreateImageView(device_, &vi, nullptr, &t.view);
        if (r == VK_SUCCESS && depth) {
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            r = vkCreateImageView(device_, &vi, nullptr, &t.sample_view);
        }
    }
    if (r != VK_SUCCESS) {
        LOG_FIRST(4, "vulkan: a %ux%u target's memory could not be set up (%s)", gw, gh, ResultName(r));
        DestroyTarget(t);
        return nullptr;
    }
    // cleared as D3D12 clears its new targets
    VkCommandBuffer cb = Cmd();
    if (!cb) {
        DestroyTarget(t);
        return nullptr;
    }
    EndRendering();
    ToLayout(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (depth) {
        VkClearDepthStencilValue v{1.0f, 0};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
        vkCmdClearDepthStencilImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    } else {
        VkClearColorValue v{};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    }
    if (WantMs(t) && !MakeTwin(t)) LOG_FIRST(2, "vulkan: anti-aliasing target %ux%u could not be made", t.w, t.h);
    LOG_FIRST(16, "vulkan: %s target %08X %ux%u, drawn at %ux%u%s", depth ? "depth" : "colour", mem::VirtualFromPhysical(s.data),
              gw, gh, t.w, t.h, t.ms ? " (anti-aliased)" : "");
    return &(targets_[key] = t);
}

// the frame's command buffer, begun on first use (after the GPU finished the last use of this frame slot)
// S17: a wait for the GPU that checks every 2 s whether the device is still there (as Direct3D 12's WaitFence): a GPU
// timeout or a driver reset otherwise leaves the game waiting for ever; false when the device is gone
bool VkBackend::WaitGpu(VkFence fence, const char* where) {
    for (int waited = 0;; waited += 2) {
        VkResult r = vkWaitForFences(device_, 1, &fence, VK_TRUE, 2000000000ull);
        if (r == VK_SUCCESS) return true;
        if (r != VK_TIMEOUT) {
            DeviceLost(where, r);
            return false;
        }
        if (waited + 2 == 10) LOGW("vulkan: the GPU has been busy with one frame for 10 s (still waiting)");
    }
}

void VkBackend::WaitSerial(uint64_t value) {
    if (!progress_ || !vkWaitSemaphores || Done() >= value || lost_) return;
    VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wi.semaphoreCount = 1;
    wi.pSemaphores = &progress_;
    wi.pValues = &value;
    for (;;) {
        VkResult r = vkWaitSemaphores(device_, &wi, 2000000000ull);
        if (r == VK_SUCCESS) return;
        if (r != VK_TIMEOUT) {
            DeviceLost("read-back wait", r);
            return;
        }
    }
}

// the graphics device is gone: logged, then the same plain message as Direct3D 12's
void VkBackend::DeviceLost(const char* where, VkResult r) {
    if (lost_) return;
    lost_ = true;
    LOGE("vulkan: the graphics device was lost (%s: %s)", where, ResultName(r));
    // what the driver says happened (VK_EXT_device_fault), as Direct3D 12 logs DRED's breadcrumbs
    if (device_fault_ && vkGetDeviceFaultInfoEXT) {
        VkDeviceFaultCountsEXT counts{VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
        if (vkGetDeviceFaultInfoEXT(device_, &counts, nullptr) == VK_SUCCESS) {
            std::vector<VkDeviceFaultAddressInfoEXT> addr(counts.addressInfoCount);
            std::vector<VkDeviceFaultVendorInfoEXT> vend(counts.vendorInfoCount);
            counts.vendorBinarySize = 0;  // the vendor's binary dump is not read
            VkDeviceFaultInfoEXT info{VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
            info.pAddressInfos = addr.empty() ? nullptr : addr.data();
            info.pVendorInfos = vend.empty() ? nullptr : vend.data();
            VkResult fr = vkGetDeviceFaultInfoEXT(device_, &counts, &info);
            if (fr == VK_SUCCESS || fr == VK_INCOMPLETE) {
                LOGE("vulkan: device fault: %s", info.description);
                for (uint32_t i = 0; i < counts.addressInfoCount && i < 8; i++)
                    LOGE("vulkan: device fault address %016llX (type %d, precision %llu)",
                         static_cast<unsigned long long>(addr[i].reportedAddress), int(addr[i].addressType),
                         static_cast<unsigned long long>(addr[i].addressPrecision));
                for (uint32_t i = 0; i < counts.vendorInfoCount && i < 8; i++)
                    LOGE("vulkan: device fault (driver): %s, code %llu, data %llu", vend[i].description,
                         static_cast<unsigned long long>(vend[i].vendorFaultCode),
                         static_cast<unsigned long long>(vend[i].vendorFaultData));
            }
        }
    }
    app::Fail(app::kExitGpuLost,
              L"The graphics driver stopped responding. This can happen when the graphics card is asked for more "
              L"than it can do: try a lower Render resolution, Anti-aliasing off, or a lower frame rate. Updating "
              L"the graphics driver may also help; the log has the details.");
}

VkCommandBuffer VkBackend::Cmd() {
    if (lost_ || !device_) return VK_NULL_HANDLE;
    Frame& f = frames_[frame_];
    if (recording_) return f.cmd;
    if (!WaitGpu(f.done, "frame slot")) return VK_NULL_HANDLE;
    vkResetCommandPool(device_, f.pool, 0);
    // the slot's last use is done: its ring, descriptor sets and retired images are free again
    f.used = 0;
    ring_gen_++;
    if (!f.rings.empty()) {  // back to the first ring and pool (D11)
        // extra rings and pools this slot has not needed for a while are given back (review: a 32-bit process has
        // little address space, and a level load's texture burst could keep hundreds of MB mapped for good)
        bool busy = f.ring > 0 || f.pool_i > 0;
        f.idle = busy ? 0 : f.idle + 1;
        if (f.idle >= 300 && (f.rings.size() > 1 || f.pools.size() > 1)) {
            for (size_t i = 1; i < f.rings.size(); i++) {
                vkDestroyBuffer(device_, f.rings[i].buf, nullptr);
                vkFreeMemory(device_, f.rings[i].mem, nullptr);
            }
            f.rings.resize(1);
            for (size_t i = 1; i < f.pools.size(); i++) vkDestroyDescriptorPool(device_, f.pools[i], nullptr);
            f.pools.resize(1);
        }
        f.ring = 0;
        f.upload = f.rings[0].buf;
        f.upload_mem = f.rings[0].mem;
        f.upload_cpu = f.rings[0].cpu;
    }
    for (uint32_t i = 0; i <= f.pool_i && i < f.pools.size(); i++) vkResetDescriptorPool(device_, f.pools[i], 0);
    f.pool_i = 0;
    if (!f.pools.empty()) f.sets = f.pools[0];
    f.sets_used = 0;
    f.set_cache.clear();
    for (Image& im : f.garbage) DestroyImage(im);
    f.garbage.clear();
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);
    recording_ = true;
    return f.cmd;
}

// what is recorded goes to the GPU without a present; wait: until it is done (a lock that reads results)
void VkBackend::Submit(bool wait) {
    if (!recording_ || lost_) return;
    EndRendering();
    Frame& f = frames_[frame_];
    vkEndCommandBuffer(f.cmd);
    recording_ = false;
    vkResetFences(device_, 1, &f.done);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    uint64_t value = serial_ + 1;
    VkTimelineSemaphoreSubmitInfo tsi{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    if (progress_) {
        tsi.signalSemaphoreValueCount = 1;
        tsi.pSignalSemaphoreValues = &value;
        si.pNext = &tsi;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &progress_;
    }
    VkResult r = vkQueueSubmit(queue_, 1, &si, f.done);
    if (r == VK_SUCCESS) serial_ = value;
    if (r != VK_SUCCESS) {
        if (r == VK_ERROR_DEVICE_LOST) DeviceLost("queue submit", r);
        lost_ = true;  // the fence was reset and never submitted: waiting on it would never end
        LOG_FIRST(4, "vulkan: vkQueueSubmit failed (%s)", ResultName(r));
        return;
    }
    if (wait) {
        PerfAdd wperf{wait_ms_};
        waits_++;
        WaitGpu(f.done, "submit");
    }
    else frame_ = (frame_ + 1) % kFrames;
}

void VkBackend::Flush() { Submit(true); }

// a full barrier between any earlier use and the next: simple and always right; finer stages come with profiling
static bool WritableLayout(VkImageLayout l) {
    return l == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || l == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
           l == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || l == VK_IMAGE_LAYOUT_GENERAL;
}

// every write recorded so far finished and visible before anything after (outside a rendering); see writes_open_
void VkBackend::OrderWrites() {
    if (!writes_open_) return;
    EndRendering();
    VkCommandBuffer cb = Cmd();
    if (!cb) return;
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr,
                         0, nullptr);
    writes_open_ = false;
}

void VkBackend::ToLayout(VkImage image, bool depth, VkImageLayout& cur, VkImageLayout want) {
    if (cur == want) {
        // a transfer follows: never inside a rendering (a read-back of a target left as a copy source, review of S4)
        if (want == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || want == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) EndRendering();
        // used again to write in the layout it has (a transfer into it again): the earlier writes go first. An
        // attachment is ordered where its rendering begins (BeginRendering), so a draw's own target costs nothing
        if (want == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && writes_open_) OrderWrites();
        if (want == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) writes_open_ = true;
        return;
    }
    EndRendering();  // no barriers inside a rendering
    VkCommandBuffer cb = Cmd();
    if (!cb) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = cur;
    b.newLayout = want;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                   : VK_IMAGE_ASPECT_COLOR_BIT),
                          0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    cur = want;
    if (WritableLayout(want)) writes_open_ = true;
}

bool VkBackend::BeginRendering(Target* rt, Target* ds, bool ms) {
    if (!dynamic_rendering_ || !vkCmdBeginRenderingKHR) return false;  // render passes: a later step
    if (rt && ds && (rt->w != ds->w || rt->h != ds->h)) ds = nullptr;
    if (!rt && !ds) return false;
    if ((rt && !rt->ms) || (ds && !ds->ms)) ms = false;
    if (rendering_ && rt == render_rt_ && ds == render_ds_ && ms == render_ms_) return true;
    EndRendering();
    VkCommandBuffer cb = Cmd();
    if (!cb) return false;
    // into the twins: each brought up to date with what was written outside them first (S14)
    if (ms && !clearing_)
        for (Target* t : {rt, ds})
            if (t) ReadyMs(*t);
    // a new rendering on attachments that may hold writes not yet ordered (drawn before, then another target, now
    // again): those writes finished first, so this rendering's LOAD reads them (D3)
    OrderWrites();
    if (ms) {
        if (rt) ToLayout(rt->ms, false, rt->ms_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (ds) ToLayout(ds->ms, true, ds->ms_layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        if (!clearing_)
            for (Target* t : {rt, ds})
                if (t) t->ms_new = true;
    } else {
        if (rt) ToLayout(*rt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (ds) ToLayout(*ds, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        // a depth resolve ReadyRes just did leaves the image in the attachment layout already, so no barrier came
        // with the layout: its write ordered before this rendering's LOAD (review of S14)
        OrderWrites();
    }
    writes_open_ = true;  // this rendering writes
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    VkRenderingAttachmentInfoKHR da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    if (rt) {
        ca.imageView = ms ? rt->ms_view : rt->view;
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    if (ds) {
        da.imageView = ms ? ds->ms_view : ds->view;
        da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    Target* size = rt ? rt : ds;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {size->w, size->h}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = rt ? 1 : 0;
    ri.pColorAttachments = rt ? &ca : nullptr;
    ri.pDepthAttachment = ds ? &da : nullptr;
    ri.pStencilAttachment = ds ? &da : nullptr;
    if (query_pool_) QueryTopUp(cb);  // (outside the rendering)
    vkCmdBeginRenderingKHR(cb, &ri);
    rendering_ = true;
    if (rt) rt->drawn = true;
    if (ds) ds->drawn = true;
    render_rt_ = rt;
    render_ds_ = ds;
    render_ms_ = ms;
    QueryPieceBegin(cb);  // a test open: its count goes on in this rendering
    return true;
}

void VkBackend::EndRendering() {
    if (!rendering_) return;
    QueryPieceEnd(frames_[frame_].cmd);
    vkCmdEndRenderingKHR(frames_[frame_].cmd);
    rendering_ = false;
    rep_.ok = false;  // (the next rendering binds again)
    render_rt_ = render_ds_ = nullptr;
    render_ms_ = false;
}

// ---- S14: anti-aliasing (Direct3D 12's lazy twins)

void VkBackend::ToLayout(Target& t, VkImageLayout want) {
    if (!clearing_) ReadyRes(t);
    ToLayout(t.image, t.depth, t.layout, want);
    bool write = want == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || want == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
                 want == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || want == VK_IMAGE_LAYOUT_GENERAL;
    if (write && t.ms && !clearing_) t.res_new = true;
}

// anti-aliasing's sample count: the setting, lowered to what the card takes for the frame's colour and depth
uint32_t VkBackend::PickSamples() {
    uint32_t want = app::Get().msaa;
    if (want < 2 || !gpu_) return 1;
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(gpu_, &p);
    VkSampleCountFlags can = p.limits.framebufferColorSampleCounts & p.limits.framebufferDepthSampleCounts &
                             p.limits.framebufferStencilSampleCounts;
    for (uint32_t n = std::min(want, 8u); n >= 2; n /= 2) {
        if (!(can & n)) continue;
        static uint32_t logged = 0;
        if (logged != n) {
            logged = n;
            if (n != want) LOGR("vulkan: anti-aliasing %ux (%ux is not supported by this graphics card)", n, want);
            else LOGR("vulkan: anti-aliasing %ux", n);
        }
        return n;
    }
    LOG_FIRST(1, "vulkan: anti-aliasing is not supported by this graphics card");
    return 1;
}

// the twin's format at samples_: drawn into, resolved from (colour: a transfer), cleared
bool VkBackend::MsFormatOk(VkFormat fmt, bool depth) {
    auto it = ms_ok_.find(uint32_t(fmt));
    if (it != ms_ok_.end()) return it->second;
    VkImageFormatProperties ip{};
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                     : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    bool ok = vkGetPhysicalDeviceImageFormatProperties &&
              vkGetPhysicalDeviceImageFormatProperties(gpu_, fmt, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                                       &ip) == VK_SUCCESS &&
              (ip.sampleCounts & samples_);
    ms_ok_[uint32_t(fmt)] = ok;
    return ok;
}

// the frame's own targets (the console's back buffer size) and the depth drawn with them; render-to-texture passes
// of other sizes (shadows, water, glows) keep one sample
bool VkBackend::WantMs(const Target& t) {
    if (samples_ < 2 || t.gw != guest_w_ || t.gh != guest_h_) return false;
    return MsFormatOk(t.depth ? depth_fmt_ : kColorFormat, t.depth);
}

// the twin, cleared as the target is when made; none when it can't be made
bool VkBackend::MakeTwin(Target& t) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = t.depth ? depth_fmt_ : kColorFormat;
    ci.extent = {t.w, t.h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VkSampleCountFlagBits(samples_);
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               (t.depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                        : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageAspectFlags aspect =
        t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    bool ok = vkCreateImage(device_, &ci, nullptr, &t.ms) == VK_SUCCESS && AllocImage(t.ms, t.ms_alloc);
    if (ok) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.ms;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ci.format;
        vi.subresourceRange = {aspect, 0, 1, 0, 1};
        ok = vkCreateImageView(device_, &vi, nullptr, &t.ms_view) == VK_SUCCESS;
    }
    VkCommandBuffer cb = ok ? Cmd() : VK_NULL_HANDLE;
    if (!cb) {
        if (t.ms_view) vkDestroyImageView(device_, t.ms_view, nullptr);
        if (t.ms) vkDestroyImage(device_, t.ms, nullptr);
        if (t.ms_alloc) vmaFreeMemory(vma_, t.ms_alloc);
        t.ms = VK_NULL_HANDLE;
        t.ms_alloc = VK_NULL_HANDLE;
        t.ms_view = VK_NULL_HANDLE;
        return false;
    }
    EndRendering();
    ToLayout(t.ms, t.depth, t.ms_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageSubresourceRange range{aspect, 0, 1, 0, 1};
    if (t.depth) {
        VkClearDepthStencilValue v{1.0f, 0};
        vkCmdClearDepthStencilImage(cb, t.ms, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    } else {
        VkClearColorValue v{};
        vkCmdClearColorImage(cb, t.ms, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    }
    t.ms_new = t.res_new = false;
    return true;
}

// the twin's newer pixels into the target: colour by a resolve (the samples averaged), depth by Vulkan 1.2's
// depth resolve at the end of an empty rendering (each pixel's first sample, as Direct3D 12's pass; the stencil too)
void VkBackend::ReadyRes(Target& t) {
    if (!t.ms || !t.ms_new) return;
    VkCommandBuffer cb = Cmd();
    if (!cb) return;
    EndRendering();
    t.ms_new = false;
    if (!t.depth) {
        ToLayout(t.ms, false, t.ms_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        ToLayout(t.image, false, t.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageResolve r{};
        r.srcSubresource = r.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        r.extent = {t.w, t.h, 1};
        vkCmdResolveImage(cb, t.ms, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          1, &r);
        return;
    }
    OrderWrites();  // the twin's draws finished before the resolve reads them
    ToLayout(t.ms, true, t.ms_layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    ToLayout(t.image, true, t.layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfoKHR da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    da.imageView = t.ms_view;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;  // every Vulkan 1.2 device has it, for depth and stencil
    da.resolveImageView = t.view;
    da.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {t.w, t.h}};
    ri.layerCount = 1;
    ri.pDepthAttachment = &da;
    ri.pStencilAttachment = &da;
    vkCmdBeginRenderingKHR(cb, &ri);
    vkCmdEndRenderingKHR(cb);
    writes_open_ = true;
}

// image's pixels into the twin (every sample the pixel's), as Direct3D 12's kMsUp passes: depth carried without its
// stencil, as there
VkPipeline VkBackend::MsUpPipeline(bool depth) {
    VkPipeline& pipe = ms_up_[depth ? 1 : 0];
    if (pipe) return pipe;
    const char* src = depth ? kMsUpDepthHlsl : kMsUpHlsl;
    uint64_t key = depth ? 0x4D5355500000D000ull : 0x4D5355500000C000ull;
    VkShaderModule vs = ShaderModule(key | 1, src, spirv::Stage::Vertex, "vs");
    VkShaderModule ps = ShaderModule(key | 2, src, spirv::Stage::Pixel, "psmain");
    if (!vs || !ps) return VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "vs", nullptr};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "psmain", nullptr};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VkSampleCountFlagBits(samples_);
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = depth ? VK_TRUE : VK_FALSE;
    dss.depthWriteEnable = depth ? VK_TRUE : VK_FALSE;
    dss.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = depth ? 0 : 1;
    bs.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkFormat color = kColorFormat;
    VkPipelineRenderingCreateInfoKHR ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    ri.colorAttachmentCount = depth ? 0 : 1;
    ri.pColorAttachmentFormats = depth ? nullptr : &color;
    ri.depthAttachmentFormat = depth ? depth_fmt_ : VK_FORMAT_UNDEFINED;
    ri.stencilAttachmentFormat = depth ? depth_fmt_ : VK_FORMAT_UNDEFINED;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.pNext = &ri;
    pi.stageCount = 2;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &dss;
    pi.pColorBlendState = &bs;
    pi.pDynamicState = &dy;
    pi.layout = pipe_layout_;
    if (vkCreateGraphicsPipelines(device_, pipe_cache_, 1, &pi, nullptr, &pipe) != VK_SUCCESS) {
        LOG_FIRST(2, "vulkan: anti-aliasing pass %d could not be made", depth ? 1 : 0);
        pipe = VK_NULL_HANDLE;
    }
    return pipe;
}

void VkBackend::ReadyMs(Target& t) {
    if (!t.ms || !t.res_new) return;
    VkCommandBuffer cb = Cmd();
    VkPipeline pipe = cb ? MsUpPipeline(t.depth) : VK_NULL_HANDLE;
    if (!pipe) return;
    EndRendering();
    if (t.depth) LOG_FIRST(2, "vulkan: anti-aliasing: depth %08X written outside a draw, carried over without its stencil", t.data);
    VkImageLayout src_layout =
        t.depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ToLayout(t.image, t.depth, t.layout, src_layout);
    OrderWrites();
    ToLayout(t.ms, t.depth, t.ms_layout,
             t.depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfoKHR a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    a.imageView = t.ms_view;
    a.imageLayout = t.depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {t.w, t.h}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = t.depth ? 0 : 1;
    ri.pColorAttachments = t.depth ? nullptr : &a;
    ri.pDepthAttachment = t.depth ? &a : nullptr;
    ri.pStencilAttachment = t.depth ? &a : nullptr;
    vkCmdBeginRenderingKHR(cb, &ri);
    VkViewport v{0, 0, float(t.w), float(t.h), 0, 1};
    VkRect2D sc{{0, 0}, {t.w, t.h}};
    vkCmdSetViewport(cb, 0, 1, &v);
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    const VkImageView src[3] = {t.depth ? t.sample_view : t.view, VK_NULL_HANDLE, VK_NULL_HANDLE};
    const VkImageLayout lay[3] = {src_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const float p[8] = {};
    // the twin up to date only once the pass is drawn (review of S14: a full ring or set pool left it stale)
    if (PassSet(src, lay, p)) {
        vkCmdDraw(cb, 3, 1, 0, 0);
        t.res_new = false;
    }
    vkCmdEndRenderingKHR(cb);
    writes_open_ = true;
}

// ---- S6 leftover: depth copies (Direct3D 12's DepthCopy / DrawDepthCopy, kDepthHlsl)

bool VkBackend::DepthCopy(const State& s) {
    // the same texture onto the same target as the last draw that was no depth copy, no target made since: none now
    // either (as Direct3D 12: this check was 1 ms a frame at 12,000 to 20,000 draws there)
    static uint32_t last_tex = 0, last_rt = 0;
    static size_t last_targets = size_t(-1);
    if (s.textures[0] == last_tex && s.rt.data == last_rt && targets_.size() == last_targets) return false;
    auto miss = [&] {
        last_tex = s.textures[0];
        last_rt = s.rt.data;
        last_targets = targets_.size();
        return false;
    };
    TextureDesc d;
    if (!s.rt.header || !s.textures[0] || !Describe(s.textures[0], d) || Format(d.format).depth || d.cube ||
        d.depth != 1)
        return miss();
    uint32_t gw, gh;
    SurfaceSize(s.rt, gw, gh);
    auto di = targets_.find(SizedKey(mem::VirtualFromPhysical(s.rt.data), true, gw, gh));
    auto si = targets_.find(SizedKey(d.data, true, d.width, d.height));
    if (di == targets_.end() || si == targets_.end() || di == si) return miss();
    Target& dst = di->second;
    Target& src = si->second;
    if (!depth_copy_pipe_) {
        VkShaderModule vs = ShaderModule(0xDE97C0B1ull, kDepthHlsl, spirv::Stage::Vertex, "vs");
        VkShaderModule ps = ShaderModule(0xDE97C0B2ull, kDepthHlsl, spirv::Stage::Pixel, "psmain");
        if (!vs || !ps) return miss();
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "vs", nullptr};
        st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "psmain", nullptr};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        dss.depthTestEnable = VK_TRUE;
        dss.depthWriteEnable = VK_TRUE;
        dss.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2;
        dy.pDynamicStates = dyn;
        VkPipelineRenderingCreateInfoKHR ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
        ri.depthAttachmentFormat = depth_fmt_;
        ri.stencilAttachmentFormat = depth_fmt_;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.pNext = &ri;
        pi.stageCount = 2;
        pi.pStages = st;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &dss;
        pi.pColorBlendState = &bs;
        pi.pDynamicState = &dy;
        pi.layout = pipe_layout_;
        if (vkCreateGraphicsPipelines(device_, pipe_cache_, 1, &pi, nullptr, &depth_copy_pipe_) != VK_SUCCESS) {
            depth_copy_pipe_ = VK_NULL_HANDLE;
            LOG_FIRST(1, "vulkan: the depth copy pass could not be made");
            return miss();
        }
    }
    Room(1);  // its set and constants (review: a full ring or pool made the copy fail while the draw was dropped)
    VkCommandBuffer cb = Cmd();
    if (!cb) return false;
    last_targets = size_t(-1);
    if (cap_) fprintf(cap_, "%04u depth copy %08X to %08X\n", cap_i_++, src.data, dst.data);
    EndRendering();
    ToLayout(src, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    ToLayout(dst, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    OrderWrites();
    VkRenderingAttachmentInfoKHR da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    da.imageView = dst.view;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {dst.w, dst.h}};
    ri.layerCount = 1;
    ri.pDepthAttachment = &da;
    ri.pStencilAttachment = &da;
    vkCmdBeginRenderingKHR(cb, &ri);
    // kDepthHlsl places the triangle as Direct3D does: a viewport of negative height stands it up, as the draws' do
    VkViewport v{0, float(dst.h), float(dst.w), -float(dst.h), 0, 1};
    VkRect2D sc{{0, 0}, {dst.w, dst.h}};
    vkCmdSetViewport(cb, 0, 1, &v);
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_copy_pipe_);
    const VkImageView srcv[3] = {src.sample_view, VK_NULL_HANDLE, VK_NULL_HANDLE};
    const VkImageLayout lay[3] = {VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const float p[8] = {};
    bool drawn = PassSet(srcv, lay, p);
    if (drawn) vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRenderingKHR(cb);
    writes_open_ = true;
    if (!drawn) {
        LOG_FIRST(2, "vulkan: a depth copy could not be drawn (no room for its set)");
        return false;
    }
    dst.drawn = true;
    LOG_FIRST(2, "vulkan: depth %08X copied into depth %08X", src.data, dst.data);
    return true;
}

// ---- S13: visibility tests

bool VkBackend::EnsureQueries() {
    if (query_pool_) return true;
    if (query_failed_ || !device_ || !vkCreateQueryPool || !vkGetQueryPoolResults) {
        query_failed_ = true;
        return false;
    }
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_OCCLUSION;
    qi.queryCount = kQueryEntries;
    VkQueryPool pool = VK_NULL_HANDLE;
    if (vkCreateQueryPool(device_, &qi, nullptr, &pool) != VK_SUCCESS) {
        LOGE("vulkan: occlusion queries unavailable, visibility tests read as fully visible");
        query_failed_ = true;
        return false;
    }
    query_tag_.reset(new std::atomic<uint64_t>[kQueryEntries]);
    for (uint32_t i = 0; i < kQueryEntries; i++) query_tag_[i] = 0;
    std::lock_guard lock(query_lock_);
    query_pool_ = pool;
    query_next_ = query_ready_ = 0;
    LOGR("vulkan: visibility tests on (%s counts)", precise_ ? "exact" : "rough");
    return true;
}

// the next entries reset, in order round the pool, so that ~32 are always ready: a reset belongs outside a
// rendering, and one recorded earlier in this or an earlier command buffer runs before the entry's next use
void VkBackend::QueryTopUp(VkCommandBuffer cb) {
    if (!query_pool_ || query_ready_ >= 32) return;
    uint32_t first = (query_next_ + query_ready_) % kQueryEntries, n = 64;
    while (n) {
        uint32_t run = std::min(n, kQueryEntries - first);
        vkCmdResetQueryPool(cb, query_pool_, first, run);
        n -= run;
        first = (first + run) % kQueryEntries;
    }
    query_ready_ += 64;
}

void VkBackend::QueryPieceBegin(VkCommandBuffer cb) {
    if (!query_open_ || query_hold_ || query_piece_ >= 0 || !rendering_ || !query_pool_ || !query_ready_) return;
    if (query_cur_.pieces == kQueryPieces) {
        LOG_FIRST(2, "vulkan: a visibility test drawn in more than %u renderings, the rest not counted", kQueryPieces);
        return;
    }
    uint32_t e = query_next_;
    query_next_ = (query_next_ + 1) % kQueryEntries;
    query_ready_--;
    uint64_t tag = ++query_tags_;
    query_tag_[e].store(tag);  // before the begin: a result read under the old tag is the old use's
    vkCmdBeginQuery(cb, query_pool_, e, precise_ ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
    query_cur_.entry[query_cur_.pieces] = e;
    query_cur_.tag[query_cur_.pieces] = tag;
    // counted at our resolution on the target drawn to, given back in the title's (as Direct3D 12); worked out now,
    // while the target exists (review of S13: one remade before EndQuery was read freed)
    const Target* t = render_ds_ ? render_ds_ : render_rt_;
    float ratio = t && t->w && t->h ? float(t->gw) * t->gh / (float(t->w) * t->h) : 1 / (scale_ * xmul_ * scale_);
    if (render_ms_) ratio /= float(samples_);  // anti-aliasing counts samples, not pixels
    query_cur_.ratio[query_cur_.pieces] = ratio;
    query_cur_.pieces++;
    query_piece_ = e;
}

void VkBackend::QueryPieceEnd(VkCommandBuffer cb) {
    if (query_piece_ < 0) return;
    vkCmdEndQuery(cb, query_pool_, uint32_t(query_piece_));
    query_piece_ = -1;
}

void VkBackend::BeginQuery() {
    if (!EnsureQueries()) return;
    if (query_open_) QueryPieceEnd(frames_[frame_].cmd);  // one left unnamed: dropped
    query_open_ = true;
    query_cur_ = QueryRec{};
    if (!rendering_) return;  // begun with the test's first draw
    if (!query_ready_) {
        EndRendering();  // none reset: the next rendering resets more first
        return;
    }
    QueryPieceBegin(frames_[frame_].cmd);
}

void VkBackend::EndQuery(uint32_t index) {
    if (!query_open_) return;
    if (recording_) QueryPieceEnd(frames_[frame_].cmd);
    query_open_ = false;
    query_cur_.seq = ++query_seq_;
    query_cur_.serial = progress_ ? serial_ + 1 : 0;  // the next submit carries what is recorded (or it went already)
    std::lock_guard lock(query_lock_);
    QuerySlot& q = queries_[index % kQueryIndexes];
    q.rec[q.next] = query_cur_;
    q.next = (q.next + 1) % kQueryRing;
}

// the newest finished count for the index: every piece's result there (vkGetQueryPoolResults without waiting) and
// its entry not taken by a later test meanwhile (the tag read again after the result, so a reuse in between shows)
bool VkBackend::QueryResult(uint32_t index, uint32_t& pixels) {
    std::lock_guard lock(query_lock_);
    QuerySlot& q = queries_[index % kQueryIndexes];
    if (query_pool_) {
        uint64_t finished = Done();
        uint32_t order[kQueryRing];
        for (uint32_t i = 0; i < kQueryRing; i++) order[i] = i;
        std::sort(order, order + kQueryRing, [&](uint32_t a, uint32_t b) { return q.rec[a].seq > q.rec[b].seq; });
        for (uint32_t i : order) {
            QueryRec& r = q.rec[i];
            if (!r.seq || r.seq <= q.got) break;  // older than what was read already
            // the submit with its last piece finished: its resets and ends have run, so the availability bits read
            // below are this test's (review of S13: before that, a reused entry still showed the old test's result)
            if (r.serial > finished) continue;
            double sum = 0;
            bool done = true;
            for (uint32_t k = 0; k < r.pieces && done; k++) {
                uint64_t res[2] = {};  // count, available
                if (query_tag_[r.entry[k]].load() != r.tag[k]) {
                    done = false;
                    break;
                }
                VkResult vr = vkGetQueryPoolResults(device_, query_pool_, r.entry[k], 1, sizeof(res), res, sizeof(res),
                                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
                if ((vr != VK_SUCCESS && vr != VK_NOT_READY) || !res[1] || query_tag_[r.entry[k]].load() != r.tag[k])
                    done = false;
                else
                    sum += double(res[0]) * r.ratio[k];
            }
            if (!done) continue;  // not finished yet (or lost to a reuse): an older one may be
            q.pixels = uint32_t(std::min<double>(std::round(sum), 4294967295.0));
            q.got = r.seq;
            q.have = true;
            LOG_FIRST(4, "vulkan: visibility test %u: %u pixels (%u renderings)", index, q.pixels, r.pieces);
            break;
        }
    }
    pixels = q.pixels;
    return q.have;
}

// some colour channels cleared: a full-screen triangle with that write mask, scissored to each rect
constexpr const char* kMaskedClearHlsl = R"(
[[vk::push_constant]] cbuffer P { float4 col; };
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2 - 1, 0, 1);
}
float4 ps() : SV_Target { return col; }
)";

void VkBackend::MaskedClear(const State& s, Target& t, uint32_t count, const uint8_t* rects, uint32_t color,
                            uint32_t mask, bool ms) {
    mask &= 0xF;
    if (!mask || !dynamic_rendering_ || (ms && !t.ms)) return;
    uint32_t pi_index = mask | (ms ? 16 : 0);  // the twin's at samples_
    bool ms_on = ms;  // (the pipeline's multisample state below takes the name)
    rep_.ok = false;  // binds its own pipeline in the open rendering
    if (!clear_layout_) {
        VkPushConstantRange pr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &pr;
        if (vkCreatePipelineLayout(device_, &li, nullptr, &clear_layout_) != VK_SUCCESS) return;
    }
    if (!clear_pipes_[pi_index]) {
        VkShaderModule vs = ShaderModule(0xC1EA5000ull, kMaskedClearHlsl, spirv::Stage::Vertex, "vs");
        VkShaderModule ps = ShaderModule(0xC1EA5001ull, kMaskedClearHlsl, spirv::Stage::Pixel, "ps");
        if (!vs || !ps) return;
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "vs", nullptr};
        st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "ps", nullptr};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = ms_on ? VkSampleCountFlagBits(samples_) : VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = mask;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        bs.attachmentCount = 1;
        bs.pAttachments = &cb;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2;
        dy.pDynamicStates = dyn;
        VkFormat color_fmt = kColorFormat;
        VkPipelineRenderingCreateInfoKHR ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &color_fmt;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.pNext = &ri;
        pi.stageCount = 2;
        pi.pStages = st;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &bs;
        pi.pDynamicState = &dy;
        pi.layout = clear_layout_;
        if (vkCreateGraphicsPipelines(device_, pipe_cache_, 1, &pi, nullptr, &clear_pipes_[pi_index]) != VK_SUCCESS) {
            clear_pipes_[pi_index] = VK_NULL_HANDLE;
            return;
        }
    }
    std::vector<VkRect2D> rs;
    auto add = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
        VkRect2D r = ScaledRect(t, x0, y0, x1, y1);
        if (r.extent.width && r.extent.height) rs.push_back(r);
    };
    if (count && rects) {
        for (uint32_t i = 0; i < count; i++) {
            int32_t r[4];
            std::memcpy(r, rects + size_t(i) * sizeof(r), sizeof(r));
            add(r[0], r[1], r[2], r[3]);
        }
    } else {
        add(int32_t(s.viewport.x), int32_t(s.viewport.y), int32_t(s.viewport.x + s.viewport.w),
            int32_t(s.viewport.y + s.viewport.h));
    }
    // a rendering of its own, outside any visibility test (Direct3D 12 clears with ClearRenderTargetView, which no
    // test counts; this one is drawn)
    if (rs.empty()) return;
    if (query_open_) EndRendering();
    query_hold_ = true;
    bool began = BeginRendering(&t, nullptr, ms);
    query_hold_ = false;
    if (!began) return;
    VkCommandBuffer cb = Cmd();
    float col[4] = {float((color >> 16) & 0xFF) / 255.0f, float((color >> 8) & 0xFF) / 255.0f, float(color & 0xFF) / 255.0f,
                    float(color >> 24) / 255.0f};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, clear_pipes_[pi_index]);
    vkCmdPushConstants(cb, clear_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(col), col);
    VkViewport v{0, 0, float(t.w), float(t.h), 0, 1};
    vkCmdSetViewport(cb, 0, 1, &v);
    for (const VkRect2D& r : rs) {
        vkCmdSetScissor(cb, 0, 1, &r);
        vkCmdDraw(cb, 3, 1, 0, 0);
    }
    if (query_open_) EndRendering();  // the test's next draw counts in a rendering of its own
    LOG_FIRST(2, "vulkan: a clear of colour channels %X drawn", mask);
}

// rects: count D3DRECTs, else the viewport; colour channels other than all four are not cleared yet (logged)
void VkBackend::Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
                      uint32_t stencil) {
    if (cap_) {
        CapStep(s.rt);
        fprintf(cap_, "%04u clear rt %08X ds %08X flags %X color %08X z %.3f\n", cap_i_++, s.rt.data, s.zs.data, flags,
                color, z);
    }
    Target* rt = (flags & kClearTarget) ? EnsureTarget(s.rt, false) : nullptr;
    Target* ds = (flags & (kClearZ | kClearStencil)) ? EnsureTarget(s.zs, true) : nullptr;
    if (rt && CpuWrote(*rt)) UploadTarget(*rt, s.rt);  // what the CPU wrote goes in first, the clear goes over it
    // S14: a target with a multisampled twin clears both, so neither falls behind the other (their newer flags stay
    // as they are); the rendering is closed after, so the next draw marks what it draws into
    bool twins = (rt && rt->ms) || (ds && ds->ms);
    clearing_ = twins;
    if (rt && (flags & kClearTarget) != kClearTarget) {
        // vkCmdClearAttachments writes every channel: The Two Towers clears only alpha between drawing the world and
        // its lighting passes, which would wipe the picture; drawn instead with that channel mask
        MaskedClear(s, *rt, count, rects, color, (flags & kClearTarget) >> 4);
        if (rt->ms) MaskedClear(s, *rt, count, rects, color, (flags & kClearTarget) >> 4, true);
        rt = nullptr;
    }
    if (rt || ds) {
        ClearInto(s, rt, ds, false, count, rects, flags, color, z, stencil);
        if (twins) {
            Target* mrt = rt && rt->ms ? rt : nullptr;
            Target* mds = ds && ds->ms ? ds : nullptr;
            if (mrt && mds && mrt->w == mds->w && mrt->h == mds->h) {
                ClearInto(s, mrt, mds, true, count, rects, flags, color, z, stencil);
            } else {
                if (mrt) ClearInto(s, mrt, nullptr, true, count, rects, flags, color, z, stencil);
                if (mds) ClearInto(s, nullptr, mds, true, count, rects, flags, color, z, stencil);
            }
        }
    }
    if (twins) EndRendering();
    clearing_ = false;
}

// Clear's rects into rt and ds (either may be null), or into their twins
void VkBackend::ClearInto(const State& s, Target* rt, Target* ds, bool ms, uint32_t count, const uint8_t* rects,
                          uint32_t flags, uint32_t color, float z, uint32_t stencil) {
    if (!rt && !ds) return;
    Target* size = rt ? rt : ds;
    std::vector<VkClearRect> rs;
    // the rects (or the viewport) scaled to a target's render size (D6: each target its own, as Direct3D 12)
    auto build = [&](const Target& t) {
        rs.clear();
        auto add = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
            VkRect2D r = ScaledRect(t, x0, y0, x1, y1);
            if (r.extent.width && r.extent.height) rs.push_back({r, 0, 1});
        };
        if (count && rects) {
            for (uint32_t i = 0; i < count; i++) {
                int32_t r[4];
                std::memcpy(r, rects + size_t(i) * sizeof(r), sizeof(r));
                add(r[0], r[1], r[2], r[3]);
            }
        } else {
            const Viewport& v = s.viewport;
            add(int32_t(v.x), int32_t(v.y), int32_t(v.x + v.w), int32_t(v.y + v.h));
        }
    };
    build(*size);
    if (rs.empty() && !(rt && ds)) return;

    VkClearAttachment at[2]{};
    uint32_t n = 0;
    if (rt) {
        at[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        at[n].colorAttachment = 0;
        // D3DCOLOR ARGB to rgba
        at[n].clearValue.color.float32[0] = float((color >> 16) & 0xFF) / 255.0f;
        at[n].clearValue.color.float32[1] = float((color >> 8) & 0xFF) / 255.0f;
        at[n].clearValue.color.float32[2] = float(color & 0xFF) / 255.0f;
        at[n].clearValue.color.float32[3] = float(color >> 24) / 255.0f;
        n++;
    }
    if (ds) {
        at[n].aspectMask = VkImageAspectFlags(((flags & kClearZ) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                                              ((flags & kClearStencil) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0));
        at[n].clearValue.depthStencil = {std::clamp(z, 0.0f, 1.0f), stencil & 0xFF};
        n++;
    }
    // the clear goes into the targets bound as they are: a depth buffer of another size is cleared on its own
    if (rt && ds && (rt->w != ds->w || rt->h != ds->h)) {
        if (!rs.empty() && BeginRendering(rt, nullptr, ms))
            vkCmdClearAttachments(Cmd(), 1, &at[0], uint32_t(rs.size()), rs.data());
        rt = nullptr;
        size = ds;
        at[0] = at[1];
        n = 1;
        build(*ds);  // the same rects at the depth buffer's own scale
    }
    if (rs.empty()) return;
    if (BeginRendering(rt, ds, ms)) {
        vkCmdClearAttachments(Cmd(), n, at, uint32_t(rs.size()), rs.data());
        return;
    }
    LOG_FIRST(1, "vulkan: clears need dynamic rendering (render passes are a later step)");
}

// ---- phase 1b: draws

// D3DBLEND (GL values) as a Vulkan factor; alpha: for the alpha channel, where colour factors read alpha
VkBlendFactor VkBlend(uint32_t v, bool alpha) {
    switch (v) {
        case 0: return VK_BLEND_FACTOR_ZERO;
        case 1: return VK_BLEND_FACTOR_ONE;
        case 0x300: return alpha ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_SRC_COLOR;
        case 0x301: return alpha ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 0x302: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x304: return VK_BLEND_FACTOR_DST_ALPHA;
        case 0x305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 0x306: return alpha ? VK_BLEND_FACTOR_DST_ALPHA : VK_BLEND_FACTOR_DST_COLOR;
        case 0x307: return alpha ? VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 0x308: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case 0x8001: return alpha ? VK_BLEND_FACTOR_CONSTANT_ALPHA : VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 0x8002: return alpha ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        // the constant colour's alpha in every channel: Vulkan has the factor D3D12 lacks
        case 0x8003: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case 0x8004: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp VkBlendOpOf(uint32_t v) {
    switch (v) {
        case 0x800A: return VK_BLEND_OP_SUBTRACT;
        case 0x800B: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case 0x8007: return VK_BLEND_OP_MIN;
        case 0x8008: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

// D3DCMP_NEVER 0x200 .. ALWAYS 0x207: the same order as VkCompareOp
VkCompareOp VkCmp(uint32_t v) { return VkCompareOp(v >= 0x200 && v <= 0x207 ? v - 0x200 : 7); }

VkStencilOp VkStencil(uint32_t v) {
    switch (v) {
        case 0: return VK_STENCIL_OP_ZERO;
        case 0x1E01: return VK_STENCIL_OP_REPLACE;
        case 0x1E02: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case 0x1E03: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case 0x150A: return VK_STENCIL_OP_INVERT;
        case 0x8507: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case 0x8508: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default: return VK_STENCIL_OP_KEEP;
    }
}

// D3DTADDRESS: 1 wrap, 2 mirror, 3 clamp, 4 border, 5 clamp to edge
VkSamplerAddressMode VkAddress(uint32_t v) {
    switch (v) {
        case 2: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case 3: case 5: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case 4: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

// the DXGI formats vertex.cpp hands its elements over in (numbers, as DXGI defines them)
VkFormat VkVertexFormat(uint32_t dxgi) {
    switch (dxgi) {
        case 2: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case 6: return VK_FORMAT_R32G32B32_SFLOAT;
        case 13: return VK_FORMAT_R16G16B16A16_SNORM;
        case 14: return VK_FORMAT_R16G16B16A16_SINT;
        case 16: return VK_FORMAT_R32G32_SFLOAT;
        case 28: return VK_FORMAT_R8G8B8A8_UNORM;
        case 37: return VK_FORMAT_R16G16_SNORM;
        case 38: return VK_FORMAT_R16G16_SINT;
        case 41: return VK_FORMAT_R32_SFLOAT;
        case 42: return VK_FORMAT_R32_UINT;
        case 49: return VK_FORMAT_R8G8_UNORM;
        case 58: return VK_FORMAT_R16_SNORM;
        case 59: return VK_FORMAT_R16_SINT;
        case 61: return VK_FORMAT_R8_UNORM;
        case 87: return VK_FORMAT_B8G8R8A8_UNORM;
        default: return VK_FORMAT_UNDEFINED;
    }
}

VkFormat VkTextureFormat(Host h) {
    switch (h) {
        case Host::BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case Host::BC2: return VK_FORMAT_BC2_UNORM_BLOCK;
        case Host::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
        case Host::R32F: return VK_FORMAT_R32_SFLOAT;
        case Host::R16Unorm: return VK_FORMAT_R16_UNORM;
        default: return VK_FORMAT_B8G8R8A8_UNORM;
    }
}

constexpr uint32_t kFillWireframe = 0x1B01;  // D3DFILL_WIREFRAME
constexpr uint32_t kCullCw = 0x900;          // D3DCULL_CW
constexpr float kDepth24Max = 16777215.0f;
constexpr uint64_t kPixelKeySalt = 0x9E3779B97F4A7C15ull;


// Upload's ring: a draw's constants, vertices, indices and texture levels, host visible, one per frame in flight
bool VkBackend::MakeRing(Frame::Ring& ring) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = kUploadSize;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
               VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bi, nullptr, &ring.buf) != VK_SUCCESS) {
        ring = {};
        return false;
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, ring.buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    void* p = nullptr;
    bool ok = FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         ai.memoryTypeIndex) &&
              vkAllocateMemory(device_, &ai, nullptr, &ring.mem) == VK_SUCCESS &&
              vkBindBufferMemory(device_, ring.buf, ring.mem, 0) == VK_SUCCESS &&
              vkMapMemory(device_, ring.mem, 0, VK_WHOLE_SIZE, 0, &p) == VK_SUCCESS;
    if (!ok) {
        vkDestroyBuffer(device_, ring.buf, nullptr);
        if (ring.mem) vkFreeMemory(device_, ring.mem, nullptr);
        ring = {};
        return false;
    }
    ring.cpu = static_cast<uint8_t*>(p);
    return true;
}

bool VkBackend::MakePool(VkDescriptorPool& pool) {
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kSetsPerFrame * 2},
                                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSetsPerFrame * 4},
                                    {VK_DESCRIPTOR_TYPE_SAMPLER, kSetsPerFrame * 4}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = kSetsPerFrame;
    pi.poolSizeCount = 3;
    pi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device_, &pi, nullptr, &pool) != VK_SUCCESS) {
        pool = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VkBackend::Room(uint32_t sets) {
    if (!Cmd()) return;
    Frame& f = frames_[frame_];
    bool ring_full = f.used > kUploadSize - kUploadReserve, pool_full = f.sets_used + sets >= kSetsPerFrame;
    if (!ring_full && !pool_full) return;
    if (ring_full) {
        // (at most 3 rings in a 32-bit process, 8 in a 64-bit one: past that the frame waits, as before D11)
        constexpr size_t kMaxRings = sizeof(void*) == 4 ? 3 : 8;
        if (f.ring + 1 == f.rings.size() && f.rings.size() < kMaxRings) {
            Frame::Ring r;
            if (MakeRing(r)) {
                f.rings.push_back(r);
                LOG_FIRST(4, "vulkan: a frame filled its upload ring: ring %zu added (as Direct3D 12's overflow)",
                          f.rings.size());
            }
        }
        if (f.ring + 1 < f.rings.size()) {
            f.ring++;
            f.upload = f.rings[f.ring].buf;
            f.upload_mem = f.rings[f.ring].mem;
            f.upload_cpu = f.rings[f.ring].cpu;
            f.used = 0;
            ring_gen_++;           // the constants' dedupe: another buffer
            f.set_cache.clear();   // the sets name the ring buffer
            rep_.ok = false;       // (the repeat path's set too)
            ring_full = false;
        }
    }
    if (pool_full) {
        if (f.pool_i + 1 == f.pools.size()) {
            VkDescriptorPool p;
            if (MakePool(p)) f.pools.push_back(p);
        }
        if (f.pool_i + 1 < f.pools.size()) {
            f.pool_i++;
            f.sets = f.pools[f.pool_i];
            f.sets_used = 0;
            f.set_cache.clear();
            pool_full = false;
        }
    }
    if (ring_full || pool_full) Submit(true);  // nothing more could be made: the frame's work finished first
}

bool VkBackend::CreateDrawResources() {
    for (Frame& f : frames_) {
        f.rings.resize(1);
        f.pools.resize(1);
        if (!MakeRing(f.rings[0])) return Fail("the upload ring could not be made");
        if (!MakePool(f.pools[0])) return Fail("vkCreateDescriptorPool");
        f.upload = f.rings[0].buf;
        f.upload_mem = f.rings[0].mem;
        f.upload_cpu = f.rings[0].cpu;
        f.sets = f.pools[0];
    }

    // set 0: b0 b1 (uniform buffers, dynamic), t0..t3 at 8..11, s0..s3 at 16..19, as gpu/spirv.cpp maps them
    VkDescriptorSetLayoutBinding b[10]{};
    // (b0 b1 dynamic: a set names the ring buffer, each bind gives the draw's offsets, so sets are shared, S16)
    for (uint32_t i = 0; i < 2; i++)
        b[i] = {i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr};
    for (uint32_t i = 0; i < 4; i++) {
        b[2 + i] = {8 + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        b[6 + i] = {16 + i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 10;
    li.pBindings = b;
    VkResult r = vkCreateDescriptorSetLayout(device_, &li, nullptr, &set_layout_);
    if (r != VK_SUCCESS) return Fail("vkCreateDescriptorSetLayout", r);
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &set_layout_;
    r = vkCreatePipelineLayout(device_, &pl, nullptr, &pipe_layout_);
    if (r != VK_SUCCESS) return Fail("vkCreatePipelineLayout", r);
    // the driver's pipeline cache from the last run (S15), when it was this card's and driver's: a header check, as
    // drivers are not all careful with another device's data
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    std::vector<uint8_t> saved;
    if (FILE* f = _wfopen((app::ShaderCacheDir() / "vk_pipeline_cache.bin").c_str(), L"rb")) {
        if (std::fseek(f, 0, SEEK_END) == 0) {
            long n = std::ftell(f);
            if (n >= 32 && n < (512l << 20)) {
                saved.resize(size_t(n));
                std::fseek(f, 0, SEEK_SET);
                if (std::fread(saved.data(), 1, saved.size(), f) != saved.size()) saved.clear();
            }
        }
        std::fclose(f);
    }
    if (!saved.empty()) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(gpu_, &p);
        uint32_t head[4];
        std::memcpy(head, saved.data(), sizeof(head));
        bool same = head[0] >= 32 && head[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE && head[2] == p.vendorID &&
                    head[3] == p.deviceID && !std::memcmp(saved.data() + 16, p.pipelineCacheUUID, VK_UUID_SIZE);
        if (same) {
            ci.initialDataSize = saved.size();
            ci.pInitialData = saved.data();
        } else {
            LOGR("vulkan: the saved pipeline cache is another card's or driver's; started again");
        }
    }
    if (vkCreatePipelineCache(device_, &ci, nullptr, &pipe_cache_) != VK_SUCCESS && ci.initialDataSize) {
        ci.initialDataSize = 0;
        ci.pInitialData = nullptr;
        vkCreatePipelineCache(device_, &ci, nullptr, &pipe_cache_);
    }
    if (ci.initialDataSize) LOGR("vulkan: pipeline cache of %zu KB from the last run", saved.size() / 1024);

    // what a stage without a texture reads: 1x1 black of each kind, and a depth texel for a shadow stage
    if (!CreateDummy(dummy_[0], VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, kColorFormat, 1) ||
        !CreateDummy(dummy_[1], VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE, kColorFormat, 6) ||
        !CreateDummy(dummy_[2], VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D, kColorFormat, 1) ||
        !CreateDummy(dummy_[3], VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, depth_fmt_, 1))
        return Fail("the empty stage textures could not be made");
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.maxLod = VK_LOD_CLAMP_NONE;
    r = vkCreateSampler(device_, &si, nullptr, &plain_sampler_);
    if (r != VK_SUCCESS) return Fail("vkCreateSampler", r);
    si.compareEnable = VK_TRUE;
    si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    r = vkCreateSampler(device_, &si, nullptr, &compare_sampler_);
    if (r != VK_SUCCESS) return Fail("vkCreateSampler", r);
    return true;
}

bool VkBackend::CreateDummy(Image& im, VkImageType type, VkImageViewType view, VkFormat fmt, uint32_t layers) {
    bool depth = fmt == depth_fmt_;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.flags = view == VK_IMAGE_VIEW_TYPE_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ci.imageType = type;
    ci.format = fmt;
    ci.extent = {1, 1, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = layers;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!MakeImage(im, ci, view, depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT)) return false;
    VkCommandBuffer cb = Cmd();
    if (!cb) return false;
    VkImageSubresourceRange range{VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                           : VK_IMAGE_ASPECT_COLOR_BIT),
                                  0, 1, 0, layers};
    Barrier(im.image, range, im.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (depth) {
        VkClearDepthStencilValue v{1.0f, 0};
        vkCmdClearDepthStencilImage(cb, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    } else {
        VkClearColorValue v{};
        vkCmdClearColorImage(cb, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    }
    // read-only for shaders, a depth one too: DEPTH_STENCIL_READ_ONLY_OPTIMAL needs an image made to be a depth
    // attachment, which a stand-in is not (the validation layer's first error, S0)
    Barrier(im.image, range, im.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

// an image with its own memory and one view; view_aspect: what the view reads
bool VkBackend::MakeImage(Image& im, const VkImageCreateInfo& ci, VkImageViewType view, VkImageAspectFlags view_aspect) {
    VkResult r = vkCreateImage(device_, &ci, nullptr, &im.image);
    if (r != VK_SUCCESS) return false;
    if (!AllocImage(im.image, im.alloc)) r = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (r == VK_SUCCESS) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = im.image;
        vi.viewType = view;
        vi.format = ci.format;
        vi.subresourceRange = {view_aspect, 0, ci.mipLevels, 0, ci.arrayLayers};
        r = vkCreateImageView(device_, &vi, nullptr, &im.view);
    }
    if (r != VK_SUCCESS) {
        DestroyImage(im);
        return false;
    }
    im.levels = ci.mipLevels;
    im.layers = ci.arrayLayers;
    im.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

bool VkBackend::AllocImage(VkImage image, VmaAllocation& alloc) {
    // device-local when there is room, else any memory the image can live in (as before: integrated GPUs)
    VmaAllocationCreateInfo ai{};
    ai.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkResult r = vmaAllocateMemoryForImage(vma_, image, &ai, &alloc, nullptr);
    if (r == VK_SUCCESS) r = vmaBindImageMemory(vma_, alloc, image);
    if (r != VK_SUCCESS) {
        if (alloc) vmaFreeMemory(vma_, alloc);
        alloc = VK_NULL_HANDLE;
        LOG_FIRST(4, "vulkan: an image's memory could not be allocated (%s)", ResultName(r));
        return false;
    }
    return true;
}

// as Direct3D 12's: above 2048 textures, every 60 presents, the ones unused for 600 frames go (their images retired
// with the frame, as it may still read them); a texture used again is simply made again (S3: textures_ only grew)
void VkBackend::ReleaseOldTextures() {
    constexpr uint64_t kSweepEvery = 60, kIdleFrames = 600;
    constexpr size_t kTexturesKept = 2048;
    if (presents_ % kSweepEvery != 0 || textures_.size() <= kTexturesKept) return;
    size_t before = textures_.size();
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (it->second.frame + kIdleFrames < presents_) {
            Retire(it->second.img);
            Retire(it->second.own);
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
    LOG_FIRST(2, "vulkan: %zu textures unused for 10 s released (%zu kept)", before - textures_.size(), textures_.size());
}

void VkBackend::DestroyImage(Image& im) {
    if (im.view) frames_[frame_].set_cache.clear();  // a set may name the view; a new view can take its handle
    if (im.view) vkDestroyImageView(device_, im.view, nullptr);
    if (im.image) vkDestroyImage(device_, im.image, nullptr);
    if (im.alloc) vmaFreeMemory(vma_, im.alloc);
    im = Image{};
}

// an image destroyed once the frames in flight are done with it
void VkBackend::Retire(Image& im) {
    if (im.image) (cap_ ? cap_hold_ : frames_[frame_].garbage).push_back(im);
    im = Image{};
}

void VkBackend::Barrier(VkImage image, const VkImageSubresourceRange& range, VkImageLayout& cur, VkImageLayout want) {
    EndRendering();
    VkCommandBuffer cb = Cmd();
    if (!cb) return;
    if (cur == want) {
        // a texture's uploads call this per level and face, separate regions that need no order among themselves (a
        // full barrier each was 60 for a cube with 10 levels: review of S4); whole images written again come through
        // ToLayout, which orders them
        if (want == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) writes_open_ = true;
        return;
    }
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = cur;
    b.newLayout = want;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = range;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    cur = want;
    if (WritableLayout(want)) writes_open_ = true;
}

// size bytes in this frame's upload ring; may_flush: when it is full, the recorded work is finished first and the ring
// starts over (only safe before anything of the current draw sits in the ring)
bool VkBackend::Upload(VkDeviceSize size, VkDeviceSize align, bool may_flush, VkDeviceSize& off, uint8_t*& cpu) {
    if (!Cmd()) return false;
    VkDeviceSize at = (frames_[frame_].used + align - 1) & ~(align - 1);
    if (at + size > kUploadSize) {
        if (!may_flush || size > kUploadSize) return false;
        Submit(true);
        if (!Cmd()) return false;
        at = 0;
    }
    Frame& f = frames_[frame_];
    off = at;
    cpu = f.upload_cpu + at;
    f.used = at + size;
    return true;
}

bool VkBackend::UploadConstants(const void* data, VkDeviceSize size, LastUpload& last, VkDeviceSize& off) {
    if (!Cmd()) return false;
    if (last.gen == ring_gen_ && last.bytes.size() == size_t(size) && !std::memcmp(last.bytes.data(), data, size_t(size))) {
        off = last.off;
        return true;
    }
    uint8_t* cpu;
    if (!Upload(size, ubo_align_, false, off, cpu)) return false;
    last.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size_t(size));
    std::memcpy(cpu, data, size_t(size));
    last.gen = ring_gen_;
    last.off = off;
    return true;
}

VkShaderModule VkBackend::ShaderModule(uint64_t key, const std::string& hlsl, spirv::Stage stage, const char* entry) {
    auto it = modules_.find(key);
    if (it != modules_.end()) return it->second;
    std::vector<uint32_t> code;
    std::string error;
    VkShaderModule m = VK_NULL_HANDLE;
    // S15 (D20): compiled once and kept in the shader cache, so a shader first used mid-play does not stall glslang
    // again in later runs
    uint64_t h = SpvHash(hlsl, entry, stage);
    bool ok = ReadSpv(h, code);
    if (!ok) {
        ok = spirv::Compile(hlsl, entry, stage, code, error);
        if (!ok)
            LOG_FIRST(8, "vulkan: a %s shader did not compile: %s", stage == spirv::Stage::Vertex ? "vertex" : "pixel",
                      error.c_str());
        else if (!error.empty())
            LOG_FIRST(4, "vulkan: a %s shader: %s", stage == spirv::Stage::Vertex ? "vertex" : "pixel", error.c_str());
        // (a shader the legaliser could not fix still runs, as before, but is not cached: the next run tries again
        // and logs why, review)
        if (ok && error.empty()) WriteSpv(h, code);
    }
    if (ok) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = code.size() * 4;
        ci.pCode = code.data();
        if (vkCreateShaderModule(device_, &ci, nullptr, &m) != VK_SUCCESS) m = VK_NULL_HANDLE;
    }
    modules_[key] = m;  // a failure is remembered, not compiled again every draw
    if (m) module_hash_[m] = h;
    return m;
}

// a stage's texture: a target the GPU drew (a copy when the draw draws into it), else the guest texture uploaded
void VkBackend::BindTexture(const State& s, int i, Target* rt, Target* ds, StageBind& sb, TexInfo& info) {
    info = {};
    sb = {};
    TextureDesc d;
    if (!s.textures[size_t(i)] || !Describe(s.textures[size_t(i)], d)) return;
    const FormatInfo& fi = Format(d.format);
    info.kind = TextureKind(d);
    info.rect = d.linear;
    sb.width = d.width;
    sb.height = d.height;
    auto tit = targets_.find(SizedKey(d.data, fi.depth, d.width, d.height));
    if (tit != targets_.end() && tit->second.gw == d.width && tit->second.gh == d.height && !d.cube && d.depth == 1 &&
        !fi.compressed) {
        Target& t = tit->second;
        if (&t == ds) return;  // the depth buffer being drawn with: not read, as D3D12 does
        target_used_[d.data] = presents_;
        // what the CPU wrote into it since goes in first (as Direct3D 12's BindTargetTexture)
        if (!t.depth && CpuWrote(t)) {
            Surface su;
            su.header = s.textures[size_t(i)];
            UploadTarget(t, su);
        }
        info.kind = kTex2D;
        sb.target = true;
        // a pass shrinking an upscaled target averages each tap over a console texel, else its taps skip texels and
        // the glow chains ghost (Direct3D 12's kTexWide, render scale above 100%)
        float k = float(t.w) / float(std::max(t.gw, 1u));
        if (!t.depth && k > 1.01f && rt && rt != &t && (t.gw > rt->gw || t.gh > rt->gh)) {
            sb.wide = k;
            info.flags |= kTexWide;
        }
        if (g_extension) {  // the game's flags for this target (The Two Towers' water: read where its coordinates say)
            gpu::Target desc;
            desc.depth = t.depth;
            desc.gw = t.gw;
            desc.gh = t.gh;
            desc.w = t.w;
            desc.h = t.h;
            desc.data = t.data;
            g_extension->Sampled(d, &desc, 0, info);
        }
        if (t.depth) {
            info.shadow = (d.format == 0x2C || d.format == 0x30) ? 2 : 1;  // D16, LIN_D16
            ToLayout(t, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            sb.view = t.sample_view;
            sb.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            return;
        }
        if (&t == rt) {
            // the console reads the pixels it is about to overwrite: the draw reads a copy
            if (!t.fb.image) {
                VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                ci.imageType = VK_IMAGE_TYPE_2D;
                ci.format = kColorFormat;
                ci.extent = {t.w, t.h, 1};
                ci.mipLevels = 1;
                ci.arrayLayers = 1;
                ci.samples = VK_SAMPLE_COUNT_1_BIT;
                ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                if (!MakeImage(t.fb, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) return;
            }
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            ToLayout(t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            Barrier(t.fb.image, range, t.fb.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageCopy c{};
            c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.extent = {t.w, t.h, 1};
            vkCmdCopyImage(Cmd(), t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.fb.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            Barrier(t.fb.image, range, t.fb.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            sb.view = t.fb.view;
            sb.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            return;
        }
        ToLayout(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sb.view = t.view;
        sb.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return;
    }
    if (fi.depth) info.shadow = (d.format == 0x2C || d.format == 0x30) ? 2 : 1;
    if (fi.host == Host::None) return;
    sb.view = GuestTexture(s, i, d, fi, sb);
    sb.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (g_extension) g_extension->Sampled(d, nullptr, 0, info);
    // no view: the stage reads the empty texture of its kind (Draw)
}

// a texture in guest memory: made again when its layout changed, uploaded again when its bytes did
VkImageView VkBackend::GuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi, StageBind& sb) {
    // a P8 texture's colours
    uint32_t colours[256] = {};
    uint32_t pal_va = 0;
    uint64_t pal_hash = 0;
    if (d.format == 0x0B && s.palettes[size_t(stage)]) {
        uint32_t header = s.palettes[size_t(stage)];
        pal_va = mem::VirtualFromPhysical(kernel::R32(header + 4));
        size_t bytes = (256u >> (kernel::R32(header) >> 30)) * sizeof(uint32_t);
        std::memcpy(colours, kernel::HostPtr(pal_va), bytes);
        pal_hash = Hash(colours, sizeof(colours));
    }
    uint64_t key = uint64_t(pal_va) << 32 | d.data;
    Tex& tx = textures_[key];
    bool same = tx.img.image && tx.d.format == d.format && tx.d.width == d.width && tx.d.height == d.height &&
                tx.d.depth == d.depth && tx.d.levels == d.levels && tx.d.cube == d.cube && tx.d.pitch == d.pitch;
    if (!same) {
        Retire(tx.img);
        Retire(tx.own);
        tx = Tex{};
        tx.d = d;
        bool bc = fi.compressed;
        uint32_t w = d.width, h = d.height;
        tx.levels = d.levels;
        if (bc && ((w & 3) || (h & 3))) {
            w = (w + 3) & ~3u;
            h = (h + 3) & ~3u;
            tx.levels = 1;
        }
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.flags = d.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        ci.imageType = d.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        ci.format = VkTextureFormat(fi.host);
        ci.extent = {w, h, d.depth};
        ci.mipLevels = tx.levels;
        ci.arrayLayers = d.cube ? 6 : 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VkImageViewType vt = d.cube ? VK_IMAGE_VIEW_TYPE_CUBE : d.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
        if (!MakeImage(tx.img, ci, vt, VK_IMAGE_ASPECT_COLOR_BIT)) {
            LOG_FIRST(8, "vulkan: a %ux%u texture (format %X) could not be made", w, h, d.format);
            textures_.erase(key);
            return VK_NULL_HANDLE;
        }
        tx.host_w = w;
        tx.host_h = h;
    }
    // change check once per frame: lock epochs plus the bytes (fonts the CPU draws into, hashed whole)
    if (tx.frame != presents_ + 1 || !same || tx.palette != pal_hash) {
        tx.frame = presents_ + 1;
        tx.palette = pal_hash;
        uint32_t total = TotalSize(d);
        const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(d.data));
        bool font_cache = (d.format == 0x19 || d.format == 0x1F) && total <= (1u << 20);
        uint64_t sig = (font_cache ? Hash(p, total, total) : MemSig(d.data, total)) ^ DirtyEpoch(d.data) ^ pal_hash;
        if (sig != tx.sig || !same) {
            tx.sig = UploadTexture(tx, p, pal_va ? colours : nullptr) ? sig : 0;  // a failed one is tried again
        }
    }
    // a replacement is sampled at its own size: the shader's coordinates are 0..1 (or divided by the console's size
    // for linear textures, texsize), so only smooth_2d's texel step needs the new size
    if (tx.repl && !g_pack_off) {  // (F8 / a F7 pair: the game's own texture instead)
        sb.host_w = tx.repl_w;
        sb.host_h = tx.repl_h;
        sb.levels = tx.repl_levels;
        return tx.repl;
    }
    sb.host_w = tx.host_w;
    sb.host_h = tx.host_h;
    sb.levels = tx.levels;
    return tx.img.view;
}

bool VkBackend::UploadTexture(Tex& tx, const uint8_t* p, const uint32_t* colours) {
    // the old contents' replacement isn't drawn for the new contents, even if this upload stops early (review)
    tx.repl = VK_NULL_HANDLE;
    tx.pack_hash = 0;
    const TextureDesc& d = tx.d;
    const FormatInfo& fi = Format(d.format);
    VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, tx.levels, 0, d.cube ? 6u : 1u};
    Barrier(tx.img.image, all, tx.img.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    std::vector<uint8_t> px;
    // a plain 2D texture in a format a DDS file holds can be dumped and replaced, as on D3D12
    pack::Format pf = fi.host == Host::BGRA8 ? pack::Format::BGRA8
                      : fi.host == Host::BC1 ? pack::Format::BC1
                      : fi.host == Host::BC2 ? pack::Format::BC2
                      : fi.host == Host::BC3 ? pack::Format::BC3
                                             : pack::Format::Unknown;
    // linear textures only for a game's own replacements (g_builtin_contents), never dumped
    bool packable = !d.cube && d.depth == 1 && (!d.linear || pack::g_builtin_contents) && pack::Supported(pf) &&
                    (pack_lib_.Size() || app::Get().texture_dump || pack::g_builtin || pack::g_builtin_contents ||
                     pack::g_upscale);
    std::vector<std::vector<uint8_t>> levels;
    for (uint32_t f = 0; f < (d.cube ? 6u : 1u); f++) {
        for (uint32_t l = 0; l < tx.levels; l++) {
            uint32_t pitch = 0;
            DecodeLevel(d, l, f, p, colours, px, pitch);
            if (packable && !d.linear && app::Get().texture_dump && f == 0 && l == 0) {
                // the level as decoded, rows tightly packed
                uint32_t rb = pack::RowBytes(pf, d.width), prow = pack::Rows(pf, d.height);
                std::vector<uint8_t>& out = levels.emplace_back(size_t(rb) * prow, 0);
                for (uint32_t y = 0; y < prow && size_t(y + 1) * pitch <= px.size(); y++)
                    std::memcpy(&out[size_t(y) * rb], &px[size_t(y) * pitch], std::min(rb, pitch));
            }
            uint32_t lw = std::max(tx.host_w >> l, 1u), lh = std::max(tx.host_h >> l, 1u);
            uint32_t ld = std::max(d.depth >> l, 1u);
            // the copy's rows as Vulkan reads them: whole blocks for BC, texels otherwise
            uint32_t rows = fi.compressed ? (lh + 3) / 4 : lh;
            uint32_t row_bytes = fi.compressed ? ((lw + 3) / 4) * (fi.host == Host::BC1 ? 8u : 16u)
                                               : lw * (fi.host == Host::R16Unorm ? 2u : 4u);
            VkDeviceSize bytes = VkDeviceSize(row_bytes) * rows * ld;
            VkDeviceSize off;
            uint8_t* cpu;
            if (!Upload(bytes, 16, true, off, cpu)) {
                LOG_FIRST(4, "vulkan: a texture level of %llu bytes did not fit the upload ring",
                          (unsigned long long)bytes);
                Barrier(tx.img.image, all, tx.img.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                return false;
            }
            size_t src_rows = pitch ? px.size() / pitch / ld : 0;
            for (uint32_t z = 0; z < ld; z++)
                for (uint32_t y = 0; y < rows; y++) {
                    uint8_t* dst = cpu + (size_t(z) * rows + y) * row_bytes;
                    if (y < src_rows) std::memcpy(dst, &px[(size_t(z) * src_rows + y) * pitch], std::min(pitch, row_bytes));
                    else std::memset(dst, 0, row_bytes);
                }
            VkBufferImageCopy c{};
            c.bufferOffset = off;
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, f, 1};
            c.imageExtent = {lw, lh, ld};
            // the upload may have finished the frame and begun another: barriers again in the new command buffer
            Barrier(tx.img.image, all, tx.img.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdCopyBufferToImage(Cmd(), frames_[frame_].upload, tx.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &c);
        }
    }
    Barrier(tx.img.image, all, tx.img.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // the old replacement's view belongs to pack_cache_ or tx.own, which outlive the frames in flight
    tx.repl = VK_NULL_HANDLE;
    tx.pack_hash = 0;
    if (packable) TexturePack(tx, p, colours, levels);
    return true;
}

// the texture's contents hashed (with its format, size and a P8 texture's colours), the same hash as D3D12's: written
// to textures/dump with texture_dump on, and a file of that name under textures/ drawn instead
void VkBackend::TexturePack(Tex& tx, const uint8_t* p, const uint32_t* colours,
                            const std::vector<std::vector<uint8_t>>& levels) {
    const TextureDesc& d = tx.d;
    const FormatInfo& fi = Format(d.format);
    pack::Format pf = fi.host == Host::BGRA8 ? pack::Format::BGRA8
                      : fi.host == Host::BC1 ? pack::Format::BC1
                      : fi.host == Host::BC2 ? pack::Format::BC2
                                             : pack::Format::BC3;
    uint64_t seed = uint64_t(d.format) | uint64_t(d.width) << 8 | uint64_t(d.height) << 24 | uint64_t(d.levels) << 40;
    uint64_t h = Hash(p, TotalSize(d), seed);
    if (colours) h = Hash(colours, 256 * sizeof(uint32_t), h);
    if (!levels.empty() && dumped_.insert(h).second) {
        // level 0 as a PNG, written on the pack thread
        std::vector<uint8_t> rgba;
        std::filesystem::path file = app::TextureDir() / "dump" / pack::PngName(d.width, d.height, h);
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) {
        } else if (pack::ToRgba(pf, d.width, d.height, levels[0], rgba)) {
            pack::WritePngLater(file, d.width, d.height, std::move(rgba));
        } else {
            LOG_FIRST(4, "texture pack: %s is in a format a PNG does not take", file.filename().string().c_str());
        }
    }
    tx.pack_hash = h;
    const std::filesystem::path* file = pack_lib_.Find(h);
    if (!file && !pack::g_builtin && !pack::g_builtin_contents && !pack::g_upscale) return;
    if (!file && d.linear && tx.own.image && tx.own_hash == h) {
        // the same contents as last time: the picture made then
        tx.repl = tx.own.view;
        tx.repl_w = tx.own_w;
        tx.repl_h = tx.own_h;
        tx.repl_levels = tx.own.levels;
        return;
    }
    auto [it, fresh] = pack_cache_.try_emplace(h);
    if (fresh && file && !LoadPackTexture(*file, it->second))
        LOGW("texture pack: %s is not a PNG or DDS file the renderer takes", file->string().c_str());
    if (fresh && !file) {
        pack::Image img;
        bool got = pack::g_builtin_contents &&
                   pack::g_builtin_contents({d.format, d.width, d.height, d.levels, p, TotalSize(d), colours}, img);
        if (!got && pack::g_builtin) got = pack::g_builtin(h, d.width, d.height, img);
        if (!got && pack::g_upscale && !d.linear && pack::g_upscale(h)) {
            // a picture the game asks to be redrawn larger: level 0 decoded, upscaled 4x, its levels made anew
            std::vector<uint8_t> px, rgba;
            uint32_t pitch;
            DecodeLevel(d, 0, 0, p, colours, px, pitch);
            uint32_t rb = pack::RowBytes(pf, d.width), rows = pack::Rows(pf, d.height);
            std::vector<uint8_t> packed(size_t(rb) * rows);
            for (uint32_t y = 0; y < rows && size_t(y + 1) * pitch <= px.size(); y++)
                std::memcpy(&packed[size_t(y) * rb], &px[size_t(y) * pitch], std::min(rb, pitch));
            if (pack::ToRgba(pf, d.width, d.height, packed, rgba)) {
                img = {};
                img.width = d.width * 4;
                img.height = d.height * 4;
                img.format = pack::Format::RGBA8;
                pack::Upscale(rgba, d.width, d.height, 4, img.levels.emplace_back());
                pack::MakeLevels(img);
                got = true;
                LOGR("texture: %ux%u picture %016llx redrawn at 4x", d.width, d.height, (unsigned long long)h);
            }
        }
        if (got && d.linear) {
            // a game's own picture for a linear texture (text it draws afresh, Fellowship's sharper text): kept by
            // this texture alone, not by contents, as such textures change all the time
            pack_cache_.erase(it);
            Retire(tx.own);  // may still be drawn by a frame in flight
            tx.own_hash = 0;
            if (!CreatePackTexture(img, pack::PngName(d.width, d.height, h), tx.own)) return;
            tx.own_w = img.width;
            tx.own_h = img.height;
            tx.own_hash = h;
            tx.repl = tx.own.view;
            tx.repl_w = tx.own_w;
            tx.repl_h = tx.own_h;
            tx.repl_levels = tx.own.levels;
            return;
        }
        if (!got && d.linear) {  // not kept: linear textures are often movie frames, each new
            pack_cache_.erase(it);
            return;
        }
        if (got && CreatePackTexture(img, pack::PngName(d.width, d.height, h), it->second.img)) {
            it->second.w = img.width;
            it->second.h = img.height;
        }
    }
    if (!it->second.img.view) return;
    tx.repl = it->second.img.view;
    tx.repl_w = it->second.w;
    tx.repl_h = it->second.h;
    tx.repl_levels = it->second.img.levels;
}

// a PNG or DDS file into a texture of its own size: a DDS file's mip levels, or ones made for a PNG
bool VkBackend::LoadPackTexture(const std::filesystem::path& file, PackTex& out) {
    pack::Image img;
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (!(ext == ".png" ? pack::ReadPng(file, img) : pack::ReadDds(file, img))) return false;
    if (!CreatePackTexture(img, file.filename().string(), out.img)) return false;
    out.w = img.width;
    out.h = img.height;
    return true;
}

// a picture, with its mip levels, into an image of its own size. Each level goes through the upload ring, which is
// finished and started over when full: a level up to the ring's whole size fits (4096x4096 RGBA is 64 MB, exactly the
// ring), so a 2048x2048 picture with its levels (~22 MB) takes at most one extra submit
bool VkBackend::CreatePackTexture(const pack::Image& img, const std::string& name, Image& out) {
    VkFormat fmt;
    uint32_t block = 0;  // bytes per 4x4 block, 0 for 32 bit texels
    switch (img.format) {
        case pack::Format::BGRA8: fmt = VK_FORMAT_B8G8R8A8_UNORM; break;
        case pack::Format::RGBA8: fmt = VK_FORMAT_R8G8B8A8_UNORM; break;
        case pack::Format::BC1: fmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; block = 8; break;
        case pack::Format::BC2: fmt = VK_FORMAT_BC2_UNORM_BLOCK; block = 16; break;
        case pack::Format::BC3: fmt = VK_FORMAT_BC3_UNORM_BLOCK; block = 16; break;
        case pack::Format::BC7: fmt = VK_FORMAT_BC7_UNORM_BLOCK; block = 16; break;
        default: return false;
    }
    if (img.levels.empty() || !img.width || !img.height) return false;
    if (block && ((img.width & 3) || (img.height & 3))) return false;  // BC level 0 must be whole blocks
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {img.width, img.height, 1};
    ci.mipLevels = uint32_t(img.levels.size());
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    Image im;
    if (!MakeImage(im, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) {
        LOG_FIRST(4, "texture pack: %s (%ux%u) could not be made", name.c_str(), img.width, img.height);
        return false;
    }
    VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, ci.mipLevels, 0, 1};
    for (uint32_t l = 0; l < ci.mipLevels; l++) {
        uint32_t lw = std::max(img.width >> l, 1u), lh = std::max(img.height >> l, 1u);
        size_t want = size_t(pack::RowBytes(img.format, lw)) * pack::Rows(img.format, lh);
        const std::vector<uint8_t>& src = img.levels[l];
        VkDeviceSize off;
        uint8_t* cpu;
        if (src.size() < want || !Upload(want, 16, true, off, cpu)) {
            LOG_FIRST(4, "texture pack: %s level %u (%zu bytes) did not fit the upload ring", name.c_str(), l, want);
            Retire(im);  // earlier levels' copies may be recorded already
            return false;
        }
        std::memcpy(cpu, src.data(), want);
        VkBufferImageCopy c{};
        c.bufferOffset = off;
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
        c.imageExtent = {lw, lh, 1};
        // the upload may have finished the frame and begun another: barriers again in the new command buffer
        Barrier(im.image, all, im.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(Cmd(), frames_[frame_].upload, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    }
    Barrier(im.image, all, im.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    out = im;
    LOG_FIRST(16, "texture pack: %s, %ux%u, %u levels", name.c_str(), img.width, img.height, ci.mipLevels);
    return true;
}

// D3DTEXF: 1 point, 2 linear, 3 anisotropic
VkSampler VkBackend::StageSampler(const State& s, int i, const TexInfo& info, const StageBind& sb, bool clamp,
                                    float extra, bool enlarge_smooth) {
    constexpr uint32_t kTexfLinear = 2, kTexfAnisotropic = 3, kTaddressWrap = 1, kTaddressClamp = 3;
    const auto& t = s.tss[size_t(i)];
    bool target = sb.target;
    uint32_t mag = t[kTssMagFilter], min = t[kTssMinFilter], mip = t[kTssMipFilter];
    auto lin = [](uint32_t f) { return f >= kTexfLinear; };
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = lin(mag) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.minFilter = lin(min) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    // smooth cut-out edges ([graphics] alpha_edges): an alpha-tested picture's first stage enlarged filtered
    if (g_alpha_edges && SimpleState(s, kMAlphaTestEnable) && i == 0) si.magFilter = VK_FILTER_LINEAR;
    si.mipmapMode = mip == kTexfLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    // clamp: the draw's coordinates all lie in 0..1 (UnitTexcoords), so wrap is read as clamp
    si.addressModeU = VkAddress(clamp && t[kTssAddressU] == kTaddressWrap ? kTaddressClamp : t[kTssAddressU]);
    si.addressModeV = VkAddress(clamp && t[kTssAddressV] == kTaddressWrap ? kTaddressClamp : t[kTssAddressV]);
    si.addressModeW = VkAddress(t[kTssAddressW]);
    si.mipLodBias = Bits(t[kTssMipLodBias]) + app::Get().lod_bias + (target ? 0.0f : extra);
    si.minLod = float(t[kTssMaxMipLevel]);
    si.maxLod = mip ? VK_LOD_CLAMP_NONE : si.minLod;
    // Vulkan's border is one of three colours: the nearest of transparent black, opaque black, opaque white
    uint32_t bc = t[kTssBorderColor];
    si.borderColor = (bc >> 24) < 0x80 ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK
                     : (bc & 0xFFFFFF) >= 0x808080 ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                                   : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    bool anisotropic = min == kTexfAnisotropic || mag == kTexfAnisotropic;
    uint32_t an = anisotropic ? std::max<uint32_t>(t[kTssMaxAnisotropy], 1) : 1;
    // forced only where the title filters across mip levels and the texture has them (as Direct3D 12)
    if (app::Get().aniso && lin(min) && mip && sb.levels > 1 && !info.shadow) an = app::Get().aniso;
    bool aniso_on = false;
    if (an > 1) {
        // Direct3D 12's ANISOTROPIC filter: linear in every direction and across levels
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        if (anisotropy_) {
            si.anisotropyEnable = VK_TRUE;
            si.maxAnisotropy = float(std::min(an, 16u));
        }
        aniso_on = true;
    }
    // smooth_2d: the bicubic taps need a linear sampler, on the top level as the taps read it
    if (info.flags & kTexSmooth) {
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.anisotropyEnable = VK_FALSE;
        si.maxAnisotropy = 0;
        aniso_on = false;
    }
    if (info.shadow) {
        // the console tests texel OP r, Vulkan tests r OP texel: the order of the comparison swapped
        static const uint8_t swap[8] = {0, 4, 2, 6, 1, 5, 3, 7};
        si.compareEnable = VK_TRUE;
        si.compareOp = VkCompareOp(swap[VkCmp(s.rs[kRsShadowFunc])]);
        si.magFilter = si.minFilter = lin(min) || lin(mag) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        si.anisotropyEnable = VK_FALSE;
        si.maxAnisotropy = 0;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.maxLod = si.minLod;  // (minLod may be the stage's max mip level: never above maxLod)
    }
    // Above the console's 480 lines, textures a 3D draw reads unfiltered would be enlarged into blocks the console
    // never showed (Direct3D 12's EnsureStageSamplers): "linear when shrunk, point when enlarged" is enlarged linearly
    // too; one-bit-alpha cut-outs read point both ways in an alpha-tested draw are read linearly
    static const bool keep_point = std::getenv("XBE_KEEP_POINT") != nullptr;
    if (enlarge_smooth && !keep_point && !target && !info.shadow && !aniso_on && !(info.flags & kTexSmooth) &&
        si.magFilter == VK_FILTER_NEAREST) {
        if (si.minFilter == VK_FILTER_LINEAR) {
            si.magFilter = VK_FILTER_LINEAR;
        } else if (SimpleState(s, kMAlphaTestEnable)) {
            TextureDesc td;
            // A1R5G5B5 (swizzled 0x02, linear 0x10) and DXT1 (0x0C): one-bit alpha
            if (s.textures[size_t(i)] && Describe(s.textures[size_t(i)], td) &&
                (td.format == 0x02 || td.format == 0x10 || td.format == 0x0C))
                si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        }
        if (si.magFilter == VK_FILTER_LINEAR)
            LOG_FIRST(1, "vulkan: textures the game enlarges unfiltered are enlarged smoothly (above 576 lines)");
    }
    uint64_t key = Hash(&si, sizeof(si));
    auto it = samplers_.find(key);
    if (it != samplers_.end()) return it->second;
    VkSampler smp = VK_NULL_HANDLE;
    if (samplers_.size() >= sampler_cap_ || vkCreateSampler(device_, &si, nullptr, &smp) != VK_SUCCESS) {
        LOG_FIRST(2, "vulkan: sampler limit reached, a plain sampler is used");
        return info.shadow ? compare_sampler_ : plain_sampler_;
    }
    samplers_[key] = smp;
    return smp;
}

VkPipeline VkBackend::Pipeline(const PipeKey& k, VkShaderModule vs, VkShaderModule ps) {
    uint64_t key = Hash(&k, sizeof(k));
    auto it = pipelines_.find(key);
    if (it != pipelines_.end()) return it->second;
    VkPipeline p = VK_NULL_HANDLE;
    {
        // made by the warm-up already; else the warm-up is told not to make it
        std::lock_guard lock(warm_mu_);
        auto w = warm_pipes_.find(key);
        if (w != warm_pipes_.end()) {
            p = w->second;
            warm_pipes_.erase(w);
            made_warm_++;
        } else {
            warm_taken_.insert(key);
        }
    }
    if (!p) {
        p = MakePipeline(k, vs, ps);
        if (p) {
            made_drawing_++;
            unsaved_++;
        }
    }
    pipelines_[key] = p;
    // listed for the next start's warm-up (the list stops growing at 50000)
    if (p && pipe_list_ && listed_.size() < 50000 && listed_.insert(key).second) {
        std::fwrite(&k, sizeof(k), 1, pipe_list_);
        std::fflush(pipe_list_);
    }
    return p;
}

VkPipeline VkBackend::MakePipeline(const PipeKey& k, VkShaderModule vs, VkShaderModule ps) {
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "main", nullptr};

    VkVertexInputBindingDescription vb[16]{};
    // point sprites: each point an instance of a 6 vertex square (Topology::Sprites)
    VkVertexInputRate rate = k.topo == uint32_t(Topology::Sprites) ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
    for (uint32_t b = 0; b < k.buffers; b++) vb[b] = {b, k.strides[b], rate};
    VkVertexInputAttributeDescription va[16]{};
    uint32_t n = 0;
    for (uint32_t r = 0; r < 16; r++) {
        if (!((k.regs >> r) & 1)) continue;
        uint32_t e = k.elements[r];
        // locations in register order: glslang numbers the VIn (VRaw) members as declared, ascending registers
        va[n] = {n, (e >> 8) & 0xFF, VkVertexFormat(e & 0xFF), e >> 16};
        n++;
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = k.buffers;
    vi.pVertexBindingDescriptions = vb;
    vi.vertexAttributeDescriptionCount = n;
    vi.pVertexAttributeDescriptions = va;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = k.topo == 0   ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                  : k.topo == 1 ? (k.strip ? VK_PRIMITIVE_TOPOLOGY_LINE_STRIP : VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
                                : (k.strip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = k.fill == kFillWireframe && fill_lines_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rs.cullMode = k.cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    // drawn through a viewport of negative height the picture stands as on D3D12, and so do the windings
    rs.frontFace = k.ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rs.depthBiasEnable = (k.zbias || k.zslope) ? VK_TRUE : VK_FALSE;
    rs.depthBiasConstantFactor = float(k.zbias);
    rs.depthBiasSlopeFactor = k.zslope;
    rs.lineWidth = 1.0f;
    // a float depth buffer counts the constant bias in steps that depend on the depth; the 24-bit unit forced, as
    // Direct3D 12 and the console count it (D16; without the extension the offset near the camera differs, logged)
    VkDepthBiasRepresentationInfoEXT bias{VK_STRUCTURE_TYPE_DEPTH_BIAS_REPRESENTATION_INFO_EXT};
    if (rs.depthBiasEnable && depth_fmt_ == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        if (bias_unorm_) {
            bias.depthBiasRepresentation = VK_DEPTH_BIAS_REPRESENTATION_LEAST_REPRESENTABLE_VALUE_FORCE_UNORM_EXT;
            rs.pNext = &bias;
        } else {
            LOG_FIRST(1, "vulkan: depth offsets on a float depth buffer without VK_EXT_depth_bias_control: decals may "
                         "differ slightly from Direct3D 12 near the camera");
        }
    }

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VkSampleCountFlagBits(std::max(k.samples, 1u));
    ms.alphaToCoverageEnable = k.a2c ? VK_TRUE : VK_FALSE;

    VkStencilOpState so{VkStencil(k.sfail), VkStencil(k.spass), VkStencil(k.szfail), VkCmp(k.sfunc), k.smask, k.swmask, 0};
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = k.zen;
    ds.depthWriteEnable = k.zen && k.zwrite;
    ds.depthCompareOp = k.zen ? VkCmp(k.zfunc) : VK_COMPARE_OP_ALWAYS;
    ds.stencilTestEnable = k.sten;
    ds.front = ds.back = so;

    VkPipelineColorBlendAttachmentState cb{};
    cb.blendEnable = k.blend;
    cb.srcColorBlendFactor = VkBlend(k.src, false);
    cb.dstColorBlendFactor = VkBlend(k.dst, false);
    cb.colorBlendOp = VkBlendOpOf(k.op);
    cb.srcAlphaBlendFactor = VkBlend(k.src, true);
    cb.dstAlphaBlendFactor = VkBlend(k.dst, true);
    cb.alphaBlendOp = VkBlendOpOf(k.op);
    cb.colorWriteMask = k.wmask;  // R G B A bits, as D3D12's
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = k.has_rt ? 1 : 0;
    bs.pAttachments = &cb;

    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                            VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 4;
    dy.pDynamicStates = dyn;

    VkFormat color = kColorFormat;
    VkPipelineRenderingCreateInfoKHR ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    ri.colorAttachmentCount = k.has_rt ? 1 : 0;
    ri.pColorAttachmentFormats = &color;
    ri.depthAttachmentFormat = k.has_ds ? depth_fmt_ : VK_FORMAT_UNDEFINED;
    ri.stencilAttachmentFormat = k.has_ds ? depth_fmt_ : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.pNext = &ri;
    pi.stageCount = 2;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &bs;
    pi.pDynamicState = &dy;
    pi.layout = pipe_layout_;
    VkPipeline p = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(device_, pipe_cache_, 1, &pi, nullptr, &p);
    if (r != VK_SUCCESS) {
        LOG_FIRST(8, "vulkan: a pipeline could not be made (%s)", ResultName(r));
        p = VK_NULL_HANDLE;
    }
    return p;
}

// ---- S15: warm-up and the saved pipeline cache

// the list from earlier runs (and vk_pipelines.seed beside the exe, the same layout) made again on a thread of its
// own, below normal priority, after the seed's shaders not in the cache yet; a draw that needs one first takes it
void VkBackend::StartWarmUp() {
    std::filesystem::path file = app::ShaderCacheDir() / "vk_pipelines.bin";
    PipeListHeader want;
    want.key_size = sizeof(PipeKey);
    std::vector<PipeKey> keys;
    auto read = [&](const std::filesystem::path& from, bool own) {
        FILE* f = _wfopen(from.c_str(), L"rb");
        if (!f) return false;
        PipeListHeader have;
        bool ok = std::fread(&have, sizeof(have), 1, f) == 1 && !std::memcmp(&have, &want, sizeof(want));
        PipeKey k;
        while (ok && std::fread(&k, sizeof(k), 1, f) == 1) {
            uint64_t h = Hash(&k, sizeof(k));
            if (listed_.count(h)) continue;
            if (own) listed_.insert(h);
            keys.push_back(k);
        }
        std::fclose(f);
        return ok;
    };
    bool fresh = !read(file, true);
    size_t own = keys.size();
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // a list from another build's key layout (or a damaged one) is started again
    pipe_list_ = _wfopen(file.c_str(), fresh ? L"wb" : L"ab");
    if (pipe_list_ && fresh) {
        std::fwrite(&want, sizeof(want), 1, pipe_list_);
        std::fflush(pipe_list_);
    }
    if (!pipe_list_) LOGW("vulkan: the pipeline list %s can't be written; no warm-up next time", file.string().c_str());
    // the release's seed after the player's own (not listed: a seed pipeline the game never makes stays out of it)
    read(app::ExeDir() / "vk_pipelines.seed", false);
    std::vector<SeedSrc> seed = ReadShaderSeed(app::ExeDir() / "shaders.seed");
    if (keys.empty() && seed.empty()) return;
    LOGR("vulkan: warming up %zu pipelines from earlier runs and %zu from the seed, %zu seed shaders", own,
         keys.size() - own, seed.size());
    warm_ = std::thread([this, keys = std::move(keys), seed = std::move(seed)]() mutable {
        WarmUp(std::move(keys), std::move(seed));
    });
}

void VkBackend::WarmUp(std::vector<PipeKey> keys, std::vector<SeedSrc> seed) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    auto start = std::chrono::steady_clock::now();
    auto secs = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    // the seed's shaders not in the cache yet, on a few threads (half the processors, at most 4)
    if (!seed.empty()) {
        std::vector<size_t> todo;
        for (size_t i = 0; i < seed.size(); i++) {
            std::error_code ec;
            spirv::Stage st = seed[i].profile[0] == 'v' ? spirv::Stage::Vertex : spirv::Stage::Pixel;
            if (!std::filesystem::exists(SpvPath(SpvHash(seed[i].src, seed[i].entry.c_str(), st)), ec)) todo.push_back(i);
        }
        std::atomic<size_t> next{0}, made{0};
        unsigned n = std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u);
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < n && !todo.empty(); t++)
            pool.emplace_back([&] {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                for (size_t i; !warm_stop_ && (i = next++) < todo.size();) {
                    const SeedSrc& s = seed[todo[i]];
                    spirv::Stage st = s.profile[0] == 'v' ? spirv::Stage::Vertex : spirv::Stage::Pixel;
                    std::vector<uint32_t> code;
                    std::string err;
                    if (!spirv::Compile(s.src, s.entry.c_str(), st, code, err)) continue;
                    WriteSpv(SpvHash(s.src, s.entry.c_str(), st), code);
                    made++;
                }
            });
        for (auto& t : pool) t.join();
        if (!todo.empty())
            LOGR("vulkan: compiled %zu seed shaders in %.1f s (%zu were cached already)", made.load(), secs(),
                 seed.size() - todo.size());
    }
    // the pipelines, their shaders read from the cache (a module of the thread's own, destroyed at the end)
    std::unordered_map<uint64_t, VkShaderModule> mods;
    auto mod = [&](uint64_t h) {
        auto it = mods.find(h);
        if (it != mods.end()) return it->second;
        VkShaderModule m = VK_NULL_HANDLE;
        std::vector<uint32_t> code;
        if (ReadSpv(h, code)) {
            VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            ci.codeSize = code.size() * 4;
            ci.pCode = code.data();
            if (vkCreateShaderModule(device_, &ci, nullptr, &m) != VK_SUCCESS) m = VK_NULL_HANDLE;
        }
        mods[h] = m;
        return m;
    };
    size_t made = 0, missing = 0;
    for (const PipeKey& k : keys) {
        if (warm_stop_) break;
        uint64_t h = Hash(&k, sizeof(k));
        {
            std::lock_guard lock(warm_mu_);
            if (warm_taken_.count(h)) continue;
        }
        VkShaderModule vs = mod(k.vs), ps = mod(k.ps);
        if (!vs || !ps) {
            missing++;
            continue;
        }
        VkPipeline p = MakePipeline(k, vs, ps);
        if (!p) continue;
        std::lock_guard lock(warm_mu_);
        if (warm_taken_.insert(h).second) {
            warm_pipes_[h] = p;
            made++;
        } else {
            vkDestroyPipeline(device_, p, nullptr);  // a draw made it meanwhile
        }
    }
    for (auto& [h, m] : mods)
        if (m) vkDestroyShaderModule(device_, m, nullptr);
    LOGR("vulkan: warm-up made %zu pipelines in %.1f s%s", made, secs(),
         missing ? " (some cached shaders were missing)" : "");
}

// the driver's cache to vk_pipeline_cache.bin (its own header names the card, driver and cache layout)
void VkBackend::SavePipelineCache() {
    if (!pipe_cache_ || !device_) return;
    size_t n = 0;
    if (vkGetPipelineCacheData(device_, pipe_cache_, &n, nullptr) != VK_SUCCESS || n < 32) return;
    std::vector<uint8_t> data(n);
    if (vkGetPipelineCacheData(device_, pipe_cache_, &n, data.data()) != VK_SUCCESS) return;
    data.resize(n);
    WriteFileAtomic(app::ShaderCacheDir() / "vk_pipeline_cache.bin", data.data(), data.size());
}

// a vertex shader's output with PointSize 1 (the shared VOut struct, every "return o;")
static std::string PointSizeHlsl(std::string src) {
    const std::string pos = "    float4 pos : SV_Position;\n";
    size_t at = src.find("struct VOut {\n" + pos);
    if (at == std::string::npos) return src;
    src.insert(at + 14 + pos.size(), "    [[vk::builtin(\"PointSize\")]] float psize : PSIZE;\n");
    std::string out;
    size_t from = 0;
    for (size_t r; (r = src.find("    return o;", from)) != std::string::npos; from = r + 13) {
        out.append(src, from, r - from);
        out += "    o.psize = 1.0;\n    return o;";
    }
    out.append(src, from, std::string::npos);
    return out;
}

void VkBackend::Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) {
    PerfAdd perf{draw_ms_};
    if (lost_ || !device_) return;
    if (!dynamic_rendering_) {
        LOG_FIRST(1, "vulkan: draws need dynamic rendering (render passes are a later step)");
        return;
    }
    if (g_draw_observer) g_draw_observer(s);
    if (cap_) CapStep(s.rt);  // (it waits for the GPU: before anything of this draw is in the ring)
    if (DepthCopy(s)) return;
    Assembled& a = asm_;
    // D17: assembled already on the prep thread (XBE_PREP_THREAD=1), from guest memory as it was then
    if (t_preassembled) {
        std::swap(a, *t_preassembled);
        if (!t_preassembled_ok) return;
    } else if (!Assemble(s, prim, start, count, indices, a)) {
        return;
    }
    // the 2D placement hook (a game's HUD moved sideways), for every draw as Direct3D 12 asks it: part of what the
    // repeat path compares (the constants depend on it)
    float pa = 1.0f, pb = 0.0f;
    if (g_ortho_place) {
        float x0, x1, y0, y1;
        if (OrthoExtent(s, start, count, indices, x0, x1, y0, y1) && !g_ortho_place(s, x0, x1, y0, y1, pa, pb)) {
            pa = 1.0f;
            pb = 0.0f;
        }
    }
    // room for the draw's constants and vertices: a full ring is finished and started over first
    // (and a descriptor set pool about to run out the same way)
    Room(1);

    Target* rt = EnsureTarget(s.rt, false);
    Target* ds = s.rs[kRsZEnable] || s.rs[kRsStencilEnable] ? EnsureTarget(s.zs, true) : nullptr;
    if (rt && ds && (rt->w != ds->w || rt->h != ds->h)) ds = nullptr;
    if (!rt && !ds) return;
    if (rt && CpuWrote(*rt)) UploadTarget(*rt, s.rt);
    // into the multisampled twins when every bound target has one (S14), else the plain targets
    bool ms = (!rt || rt->ms) && (!ds || ds->ms);
    draws_++;
    // the repeat path (S16)
    bool quad_now = count <= kQuadVertices && !s.rs[kRsZEnable];
    // why a draw didn't go the repeat path (XBE_PERF): the first reason that stops it, as Direct3D 12 counts them
    if (g_vk_perf) {
        if (!g_draw_same_state) no_rep_[0]++;
        else if (!rep_.ok) no_rep_[1]++;
        else if (!rendering_ || rt != rep_.rt || ds != rep_.ds || ms != rep_.ms || render_rt_ != rt) no_rep_[2]++;
        else if (pa != rep_.place_a || pb != rep_.place_b || !SameLayout(a, quad_now)) no_rep_[3]++;
    }
    if (g_draw_same_state && rep_.ok && !cap_ && rendering_ && rt == rep_.rt && ds == rep_.ds && ms == rep_.ms &&
        render_rt_ == rt && render_ds_ == ds && render_ms_ == ms && pa == rep_.place_a && pb == rep_.place_b &&
        SameLayout(a, quad_now)) {
        if (!DrawGeometry(Cmd(), a)) rep_.ok = false;
        repeats_++;
        return;
    }
    rep_.ok = false;

    // vertex shader
    ShaderChoice sh = ClassifyShaders(s, a, count);
    if (sh.prog) EnsureProgramHash(s);
    sh.vkey = VertexKey(s, a, sh);
    // point draws (D12): one pixel each, as Direct3D 12 draws them; Vulkan wants the size written by the vertex
    // shader, so a point draw's shader is the same with PointSize = 1 added (its own key)
    bool points = a.topology == Topology::Points;
    uint64_t vkey = points ? sh.vkey ^ 0x9017C0DE5EED0001ull : sh.vkey;
    VkShaderModule vs = VK_NULL_HANDLE;
    {
        auto it = modules_.find(vkey);
        if (it != modules_.end()) {
            vs = it->second;
        } else {
            std::string src = VertexHlsl(s, a, sh);
            if (sh.quad) src = QuadHlsl(std::move(src));
            if (points) src = PointSizeHlsl(std::move(src));
            vs = ShaderModule(vkey, src, spirv::Stage::Vertex);
        }
    }

    // stage textures (uploads and copies end the rendering), then the pixel shader made for them
    StageBind sb[4];
    TexInfo info[4];
    for (int i = 0; i < 4; i++) BindTexture(s, i, rt, ds, sb[i], info[i]);
    // a texture pack's picture can fill much of the ring: the next ring now (D11), while nothing of this draw's own
    // (constants, vertices) sits in it, so they fit
    Room(1);
    // HUD sharpening, as Direct3D 12 (and ROTK Fix): with [graphics] sharpening on, the pictures of 2D draws are
    // sharpened at their own texel size, more the larger they are drawn (40% per 480 lines past the first 480, up to 90%)
    float hud = 0;
    if (g_screen_space_test && app::Get().sharpening && !g_sharpen_off && !g_movie)
        hud = std::clamp(0.4f * (scale_ * float(guest_h_) / 480.0f - 1.0f), 0.0f, 0.9f);
    bool smooth = g_screen_space_test && app::Get().smooth_2d && !g_movie && scale_ * float(guest_h_) > 1.2f * 480.0f;
    bool screen_space = (hud > 0 || smooth) && g_screen_space_test(s);
    // smooth_2d, as D3D12: a 2D picture the game reads unfiltered (point), as menus and HUD pictures often are, turns
    // to blocks once the frame is drawn larger than the console's; it is upscaled bicubic instead
    if (smooth && screen_space) {
        for (int i = 0; i < 4; i++) {
            TexInfo& t = info[i];
            // linear (rect) pictures too: the menus' full-screen backgrounds are 640 x 480 ones
            if (!sb[i].view || sb[i].target || t.kind != kTex2D || t.shadow || (t.flags & kTexWide)) continue;
            if (s.tss[size_t(i)][kTssMagFilter] >= 2) continue;  // filtered already
            LOG_FIRST(1, "vulkan: smooth 2D pictures on (%.0f lines)", scale_ * float(guest_h_));
            t.flags |= kTexSmooth;
            // an alpha-only picture (A8, swizzled or linear) is a font: its letters keep a crisp edge
            TextureDesc td;
            if (Describe(s.textures[size_t(i)], td) && (td.format == 0x19 || td.format == 0x1F)) t.flags |= kTexCrisp;
        }
    }
    if (hud > 0 && screen_space) {
        LOG_FIRST(1, "vulkan: sharper HUD on (%.0f%% at %.0f lines)", hud * 100, scale_ * float(guest_h_));
        for (int i = 0; i < 4; i++) {
            TexInfo& t = info[i];
            if (!sb[i].view || sb[i].target || t.kind != kTex2D || t.rect || t.shadow || (t.flags & (kTexWide | kTexSmooth)))
                continue;
            t.flags |= kTexSharp;
            sb[i].sharp = hud;
        }
    }
    PixelKey pk = MakePixelKey(s, info);
    pk.quad = sh.quad;
    uint64_t pkey = Hash(&pk, sizeof(pk)) ^ kPixelKeySalt;
    VkShaderModule ps = VK_NULL_HANDLE;
    {
        auto it = modules_.find(pkey);
        ps = it != modules_.end() ? it->second : ShaderModule(pkey, PixelHlsl(pk), spirv::Stage::Pixel);
    }
    if (!vs || !ps) return;

    // pipeline
    PipeKey k{};
    {
        auto vh = module_hash_.find(vs), ph = module_hash_.find(ps);
        if (vh == module_hash_.end() || ph == module_hash_.end()) return;
        k.vs = vh->second;
        k.ps = ph->second;
    }
    k.regs = a.regs;
    for (int r = 0; r < 16; r++)
        if ((a.regs >> r) & 1) {
            const VertexElement& e = a.elements[size_t(r)];
            k.elements[r] = e.format | uint32_t(e.buffer) << 8 | uint32_t(e.offset) << 16;
            if (VkVertexFormat(e.format) == VK_FORMAT_UNDEFINED) {
                LOG_FIRST(4, "vulkan: vertex format %u has no Vulkan format yet", e.format);
                return;
            }
        }
    k.buffers = a.buffer_count;
    for (uint32_t b = 0; b < a.buffer_count; b++) k.strides[b] = a.buffers[b].stride;
    k.topo = uint32_t(a.topology);
    k.strip = a.strip;
    k.has_rt = rt != nullptr;
    k.has_ds = ds != nullptr;
    k.blend = SimpleState(s, kMBlendEnable) != 0;
    if (k.blend) {
        k.src = SimpleState(s, kMSrcBlend);
        k.dst = SimpleState(s, kMDestBlend);
        k.op = SimpleState(s, kMBlendOp);
    }
    uint32_t cw = SimpleState(s, kMColorWriteEnable);
    k.wmask = ((cw >> 16) & 1) | ((cw >> 8) & 1) << 1 | (cw & 1) << 2 | ((cw >> 24) & 1) << 3;
    if (ds) {
        k.zen = s.rs[kRsZEnable] != 0;
        if (k.zen) {
            k.zwrite = SimpleState(s, kMZWriteEnable) != 0;
            k.zfunc = SimpleState(s, kMZFunc);
        }
        k.sten = s.rs[kRsStencilEnable] != 0;
        if (k.sten) {
            k.sfunc = SimpleState(s, kMStencilFunc);
            k.smask = SimpleState(s, kMStencilMask) & 0xFF;
            k.swmask = SimpleState(s, kMStencilWriteMask) & 0xFF;
            k.sfail = s.rs[kRsStencilFail];
            k.szfail = SimpleState(s, kMStencilZFail);
            k.spass = SimpleState(s, kMStencilPass);
        }
        if (a.topology == Topology::Triangles && SimpleState(s, kMSolidOffsetEnable)) {
            k.zbias = int32_t(std::lround(Bits(SimpleState(s, kMDepthOffset)) * kDepth24Max / s.zscale));
            // the slope is per console pixel, and an upscaled target's pixels are smaller
            k.zslope = Bits(SimpleState(s, kMSlopeScale)) * float(ds->w) / float(ds->gw);
        }
    }
    uint32_t cull = s.rs[kRsCullMode];
    k.cull = cull != 0 && a.topology == Topology::Triangles;
    k.ccw = cull == kCullCw;
    k.fill = s.rs[kRsFillMode];
    k.samples = ms ? samples_ : 1;
    // smooth cut-out edges: an alpha test passing above the reference, unblended, into anti-aliased targets
    if (g_alpha_edges && k.samples > 1 && !k.blend && SimpleState(s, kMAlphaTestEnable)) {
        VkCompareOp f = VkCmp(SimpleState(s, kMAlphaFunc));
        k.a2c = f == VK_COMPARE_OP_GREATER || f == VK_COMPARE_OP_GREATER_OR_EQUAL;
    }
    VkPipeline pipe = Pipeline(k, vs, ps);
    if (!pipe) return;

    // constants: b0 the vertex registers, b1 DrawConstants
    uint32_t gw, gh;
    SurfaceSize(rt ? s.rt : s.zs, gw, gh);
    VkDeviceSize vc_off, dc_off;
    if (!UploadConstants(s.vsc.data(), sizeof(s.vsc), last_vc_, vc_off)) {
        LOG_FIRST(2, "vulkan: a draw's constants did not fit the upload ring");
        return;
    }
    DrawConstants c{};
    c.surf[0] = float(gw);
    c.surf[1] = float(gh);
    c.surf[2] = 1.0f / s.zscale;
    c.surf[3] = k.a2c ? 1.0f : 0.0f;
    Color(s.rs[kRsFogColor], c.fog_color);
    c.misc[0] = float(SimpleState(s, kMAlphaRef) & 0xFF);
    c.misc[1] = SimpleState(s, kMAlphaTestEnable) ? float(VkCmp(SimpleState(s, kMAlphaFunc))) : 7.0f;
    c.misc[2] = float(VkCmp(s.rs[kRsShadowFunc]));
    c.misc[3] = s.rs[kRsSpecularEnable] ? 1.0f : 0.0f;
    FillFogConstants(s, sh.prog, c);
    FillCombinerConstants(s, c);
    FillSpriteConstants(s, c);
    c.widen[0] = sh.prog ? g_program_widen.load() : 0.0f;
    if (g_orbit_on.load()) {
        c.orbit[0][0] = 1.0f;
        c.orbit[0][1] = g_orbit_yaw.load();
        c.orbit[0][2] = g_orbit_pitch.load();
        c.orbit[0][3] = g_orbit_pivot.load();
        c.orbit[1][0] = g_orbit_p11.load();
        c.orbit[1][1] = g_orbit_p22.load();
        c.orbit[1][2] = g_orbit_a.load();
        c.orbit[1][3] = g_orbit_b.load();
    } else {
        c.orbit[0][0] = 0.0f;
    }
    for (int i = 0; i < 4; i++) {
        if (s.textures[size_t(i)]) {
            c.texsize[i][0] = float(sb[i].width);
            c.texsize[i][1] = float(sb[i].height);
            // smooth_2d's and HUD sharpening's taps step one texel of the image bound, a replacement's own size
            if ((info[i].flags & kTexSmooth) || sb[i].sharp > 0) {
                c.spread[i][0] = 1.0f / float(std::max(sb[i].host_w, 1u));
                c.spread[i][1] = 1.0f / float(std::max(sb[i].host_h, 1u));
                c.spread[i][2] = sb[i].sharp;
            }
            // the console samples pixels at integer positions: targets are read half a texel back, as on D3D12
            if (sb[i].target && !(info[i].flags & kTexExact)) {
                c.texsize[i][2] = -0.5f / float(sb[i].width);
                c.texsize[i][3] = -0.5f / float(sb[i].height);
                // a 3 console texel box per wide tap keeps the console chain's glow width and brightness
                if (sb[i].wide > 0) {
                    constexpr float kGlowBoxTexels = 3.0f;
                    float n = std::ceil(kGlowBoxTexels * sb[i].wide * 0.5f);
                    c.spread[i][0] = kGlowBoxTexels / (n * float(sb[i].width));
                    c.spread[i][1] = kGlowBoxTexels / (n * float(sb[i].height));
                    c.spread[i][2] = n;
                }
            }
        }
        const auto& t = s.tss[size_t(i)];
        c.bump[i][0] = Bits(t[kTssBumpEnvMat00]);
        c.bump[i][1] = Bits(t[kTssBumpEnvMat01]);
        c.bump[i][2] = Bits(t[kTssBumpEnvMat10]);
        c.bump[i][3] = Bits(t[kTssBumpEnvMat11]);
        c.bumpl[i][0] = Bits(t[kTssBumpEnvLScale]);
        c.bumpl[i][1] = Bits(t[kTssBumpEnvLOffset]);
    }
    if (sh.fixed_transform) {
        FillTransformConstants(s, {gw, gh}, c, pa, pb);
        FillLightConstants(s, sh.fk, c);
    }
    if (!UploadConstants(&c, sizeof(c), last_dc_, dc_off)) {
        LOG_FIRST(2, "vulkan: a draw's constants did not fit the upload ring");
        return;
    }
    if (cap_) {
        CaptureDraw(s, prim, count, k, c, ds, sb, info);
        if (count <= kQuadVertices) {  // small draws: their inputs and constants
            fprintf(cap_, "\n     verts");
            uint32_t n = 0;
            for (int r = 0; r < 16; r++) n += (a.regs >> r) & 1;
            for (size_t v = 0; v < std::min<size_t>(4, n ? a.vertices.size() / (n * 4) : 0); v++) {
                fprintf(cap_, " [");
                for (uint32_t q = 0; q < n * 4; q++) fprintf(cap_, " %g", a.vertices[v * n * 4 + q]);
                fprintf(cap_, " ]");
            }
            for (int r = 0; r < 192; r++) {  // s.vsc: D3D register + 96
                const auto& v = s.vsc[r];
                if (v[0] || v[1] || v[2] || v[3]) fprintf(cap_, "\n     c%d %g %g %g %g", r - 96, v[0], v[1], v[2], v[3]);
            }
            fprintf(cap_, "\n     pc");
            for (size_t r = 0; r < std::size(c.pc); r++)
                fprintf(cap_, " (%.3f %.3f %.3f %.3f)", c.pc[r][0], c.pc[r][1], c.pc[r][2], c.pc[r][3]);
        }
        fputc('\n', cap_);
        std::string vsrc = VertexHlsl(s, a, sh);
        CaptureShader("vs", sh.vkey, sh.quad ? QuadHlsl(std::move(vsrc)) : vsrc);
        CaptureShader("ps", pkey, PixelHlsl(pk));
    }


    // the samplers' draw-wide inputs, as Direct3D 12: console_textures' mip bias for a 3D draw's textures (one level
    // per doubling of the console's 480 lines), point-enlarged textures smoothed above 1.2 x 480 lines in a 3D draw
    bool clamp = UnitTexcoords(a);
    float lines = scale_ * float(guest_h_);
    bool screen = g_screen_space_test && g_screen_space_test(s);
    float extra = app::Get().console_textures && lines > 480.0f && !screen ? std::log2(lines / 480.0f) : 0.0f;
    bool enlarge_smooth = lines > 1.2f * 480.0f && !screen;

    // descriptors (S16: a set shared by the frame's draws with the same textures and samplers)
    VkDescriptorImageInfo imgs[4], smps[4];
    for (int i = 0; i < 4; i++) {
        VkImageView view = sb[i].view;
        VkImageLayout layout = sb[i].layout;
        if (!view) {
            const Image& dm = info[i].shadow ? dummy_[3] : info[i].kind == kTexCube ? dummy_[1]
                                                         : info[i].kind == kTex3D   ? dummy_[2]
                                                                                    : dummy_[0];
            view = dm.view;
            layout = dm.layout;
        }
        imgs[i] = {VK_NULL_HANDLE, view, layout};
        smps[i] = {StageSampler(s, i, info[i], sb[i], clamp, extra, enlarge_smooth), VK_NULL_HANDLE,
                   VK_IMAGE_LAYOUT_UNDEFINED};
    }
    VkDescriptorSet set = CachedSet(sizeof(s.vsc), sizeof(DrawConstants), imgs, smps);
    if (!set) {
        LOG_FIRST(2, "vulkan: the frame ran out of descriptor sets (more than %u draws)", kSetsPerFrame);
        return;
    }
    const uint32_t dyn[2] = {uint32_t(vc_off), uint32_t(dc_off)};

    // record: the targets, then the draw
    if (!BeginRendering(rt, ds, ms)) return;
    VkCommandBuffer cb = Cmd();
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_layout_, 0, 1, &set, 2, dyn);
    // the viewport covers the target (the guest viewport is in the shader), negative height to stand the picture up
    Target* size = rt ? rt : ds;
    VkViewport vp{0, float(size->h), float(size->w), -float(size->h), 0.0f, 1.0f};
    vkCmdSetViewport(cb, 0, 1, &vp);
    // the scissor as Direct3D 12's SetViewportScissor: the guest viewport (and scissor) scaled, then truncated
    float sx = float(size->w) / float(std::max(size->gw, 1u)), sy = float(size->h) / float(std::max(size->gh, 1u));
    int32_t x0 = int32_t(s.viewport.x * sx), y0 = int32_t(s.viewport.y * sy);
    int32_t x1 = int32_t((s.viewport.x + s.viewport.w) * sx), y1 = int32_t((s.viewport.y + s.viewport.h) * sy);
    if (s.scissor_count) {
        x0 = std::max(x0, int32_t(float(s.scissors[0]) * sx));
        y0 = std::max(y0, int32_t(float(s.scissors[1]) * sy));
        x1 = std::min(x1, int32_t(float(s.scissors[2]) * sx));
        y1 = std::min(y1, int32_t(float(s.scissors[3]) * sy));
    }
    x0 = std::clamp<int32_t>(x0, 0, int32_t(size->w));
    y0 = std::clamp<int32_t>(y0, 0, int32_t(size->h));
    x1 = std::clamp<int32_t>(x1, 0, int32_t(size->w));
    y1 = std::clamp<int32_t>(y1, 0, int32_t(size->h));
    if (x1 <= x0 || y1 <= y0) return;
    VkRect2D sc{{x0, y0}, {uint32_t(x1 - x0), uint32_t(y1 - y0)}};
    vkCmdSetScissor(cb, 0, 1, &sc);
    float bf[4];
    Color(SimpleState(s, kMBlendColor), bf);
    vkCmdSetBlendConstants(cb, bf);
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, SimpleState(s, kMStencilRef) & 0xFF);
    if (!DrawGeometry(cb, a)) return;
    // what the next draw in the same state can draw with (the pipeline already holds the vertex layout; the
    // constants depend on the vertices only through the 2D placement, compared as Direct3D 12 does)
    {
        rep_.ok = true;
        rep_.place_a = pa;
        rep_.place_b = pb;
        rep_.rt = rt;
        rep_.ds = ds;
        rep_.ms = ms;
        rep_.regs = a.regs;
        rep_.decode = a.decode;
        rep_.pretransformed = a.pretransformed;
        rep_.strip = a.strip;
        rep_.quad = quad_now;
        rep_.topology = a.topology;
        rep_.buffer_count = a.buffer_count;
        rep_.elements = a.elements;
        for (uint32_t b = 0; b < a.buffer_count; b++) rep_.strides[b] = a.buffers[b].stride;
    }
    LOG_FIRST(1, "vulkan: the first draw was recorded");
}

bool VkBackend::DrawGeometry(VkCommandBuffer cb, const Assembled& a) {
    Frame& f = frames_[frame_];
    VkDeviceSize vb_off[16];
    for (uint32_t b = 0; b < a.buffer_count; b++) {
        uint8_t* cpu;
        if (!Upload(a.buffers[b].bytes, 16, false, vb_off[b], cpu)) {
            LOG_FIRST(2, "vulkan: a draw's vertices did not fit the upload ring");
            return false;
        }
        std::memcpy(cpu, a.buffers[b].src, a.buffers[b].bytes);
    }
    VkDeviceSize ib_off = 0;
    if (a.index_count) {
        uint32_t ibytes = a.index_count * (a.index16 ? 2 : 4);
        uint8_t* cpu;
        if (!Upload(ibytes, 4, false, ib_off, cpu)) {
            LOG_FIRST(2, "vulkan: a draw's indices did not fit the upload ring");
            return false;
        }
        std::memcpy(cpu, a.index_src, ibytes);
    }
    VkBuffer bufs16[16];
    for (uint32_t b = 0; b < a.buffer_count; b++) bufs16[b] = f.upload;
    if (a.buffer_count) vkCmdBindVertexBuffers(cb, 0, a.buffer_count, bufs16, vb_off);
    if (a.index_count) {
        vkCmdBindIndexBuffer(cb, f.upload, ib_off, a.index16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, a.index_count, 1, 0, a.base_vertex, 0);
    } else if (a.topology == Topology::Sprites) {
        vkCmdDraw(cb, 6, a.draw_count, 0, 0);  // a square per point (SpriteHlsl)
    } else {
        vkCmdDraw(cb, a.draw_count, 1, 0, 0);
    }
    return true;
}

// ---- render scale

void VkBackend::PickScale() {
    scale_ = app::RenderScale(guest_h_);
    xmul_ = 1.0f;
    if (g_wide_targets && guest_h_ && std::fabs(float(guest_w_) / float(guest_h_) - 4.0f / 3.0f) <= 0.01f)
        xmul_ = std::max(1.0f, app::FrameAspect() / (4.0f / 3.0f));
}

// the menu changed the render scale (or the window's height did): every target is made again at the new size
bool VkBackend::ScaleChanged() {
    float s = scale_, x = xmul_;
    PickScale();
    uint32_t samples = PickSamples();  // anti-aliasing changed in the menu: the twins made again too (S14)
    if (s == scale_ && x == xmul_ && samples == samples_) return false;
    Submit(true);
    vkDeviceWaitIdle(device_);
    for (auto& [key, t] : targets_) DestroyTarget(t);
    targets_.clear();
    if (samples != samples_) {
        // the passes made for the old count go with it (the draws' pipelines carry the count in their key)
        ms_ok_.clear();
        for (VkPipeline* mp : {&ms_up_[0], &ms_up_[1]}) {
            if (*mp) vkDestroyPipeline(device_, *mp, nullptr);
            *mp = VK_NULL_HANDLE;
        }
        for (uint32_t i = 16; i < 32; i++) {
            if (clear_pipes_[i]) vkDestroyPipeline(device_, clear_pipes_[i], nullptr);
            clear_pipes_[i] = VK_NULL_HANDLE;
        }
    }
    samples_ = samples;
    LOGR("vulkan: render scale %.3f (%.2f across), anti-aliasing %ux: targets made again", scale_, scale_ * xmul_,
         samples_);
    return true;
}

bool VkBackend::EnsureScratch(uint32_t w, uint32_t h) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    if (scratch_.image && scratch_w_ == w && scratch_h_ == h) return true;
    Retire(scratch_);
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = kColorFormat;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (!MakeImage(scratch_, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) return false;
    scratch_w_ = w;
    scratch_h_ = h;
    return true;
}

VkRect2D VkBackend::ScaledRect(const Target& t, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    float sx = float(t.w) / float(std::max(t.gw, 1u)), sy = float(t.h) / float(std::max(t.gh, 1u));
    // truncated, as Direct3D 12's ScaledRect: rounding gave one-pixel edge differences at fractional scales (D7)
    auto cx = [&](int32_t v) { return std::clamp<int32_t>(int32_t(float(v) * sx), 0, int32_t(t.w)); };
    auto cy = [&](int32_t v) { return std::clamp<int32_t>(int32_t(float(v) * sy), 0, int32_t(t.h)); };
    int32_t a = cx(x0), b = cy(y0), c = cx(x1), d = cy(y1);
    if (c <= a || d <= b) return {{0, 0}, {0, 0}};
    return {{a, b}, {uint32_t(c - a), uint32_t(d - b)}};
}

// ---- copies and read-backs

// a lock alone proves nothing (reads lock too): only changed bytes mean the CPU wrote the target's memory
bool VkBackend::CpuWrote(Target& t) {
    if (t.depth) return false;
    uint64_t e = DirtyEpoch(t.data);
    if (e <= t.epoch) return false;
    t.epoch = e;
    uint64_t sig = MemSig(t.data, t.bytes);
    if (sig == t.sig && !ChangedSinceRead(t)) return false;
    t.sig = sig;
    return true;
}

// MemSig samples a big surface and misses small writes (XFONT text): after a read-back every byte is compared (as
// Direct3D 12's Changed)
bool VkBackend::ChangedSinceRead(const Target& t) {
    if (t.drawn || t.read.empty()) return false;
    uint32_t row = t.gw * 4, pitch = GuestPitch(t.gw);
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(t.data));
    for (uint32_t y = 0; y < t.gh; y++)
        if (std::memcmp(p + size_t(y) * pitch, &t.read[size_t(y) * row], row)) return true;
    return false;
}

// the pixels the CPU changed since the last read-back, opaque, the rest 0 (transparent); t.read takes them
std::vector<uint8_t> VkBackend::LatchCpuChanges(Target& t) {
    std::vector<uint8_t> px(t.read.size(), 0);
    uint32_t pitch = GuestPitch(t.gw);
    const uint8_t* guest = static_cast<const uint8_t*>(kernel::HostPtr(t.data));
    for (uint32_t y = 0; y < t.gh; y++) {
        for (uint32_t x = 0; x < t.gw; x++) {
            const uint8_t* c = guest + size_t(y) * pitch + x * 4;
            size_t i = (size_t(y) * t.gw + x) * 4;
            if (!std::memcmp(c, &t.read[i], 4)) continue;
            std::memcpy(&t.read[i], c, 4);
            std::memcpy(&px[i], c, 3);
            px[i + 3] = 255;
        }
    }
    return px;
}

static void BlitConstants(float c[32], uint32_t src_w, uint32_t src_h, bool gamma, bool look = false);

// what the CPU wrote into a colour target: decoded from the surface's format (DecodeLevel), into a picture of the
// console's size, then drawn over the target at its render scale, alpha kept (as Direct3D 12's UploadTarget). After
// a read-back only the pixels the CPU changed are drawn, over the picture, which stays at render scale (text the CPU
// writes onto a frame it read stays sharp)
void VkBackend::UploadTarget(Target& t, const Surface& s) {
    TextureDesc d;
    if (!Describe(s.header, d) || Format(d.format).depth || lost_) return;
    std::vector<uint8_t> px;
    uint32_t pitch = 0;
    bool over = !t.drawn && d.width == t.gw && d.height == t.gh && t.read.size() == size_t(t.gw) * t.gh * 4;
    if (over) {
        LOG_FIRST(4, "vulkan: CPU pixels drawn over target %08X", t.data);
        pitch = t.gw * 4;
        px = LatchCpuChanges(t);
    } else {
        DecodeLevel(d, 0, 0, static_cast<const uint8_t*>(kernel::HostPtr(d.data)), nullptr, px, pitch);
    }
    // on any failure below, the next check uploads again in full: the CPU's change is not lost (review of S6)
    auto retry = [&] {
        t.epoch = 0;
        t.sig = 0;
        t.read.clear();
    };
    if (px.size() < size_t(pitch) * d.height || pitch < d.width * 4) return retry();
    // the picture: BGRA, the console's size
    Image pic;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_B8G8R8A8_UNORM;
    ci.extent = {d.width, d.height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!MakeImage(pic, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) return retry();
    uint32_t row = d.width * 4;
    VkDeviceSize off;
    uint8_t* cpu;
    if (!Upload(VkDeviceSize(row) * d.height, 16, true, off, cpu)) {
        DestroyImage(pic);
        return retry();
    }
    for (uint32_t y = 0; y < d.height; y++) std::memcpy(cpu + size_t(y) * row, &px[size_t(y) * pitch], row);
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Barrier(pic.image, range, pic.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy c{};
    c.bufferOffset = off;
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {d.width, d.height, 1};
    vkCmdCopyBufferToImage(Cmd(), frames_[frame_].upload, pic.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    Barrier(pic.image, range, pic.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // drawn over the target (a rendering of its own; the draw that follows begins its own)
    EndRendering();
    OrderWrites();
    ToLayout(t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkCommandBuffer cb = Cmd();
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    ca.imageView = t.view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {t.w, t.h}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRenderingKHR(cb, &ri);
    VkViewport vp{0, float(t.h), float(t.w), -float(t.h), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {t.w, t.h}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    float k[32];
    BlitConstants(k, d.width, d.height, false);
    k[5] = 1.0f;  // opts.y: alpha kept
    k[8] = 0.0f;  // no sharpening
    bool drawn = BlitPass(pic.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, k, kColorFormat, over);
    vkCmdEndRenderingKHR(cb);
    writes_open_ = true;
    Retire(pic);
    if (!drawn) return retry();
    t.epoch = DirtyEpoch(t.data);
    t.sig = MemSig(t.data, t.bytes);
    LOG_FIRST(4, "vulkan: target %08X takes what the CPU wrote", t.data);
}

// CopyRects: a target the GPU drew is copied on the GPU, rect by rect; pixels the CPU made are copied in memory
void VkBackend::Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) {
    Target* st = FindTarget(src, false);
    if (cap_) {
        const char* from = "memory";
        if (st) from = st->drawn ? "target" : "idle target";
        fprintf(cap_, "%04u copy %08X %s to %08X\n", cap_i_++, src.data, from, dst.data);
    }
    if (!st || !st->drawn) {
        if (!CopySurfaceMemory(src, dst, count, src_rects, dst_points))
            LOG_FIRST(8, "vulkan: CopyRects %08X to %08X skipped", src.data, dst.data);
        else if (Target* dt = FindTarget(dst, false))
            UploadTarget(*dt, dst);
        return;
    }
    Target* dt = EnsureTarget(dst, false);
    if (!dt || dt == st) return;
    if (CpuWrote(*dt)) UploadTarget(*dt, dst);  // the copy may cover only part of it
    // as Direct3D 12's Copy (D8): an exact region copy when both are drawn at the same scale, else drawn across
    // (each pixel the average of the source texels it covers)
    bool same_scale = uint64_t(st->w) * dt->gw == uint64_t(dt->w) * st->gw &&
                      uint64_t(st->h) * dt->gh == uint64_t(dt->h) * st->gh;
    bool listed = count && src_rects;
    uint32_t n = listed ? count : 1;
    auto rect = [&](uint32_t i, int32_t r[4]) {  // left top right bottom
        uint32_t a = src_rects + i * 16;
        for (int k = 0; k < 4; k++) r[k] = int32_t(kernel::R32(a + uint32_t(k) * 4));
    };
    auto point = [&](uint32_t i, int32_t& x, int32_t& y) {
        x = int32_t(kernel::R32(dst_points + i * 8));
        y = int32_t(kernel::R32(dst_points + i * 8 + 4));
    };
    float ssx = float(st->w) / float(st->gw), ssy = float(st->h) / float(st->gh);
    float dsx = float(dt->w) / float(dt->gw), dsy = float(dt->h) / float(dt->gh);
    if (same_scale) {
        // a guest coordinate read as unsigned, times the scale (Direct3D 12's ScaleUnsigned), clamped to both
        auto su = [](int32_t v, float sc) { return uint32_t(float(uint32_t(v)) * sc); };
        std::vector<VkImageCopy> regions;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t l = 0, t = 0, r = std::min(st->w, dt->w), b = std::min(st->h, dt->h), dx = 0, dy = 0;
            if (listed) {
                int32_t g[4];
                rect(i, g);
                l = su(g[0], ssx);
                t = su(g[1], ssy);
                r = std::min(su(g[2], ssx), st->w);
                b = std::min(su(g[3], ssy), st->h);
                dx = l;
                dy = t;
                if (dst_points) {
                    int32_t px, py;
                    point(i, px, py);
                    dx = su(px, dsx);
                    dy = su(py, dsy);
                }
            }
            if (r <= l || b <= t || dx >= dt->w || dy >= dt->h) continue;
            r = std::min(r, l + dt->w - dx);
            b = std::min(b, t + dt->h - dy);
            VkImageCopy c{};
            c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.srcOffset = {int32_t(l), int32_t(t), 0};
            c.dstOffset = {int32_t(dx), int32_t(dy), 0};
            c.extent = {r - l, b - t, 1};
            regions.push_back(c);
        }
        if (regions.empty()) return;
        ToLayout(*st, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        ToLayout(*dt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyImage(Cmd(), st->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dt->image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t(regions.size()), regions.data());
        dt->drawn = true;
        if (cap_) {
            char name[32];
            snprintf(name, sizeof(name), "%04u_copy_%08X", cap_i_, dt->data);
            CapSave(*dt, name);
        }
        return;
    }
    // drawn across: one screen-pass draw a rect, alpha kept, taps over the source texels each pixel covers
    EndRendering();
    OrderWrites();
    ToLayout(*st, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ToLayout(*dt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkCommandBuffer cb = Cmd();
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    ca.imageView = dt->view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {dt->w, dt->h}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRenderingKHR(cb, &ri);
    uint32_t taps = uint32_t(std::ceil(std::max({ssx / dsx, ssy / dsy, 1.0f}) - 1e-3f));
    for (uint32_t i = 0; i < n; i++) {
        float x0 = 0, y0 = 0, x1 = float(std::min(st->gw, dt->gw)), y1 = float(std::min(st->gh, dt->gh));
        float dx = 0, dy = 0;
        if (listed) {
            int32_t g[4];
            rect(i, g);
            x0 = float(g[0]);
            y0 = float(g[1]);
            x1 = float(g[2]);
            y1 = float(g[3]);
            dx = x0;
            dy = y0;
            if (dst_points) {
                int32_t px, py;
                point(i, px, py);
                dx = float(px);
                dy = float(py);
            }
        }
        if (x1 <= x0 || y1 <= y0) continue;
        float vx = dx * dsx, vy = dy * dsy, vw = (x1 - x0) * dsx, vh = (y1 - y0) * dsy;
        // the scissor clipped to the target (a destination partly off it is drawn in part, as Direct3D 12)
        int32_t sl = std::max(int32_t(vx), 0), stp = std::max(int32_t(vy), 0);
        int32_t sr = std::min(int32_t(vx + vw), int32_t(dt->w)), sb = std::min(int32_t(vy + vh), int32_t(dt->h));
        if (sr <= sl || sb <= stp) continue;
        VkViewport vp{vx, vy + vh, vw, -vh, 0.0f, 1.0f};
        VkRect2D sc{{sl, stp}, {uint32_t(sr - sl), uint32_t(sb - stp)}};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
        float k[32];
        BlitConstants(k, st->w, st->h, false);
        k[0] = x0 / float(st->gw);
        k[1] = y0 / float(st->gh);
        k[2] = (x1 - x0) / float(st->gw);
        k[3] = (y1 - y0) / float(st->gh);
        k[5] = 1.0f;  // alpha kept
        k[6] = float(taps);
        k[8] = 0.0f;
        BlitPass(st->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, k, kColorFormat);
    }
    vkCmdEndRenderingKHR(cb);
    writes_open_ = true;
    dt->drawn = true;
}

// the CPU reads a surface the GPU drew: the pixels go back to guest memory (the GPU's work so far finished first)
// the host-visible buffer read-backs land in, grown as needed
void VkBackend::SetGamma(const uint8_t* ramp) {
    std::memcpy(ramp_, ramp, sizeof(ramp_));
    ramp_set_ = true;
    ramp_dirty_ = true;
}

// brightness over the game's ramp: v^(1/b), black and white kept, the darks lifted (b > 1) or deepened (as D3D12)
uint8_t VkBackend::Brighten(uint8_t v) const {
    float b = app::Get().brightness;
    if (b > 0.999f && b < 1.001f) return v;
    return uint8_t(std::lround(255.0f * std::pow(v / 255.0f, 1.0f / std::clamp(b, 0.5f, 2.0f))));
}

// at Present, before the screen pass: the lookup made again when the game's ramp or the brightness changed
void VkBackend::UpdateGammaRamp() {
    if (float b = app::Get().brightness; b != brightness_) {
        if (!ramp_set_ && brightness_ == 1.0f)
            for (int i = 0; i < 256; i++) ramp_[i] = ramp_[256 + i] = ramp_[512 + i] = uint8_t(i);
        brightness_ = b;
        ramp_dirty_ = true;
    }
    if (!ramp_dirty_) return;
    ramp_dirty_ = false;
    uint8_t lut[256 * 4];
    bool identity = true;
    for (int i = 0; i < 256; i++) {  // B8G8R8A8
        uint8_t* p = lut + i * 4;
        p[0] = Brighten(ramp_[512 + i]);
        p[1] = Brighten(ramp_[256 + i]);
        p[2] = Brighten(ramp_[i]);
        p[3] = 255;
        identity &= p[2] == i && p[1] == i && p[0] == i;
    }
    gamma_ = !identity;
    if (!gamma_) return;
    if (!lut_.image) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_B8G8R8A8_UNORM;
        ci.extent = {256, 1, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (!MakeImage(lut_, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) {
            LOG_FIRST(2, "vulkan: the gamma lookup could not be made (the game's brightness ramp not shown)");
            gamma_ = false;
            return;
        }
    }
    VkDeviceSize off;
    uint8_t* cpu;
    if (!Upload(sizeof(lut), 16, true, off, cpu)) {
        ramp_dirty_ = true;  // tried again next frame
        gamma_ = false;
        return;
    }
    std::memcpy(cpu, lut, sizeof(lut));
    ToLayout(lut_.image, false, lut_.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy c{};
    c.bufferOffset = off;
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {256, 1, 1};
    vkCmdCopyBufferToImage(Cmd(), frames_[frame_].upload, lut_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    ToLayout(lut_.image, false, lut_.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VkBackend::ReadbackToHost() {
    VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = readback_;
    b.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(Cmd(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &b, 0, nullptr);
}

bool VkBackend::EnsureReadback(VkDeviceSize bytes) {
    if (bytes > readback_size_) {
        if (readback_) vkDestroyBuffer(device_, readback_, nullptr);
        if (readback_mem_) vkFreeMemory(device_, readback_mem_, nullptr);
        readback_ = VK_NULL_HANDLE;
        readback_mem_ = VK_NULL_HANDLE;
        readback_cpu_ = nullptr;
        readback_size_ = 0;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (vkCreateBuffer(device_, &bi, nullptr, &readback_) != VK_SUCCESS) return false;
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, readback_, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        void* p = nullptr;
        // cached host memory reads fast; coherent is the fallback
        bool found = FindMemory(req.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                ai.memoryTypeIndex) ||
                     FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                ai.memoryTypeIndex);
        if (!found || vkAllocateMemory(device_, &ai, nullptr, &readback_mem_) != VK_SUCCESS) {
            vkDestroyBuffer(device_, readback_, nullptr);
            readback_ = VK_NULL_HANDLE;
            return false;
        }
        vkBindBufferMemory(device_, readback_, readback_mem_, 0);
        vkMapMemory(device_, readback_mem_, 0, VK_WHOLE_SIZE, 0, &p);
        readback_cpu_ = static_cast<uint8_t*>(p);
        readback_size_ = bytes;
    }
    return readback_cpu_ != nullptr;
}

bool VkBackend::ShrinkToScratch(Target& t) {
    if (!EnsureScratch(t.gw, t.gh)) return false;
    EndRendering();
    OrderWrites();
    ToLayout(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Barrier(scratch_.image, range, scratch_.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkCommandBuffer cb = Cmd();
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    ca.imageView = scratch_.view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {t.gw, t.gh}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRenderingKHR(cb, &ri);
    VkViewport vp{0, float(t.gh), float(t.gw), -float(t.gh), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {t.gw, t.gh}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    float k[32];
    BlitConstants(k, t.w, t.h, false);
    k[5] = 1.0f;  // alpha kept (the title reads it)
    k[8] = 0.0f;
    bool ok = BlitPass(t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, k, kColorFormat);
    vkCmdEndRenderingKHR(cb);
    writes_open_ = true;
    Barrier(scratch_.image, range, scratch_.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    return ok;
}

void VkBackend::FreeLate(Late& l) {
    for (int i = 0; i < int(kFrames); i++)
        if (l.buf[i]) vmaDestroyBuffer(vma_, l.buf[i], l.mem[i]);
    l = Late{};
}

bool VkBackend::DeliverLate(uint32_t data) {
    if (!progress_) return false;
    std::lock_guard lock(late_lock_);
    auto it = late_.find(data);
    if (it == late_.end()) return false;
    Late& l = it->second;
    uint64_t done = Done(), now = presents_seen_.load();
    int best = -1;
    for (int i = 0; i < int(kFrames); i++) {
        bool finished = l.value[i] && l.value[i] <= done;
        bool recent = now - l.frame[i] <= uint64_t(kFrames) + 1;
        bool newest = best < 0 || l.value[i] > l.value[best];
        if (finished && recent && newest && l.cpu[i]) best = i;
    }
    if (best < 0) return false;
    if (l.value[best] > l.delivered) {
        vmaInvalidateAllocation(vma_, l.mem[best], 0, VK_WHOLE_SIZE);  // (nothing when the memory is coherent)
        uint32_t pitch = GuestPitch(l.gw), row = l.gw * 4;
        uint8_t* dst = static_cast<uint8_t*>(kernel::HostPtr(data));
        for (uint32_t y = 0; y < l.gh; y++) std::memcpy(dst + size_t(y) * pitch, l.cpu[best] + size_t(y) * row, row);
        l.delivered = l.value[best];
    }
    return true;
}

void VkBackend::RecordLate(uint32_t data) {
    if (!progress_ || lost_) return;
    auto it = targets_.find(TargetKey(data, false));
    if (it == targets_.end()) return;
    auto u = target_used_.find(data);
    if (presents_ - (u == target_used_.end() ? 0 : u->second) > 8) return;  // long unused (as Sync's stale check)
    Target& t = it->second;
    // what DeliverLate or Sync left in guest memory is no CPU write to upload back
    t.sig = MemSig(t.data, t.bytes);
    t.read.clear();
    std::lock_guard lock(late_lock_);
    Late& l = late_[data];
    if (l.gw != t.gw || l.gh != t.gh) {
        // a new size: the old buffers may still be written by copies in flight, recorded (submitted with the next
        // Submit) or submitted already: those finished first (rare). (Review of S12: Submit(true) alone did not wait
        // when nothing was recording, right after a Present)
        if (l.buf[0] || l.buf[1]) {
            uint64_t last = 0;
            for (uint64_t v : l.value) last = std::max(last, v);
            if (last > serial_) Submit(true);
            WaitSerial(last);
            FreeLate(l);
        }
        l.gw = t.gw;
        l.gh = t.gh;
    }
    // a copy for a later lock, unless the GPU is a whole ring behind
    uint32_t slot = l.next;
    if (l.value[slot] > Done()) return;
    VkDeviceSize bytes = VkDeviceSize(t.gw) * t.gh * 4;
    if (!l.buf[slot]) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo ai{};
        ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        ai.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(vma_, &bi, &ai, &l.buf[slot], &l.mem[slot], &info) != VK_SUCCESS) {
            l.buf[slot] = VK_NULL_HANDLE;
            l.mem[slot] = VK_NULL_HANDLE;
            return;
        }
        l.cpu[slot] = static_cast<uint8_t*>(info.pMappedData);
    }
    EndRendering();
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {t.gw, t.gh, 1};
    if (t.w == t.gw && t.h == t.gh) {
        ToLayout(t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkCmdCopyImageToBuffer(Cmd(), t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, l.buf[slot], 1, &c);
    } else {
        if (!ShrinkToScratch(t)) return;
        vkCmdCopyImageToBuffer(Cmd(), scratch_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, l.buf[slot], 1, &c);
    }
    VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = l.buf[slot];
    b.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(Cmd(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &b, 0, nullptr);
    l.value[slot] = serial_ + 1;  // the next submit carries it
    l.frame[slot] = presents_;
    l.next = (slot + 1) % kFrames;
}

// the CPU reads a surface the GPU drew: its pixels back into guest memory, at the console's size (as Direct3D 12's
// Sync: the stale check of #81, t.read kept for the CPU's later changes)
void VkBackend::Sync(uint32_t data, uint32_t size) {
    auto it = targets_.find(TargetKey(data, false));
    if (it == targets_.end() || !it->second.drawn || lost_) return;
    EndRendering();  // its copies are recorded outside any rendering
    Target& t = it->second;
    // long unused, the memory may be the title's again for something else - unless nothing has written it since
    // (Fellowship's save pictures, read seconds after the copy was made, were black squares: #81)
    constexpr uint64_t kStaleFrames = 8;
    auto u = target_used_.find(data);
    uint64_t used = u == target_used_.end() ? 0 : u->second;
    if (presents_ - used > kStaleFrames) {
        if (size != t.bytes || DirtyEpoch(data) > t.epoch || MemSig(data, t.bytes) != t.sig) {
            LOG_FIRST(8, "vulkan: read-back of %08X skipped: unused for a while and its memory written since", data);
            return;
        }
        LOG_FIRST(8, "vulkan: read-back of %08X after a while unused (its memory untouched: still its pixels)", data);
    }
    VkDeviceSize bytes = VkDeviceSize(t.gw) * t.gh * 4;
    if (!EnsureReadback(bytes)) return;
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {t.gw, t.gh, 1};
    if (t.w == t.gw && t.h == t.gh) {
        ToLayout(t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkCmdCopyImageToBuffer(Cmd(), t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1, &c);
    } else {
        if (!ShrinkToScratch(t)) return;
        vkCmdCopyImageToBuffer(Cmd(), scratch_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1, &c);
    }
    ReadbackToHost();
    Submit(true);
    if (lost_ || !readback_cpu_) return;
    uint32_t row = t.gw * 4, pitch = GuestPitch(t.gw);
    uint8_t* g = static_cast<uint8_t*>(kernel::HostPtr(data));
    for (uint32_t y = 0; y < t.gh; y++) std::memcpy(g + size_t(y) * pitch, readback_cpu_ + size_t(y) * row, row);
    t.read.assign(readback_cpu_, readback_cpu_ + size_t(row) * t.gh);
    t.drawn = false;
    t.sig = MemSig(t.data, t.bytes);
    LOG_FIRST(4, "vulkan: read back %08X (%ux%u)", data, t.gw, t.gh);
}

// ---- the in-game menu, overlay and fps counter (ImGui), drawn into the swapchain image after the frame

bool VkBackend::InitUi() {
    if (!dynamic_rendering_) {
        LOGR("vulkan: the in-game menu needs dynamic rendering; it is not shown on this card yet");
        return true;
    }
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = 16;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    VkResult r = vkCreateDescriptorPool(device_, &pi, nullptr, &ui_pool_);
    if (r != VK_SUCCESS) return Fail("vkCreateDescriptorPool (menu)", r);
    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_2, [](const char* name, void* inst) { return Lookup(name, inst); }, instance_))
        return Fail("the menu's Vulkan functions could not be loaded");
    return MakeUiBackend();
}

// the menu's Vulkan side for the swapchain's format (made again when HDR changes it, S11)
bool VkBackend::MakeUiBackend() {
    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = instance_;
    ii.PhysicalDevice = gpu_;
    ii.Device = device_;
    ii.QueueFamily = family_;
    ii.Queue = queue_;
    ii.DescriptorPool = ui_pool_;
    ii.MinImageCount = 2;
    ii.ImageCount = std::max<uint32_t>(uint32_t(images_.size()), 2);
    ii.ApiVersion = VK_API_VERSION_1_2;   // (Dear ImGui 1.93: the instance's version, and the pipeline in PipelineInfoMain)
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.PipelineCache = VK_NULL_HANDLE;
    ii.UseDynamicRendering = true;
    ii.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    ii.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    ii.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swap_fmt_;
    if (!ImGui_ImplVulkan_Init(&ii)) return Fail("the menu's Vulkan backend did not start");
    ui_ = true;
    ui_fmt_ = swap_fmt_;
    return true;
}

void VkBackend::RenderUi(VkCommandBuffer cb, uint32_t index) {
    if (!ui_) return;
    std::lock_guard lock(app::UiLock());
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (app::kGame.ui) app::kGame.ui(fps_);
    app::DrawOverlay(fps_);
    // [display] show_fps: the frames presented a second, top right
    if (app::Get().show_fps && fps_ > 0) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        float pad = std::max(8.0f, vp->Size.y / 120.0f);
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - pad, vp->Pos.y + pad), ImGuiCond_Always, ImVec2(1, 0));
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##fps", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::SetWindowFontScale(app::UiScale());
        ImGui::Text("%.0f fps", fps_);
        ImGui::End();
    }
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || dd->CmdListsCount == 0) return;
    // the swapchain image is a colour attachment after the screen pass (BlitToScreen): its writes finished before the
    // menu loads them (two renderings are not ordered by themselves; review of S1)
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                         1, &mb, 0, nullptr, 0, nullptr);
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    ca.imageView = image_views_[index];
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, extent_};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRenderingKHR(cb, &ri);
    ImGui_ImplVulkan_RenderDrawData(dd, cb);
    vkCmdEndRenderingKHR(cb);
}

// presents a second, counted over half a second
void VkBackend::CountFps() {
    uint64_t now = ::GetTickCount64();
    if (!fps_start_) fps_start_ = now;
    fps_frames_++;
    if (now - fps_start_ >= 500) {
        fps_ = double(fps_frames_) * 1000.0 / double(now - fps_start_);
        fps_frames_ = 0;
        fps_start_ = now;
    }
}

// the screen's pass (S1, notes/vulkan-parity-plan.md): kBlitHlsl, the shader Direct3D 12's Blit uses, with its
// constants (uvrect, opts: gamma lookup / alpha / taps / scale, sharp: CAS amount and source texel, hdr, the colour
// look); the samplers as Direct3D 12's EnsureBlitSamplers (s0 linear, s1..s3 point, clamped)
VkPipeline VkBackend::BlitPipeline(VkFormat fmt, bool over) {
    uint32_t key = uint32_t(fmt) | (over ? 0x80000000u : 0u);
    auto it = blit_pipes_.find(key);
    if (it != blit_pipes_.end()) return it->second;
    VkShaderModule vs = ShaderModule(0xB117000Bull, kBlitHlsl, spirv::Stage::Vertex, "vs");
    VkShaderModule ps = ShaderModule(0xB117000Cull, kBlitHlsl, spirv::Stage::Pixel, "psmain");
    if (!vs || !ps) return VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "vs", nullptr};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "psmain", nullptr};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    if (over) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.colorBlendOp = cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = 1;
    bs.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfoKHR ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &fmt;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.pNext = &ri;
    pi.stageCount = 2;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pColorBlendState = &bs;
    pi.pDynamicState = &dy;
    pi.layout = pipe_layout_;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, pipe_cache_, 1, &pi, nullptr, &p) != VK_SUCCESS) {
        LOG_FIRST(2, "vulkan: the screen pass's pipeline could not be made");
        p = VK_NULL_HANDLE;
    }
    blit_pipes_[key] = p;
    return p;
}

bool VkBackend::EnsureBlitSamplers() {
    if (!blit_smp_[0]) {
        for (int i = 0; i < 2; i++) {
            VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            si.magFilter = si.minFilter = i == 0 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            si.mipmapMode = i == 0 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
            si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            si.maxLod = VK_LOD_CLAMP_NONE;
            if (vkCreateSampler(device_, &si, nullptr, &blit_smp_[i]) != VK_SUCCESS) return false;
        }
    }
    return true;
}

bool VkBackend::BlitPass(VkImageView src, VkImageLayout src_layout, const float c[32], VkFormat dst_fmt, bool over,
                         bool point) {
    VkPipeline pipe = BlitPipeline(dst_fmt, over);
    if (!pipe || !EnsureBlitSamplers()) return false;
    VkDeviceSize off;
    uint8_t* cpu;
    if (!Upload(128, ubo_align_, false, off, cpu)) return false;
    std::memcpy(cpu, c, 128);
    VkDescriptorImageInfo imgs[4], smps[4];
    for (int i = 0; i < 4; i++) {
        // t0 the picture; t1 the gamma lookup (a stand-in until the game sets a ramp: never read with opts.x 0)
        if (i == 0) imgs[i] = {VK_NULL_HANDLE, src, src_layout};
        else if (i == 1 && lut_.view && lut_.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) imgs[i] = {VK_NULL_HANDLE, lut_.view, lut_.layout};
        else imgs[i] = {VK_NULL_HANDLE, dummy_[0].view, dummy_[0].layout};
        smps[i] = {blit_smp_[i == 0 && !point ? 0 : 1], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    VkDescriptorSet set = CachedSet(128, 128, imgs, smps);
    if (!set) return false;
    const uint32_t dyn[2] = {uint32_t(off), uint32_t(off)};
    VkCommandBuffer cb = Cmd();
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_layout_, 0, 1, &set, 2, dyn);
    vkCmdDraw(cb, 3, 1, 0, 0);
    return true;
}

// the blit constants as Direct3D 12's Blit fills them, for a plain picture (S10 adds the gamma lookup and the looks)
static void BlitConstants(float c[32], uint32_t src_w, uint32_t src_h, bool gamma, bool look) {
    std::fill(c, c + 32, 0.0f);
    c[4] = gamma ? 1.0f : 0.0f;         // opts.x: the gamma lookup at t1
    c[2] = c[3] = 1.0f;                 // uvrect: all of the source
    c[6] = 1.0f;                        // opts.z: one tap
    c[7] = 1.0f;                        // opts.w: colour scale
    c[8] = g_sharpen_off ? 0.0f : std::min(app::Get().sharpening, 100u) / 100.0f;  // sharp.x
    c[9] = 1.0f / float(std::max(src_w, 1u));
    c[10] = 1.0f / float(std::max(src_h, 1u));
    // look: [graphics] colour_look at look_strength (S10), the rows as Direct3D 12's Blit fills them
    uint32_t li = std::min<uint32_t>(g_colour_look, std::size(kLooks) - 1);
    const ColourLook& L = kLooks[li];
    float strength = look && li ? std::min(app::Get().look_strength, 100u) / 100.0f : 0.0f;
    const float rows[16] = {L.contrast, L.pivot, L.sat, L.vibrance, L.gamma, strength, 0, 0,
                            L.shadow[0], L.shadow[1], L.shadow[2], 0, L.light[0], L.light[1], L.light[2], 0};
    std::copy(rows, rows + 16, c + 16);
}

// the front buffer's target into the window through the screen pass, the frame's shape kept with black at the sides
void VkBackend::BlitToScreen(VkCommandBuffer cb, Target* front, uint32_t index) {
    VkImageLayout swap_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    // drawn larger than shown (render_percent over 100): averaged down first, every pixel shown the mean of the drawn
    // pixels it covers (one bilinear tap per screen pixel weighs them unevenly at 1.5x: fine detail pulses); the
    // sharpening then works on the shown pixels (Direct3D 12's BlitFrame)
    MovieFrame* movie = front ? PickMovieFrame() : nullptr;
    VkImageView frame_view = front ? front->view : VK_NULL_HANDLE;
    uint32_t frame_w = front ? front->w : 1, frame_h = front ? front->h : 1;
    if (front && !movie) {
        float aspect = app::FrameAspect();
        float vw = std::min(float(extent_.width), float(extent_.height) * aspect), vh = vw / aspect;
        float k = std::max(float(front->w) / vw, float(front->h) / vh);
        if (k > 1.01f) {
            uint32_t ow = uint32_t(std::lround(vw)), oh = uint32_t(std::lround(vh));
            if (shown_.image && (shown_w_ != ow || shown_h_ != oh)) Retire(shown_);
            if (!shown_.image) {
                VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                ci.imageType = VK_IMAGE_TYPE_2D;
                ci.format = kColorFormat;
                ci.extent = {ow, oh, 1};
                ci.mipLevels = 1;
                ci.arrayLayers = 1;
                ci.samples = VK_SAMPLE_COUNT_1_BIT;
                ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                if (MakeImage(shown_, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) {
                    shown_w_ = ow;
                    shown_h_ = oh;
                }
            }
            if (shown_.image) {
                ToLayout(*front, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                ToLayout(shown_.image, false, shown_.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                VkRenderingAttachmentInfoKHR sa{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
                sa.imageView = shown_.view;
                sa.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                sa.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                sa.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                VkRenderingInfoKHR sri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
                sri.renderArea = {{0, 0}, {ow, oh}};
                sri.layerCount = 1;
                sri.colorAttachmentCount = 1;
                sri.pColorAttachments = &sa;
                vkCmdBeginRenderingKHR(cb, &sri);
                VkViewport svp{0, float(oh), float(ow), -float(oh), 0.0f, 1.0f};
                VkRect2D ssc{{0, 0}, {ow, oh}};
                vkCmdSetViewport(cb, 0, 1, &svp);
                vkCmdSetScissor(cb, 0, 1, &ssc);
                float sk[32];
                BlitConstants(sk, front->w, front->h, false);
                sk[5] = 1.0f;
                sk[6] = std::ceil(k - 0.01f);
                sk[8] = 0.0f;
                bool ok = BlitPass(front->view, sk, kColorFormat);
                vkCmdEndRenderingKHR(cb);
                ToLayout(shown_.image, false, shown_.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                if (ok) {
                    frame_view = shown_.view;
                    frame_w = ow;
                    frame_h = oh;
                }
            }
        }
    }
    if (front) ToLayout(*front, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // [graphics] smaa: the frame's edges smoothed at its own size, before the scale to the screen (not on movies)
    if (front && !g_movie && app::Get().smaa) {
        if (VkImageView aa = Smaa(cb, frame_view, frame_w, frame_h)) frame_view = aa;
    }
    ToLayout(images_[index], false, swap_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    ca.imageView = image_views_[index];
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ca.clearValue.color.float32[3] = 1.0f;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, extent_};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRenderingKHR(cb, &ri);
    float sw = float(extent_.width), sh = float(extent_.height);
    VkRect2D sc{{0, 0}, extent_};
    vkCmdSetScissor(cb, 0, 1, &sc);
    if (MovieFrame* m = movie) {
        // a movie: placed by movie_display, sharpened at the console's pixel size (Direct3D 12's BlitMovie)
        MoviePlace place = PlaceMovie(app::Get().movie_display, g_movie_shape);
        float vw = std::min(sw, sh * place.aspect), vh = vw / place.aspect;
        if (place.stretch) {
            vw = sw;
            vh = sh;
        }
        // the pass's triangle is written D3D's way up: a negative height stands it the right way
        VkViewport vp{(sw - vw) / 2, (sh - vh) / 2 + vh, vw, -vh, 0.0f, 1.0f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        float c[32];
        BlitConstants(c, front->gw, front->gh, gamma_, true);
        HdrConstants(c);
        std::copy(place.uv, place.uv + 4, c);
        c[8] = std::min(app::Get().movie_sharpening, 100u) / 100.0f;
        BlitPass(m->img.view, c, swap_fmt_);
    } else if (front) {
        // 16:9 (or the screen's wider shape) when the console reports a widescreen display, else 4:3 (as BlitFrame)
        float aspect = app::FrameAspect();
        float w = std::min(sw, sh * aspect), h = w / aspect;
        VkViewport vp{(sw - w) / 2, (sh - h) / 2 + h, w, -h, 0.0f, 1.0f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        float c[32];
        BlitConstants(c, frame_w, frame_h, gamma_, true);
        HdrConstants(c);
        BlitPass(frame_view, c, swap_fmt_);
        BlackSides(cb, extent_.width, extent_.height, aspect);
    }
    vkCmdEndRenderingKHR(cb);
}

VkDescriptorSet VkBackend::CachedSet(VkDeviceSize range0, VkDeviceSize range1, const VkDescriptorImageInfo imgs[4],
                                     const VkDescriptorImageInfo smps[4]) {
    Frame& f = frames_[frame_];
    struct {
        uint64_t r0, r1;
        uint64_t view[4], layout[4], smp[4];
    } key{};
    key.r0 = range0;
    key.r1 = range1;
    for (int i = 0; i < 4; i++) {
        // (handles are pointers on 64-bit builds, integers on 32-bit)
        std::memcpy(&key.view[i], &imgs[i].imageView, sizeof(imgs[i].imageView));
        key.layout[i] = uint64_t(imgs[i].imageLayout);
        std::memcpy(&key.smp[i], &smps[i].sampler, sizeof(smps[i].sampler));
    }
    uint64_t h = Hash(&key, sizeof(key));
    auto it = f.set_cache.find(h);
    if (it != f.set_cache.end()) return it->second;
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = f.sets;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &set_layout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &dai, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
    f.sets_used++;
    VkDescriptorBufferInfo bufs[2] = {{f.upload, 0, range0}, {f.upload, 0, range1}};
    VkWriteDescriptorSet w[10]{};
    for (uint32_t i = 0; i < 2; i++) {
        w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        w[i].pBufferInfo = &bufs[i];
    }
    for (uint32_t i = 0; i < 4; i++) {
        w[2 + i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[2 + i].dstSet = set;
        w[2 + i].dstBinding = 8 + i;
        w[2 + i].descriptorCount = 1;
        w[2 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        w[2 + i].pImageInfo = &imgs[i];
        w[6 + i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[6 + i].dstSet = set;
        w[6 + i].dstBinding = 16 + i;
        w[6 + i].descriptorCount = 1;
        w[6 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        w[6 + i].pImageInfo = &smps[i];
    }
    vkUpdateDescriptorSets(device_, 10, w, 0, nullptr);
    f.set_cache[h] = set;
    return set;
}

bool VkBackend::PassSet(const VkImageView src[3], const VkImageLayout layout[3], const float p[8]) {
    if (!EnsureBlitSamplers()) return false;
    VkDeviceSize off;
    uint8_t* cpu;
    if (!Upload(128, ubo_align_, false, off, cpu)) return false;
    std::memset(cpu, 0, 128);
    std::memcpy(cpu, p, 8 * sizeof(float));
    VkDescriptorImageInfo imgs[4], smps[4];
    for (int i = 0; i < 4; i++) {
        if (i < 3 && src[i]) imgs[i] = {VK_NULL_HANDLE, src[i], layout[i]};
        else imgs[i] = {VK_NULL_HANDLE, dummy_[0].view, dummy_[0].layout};
        smps[i] = {blit_smp_[i == 0 ? 0 : 1], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    VkDescriptorSet set = CachedSet(128, 128, imgs, smps);
    if (!set) return false;
    const uint32_t dyn[2] = {uint32_t(off), uint32_t(off)};
    vkCmdBindDescriptorSets(Cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_layout_, 0, 1, &set, 2, dyn);
    return true;
}

bool VkBackend::PassDraw(const std::string& hlsl, uint64_t key, const char* entry, VkFormat dst_fmt,
                         const VkImageView src[3], const float p[8]) {
    uint64_t pkey = key ^ Hash(entry, std::strlen(entry)) ^ (uint64_t(dst_fmt) << 40);
    VkPipeline pipe = VK_NULL_HANDLE;
    auto it = pass_pipes_.find(pkey);
    if (it != pass_pipes_.end()) {
        pipe = it->second;
    } else {
        VkShaderModule vs = ShaderModule(key ^ 0x5A55000000000001ull, hlsl, spirv::Stage::Vertex, "vs");
        VkShaderModule ps = ShaderModule(key ^ Hash(entry, std::strlen(entry)), hlsl, spirv::Stage::Pixel, entry);
        if (vs && ps) {
            VkPipelineShaderStageCreateInfo st[2]{};
            st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "vs", nullptr};
            st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, entry, nullptr};
            VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            vp.viewportCount = vp.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            rs.cullMode = VK_CULL_MODE_NONE;
            rs.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState cba{};
            cba.colorWriteMask = 0xF;
            VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            bs.attachmentCount = 1;
            bs.pAttachments = &cba;
            VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dy.dynamicStateCount = 2;
            dy.pDynamicStates = dyn;
            VkPipelineRenderingCreateInfoKHR ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
            ri.colorAttachmentCount = 1;
            ri.pColorAttachmentFormats = &dst_fmt;
            VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            pi.pNext = &ri;
            pi.stageCount = 2;
            pi.pStages = st;
            pi.pVertexInputState = &vi;
            pi.pInputAssemblyState = &ia;
            pi.pViewportState = &vp;
            pi.pRasterizationState = &rs;
            pi.pMultisampleState = &ms;
            pi.pColorBlendState = &bs;
            pi.pDynamicState = &dy;
            pi.layout = pipe_layout_;
            if (vkCreateGraphicsPipelines(device_, pipe_cache_, 1, &pi, nullptr, &pipe) != VK_SUCCESS) pipe = VK_NULL_HANDLE;
        }
        pass_pipes_[pkey] = pipe;
    }
    if (!pipe) return false;
    vkCmdBindPipeline(Cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    const VkImageLayout lay[3] = {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (!PassSet(src, lay, p)) return false;
    VkCommandBuffer cb = Cmd();
    vkCmdDraw(cb, 3, 1, 0, 0);
    return true;
}

VkImageView VkBackend::Smaa(VkCommandBuffer cb, VkImageView view, uint32_t w, uint32_t h) {
    if (smaa_failed_ || !EnsureBlitSamplers()) return VK_NULL_HANDLE;
    auto image = [&](Image& im, VkFormat fmt, uint32_t iw, uint32_t ih, VkImageUsageFlags usage) {
        if (im.image) return true;
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = fmt;
        ci.extent = {iw, ih, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
        return MakeImage(im, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
    };
    // the area and search tables, once
    if (!smaa_area_.image) {
        bool ok = true;
        auto table = [&](Image& im, VkFormat fmt, uint32_t tw, uint32_t th, uint32_t pitch, const unsigned char* bytes) {
            if (!image(im, fmt, tw, th, VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return false;
            VkDeviceSize off;
            uint8_t* cpu;
            if (!Upload(VkDeviceSize(pitch) * th, 16, false, off, cpu)) return false;
            std::memcpy(cpu, bytes, size_t(pitch) * th);
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            Barrier(im.image, range, im.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy c{};
            c.bufferOffset = off;
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.imageExtent = {tw, th, 1};
            vkCmdCopyBufferToImage(Cmd(), frames_[frame_].upload, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            Barrier(im.image, range, im.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            return true;
        };
        ok = table(smaa_area_, VK_FORMAT_R8G8_UNORM, AREATEX_WIDTH, AREATEX_HEIGHT, AREATEX_PITCH, areaTexBytes) &&
             table(smaa_search_, VK_FORMAT_R8_UNORM, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, SEARCHTEX_PITCH, searchTexBytes);
        if (!ok) {  // (memory or the ring: tried again next frame)
            Retire(smaa_area_);
            Retire(smaa_search_);
            LOG_FIRST(2, "vulkan: SMAA's tables could not be made this frame");
            return VK_NULL_HANDLE;
        }
    }
    // its targets at the frame's size
    if (smaa_w_ != w || smaa_h_ != h) {
        Retire(smaa_edges_);
        Retire(smaa_weights_);
        Retire(smaa_out_);
        smaa_w_ = w;
        smaa_h_ = h;
    }
    const VkImageUsageFlags rt = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (!image(smaa_edges_, VK_FORMAT_R8G8_UNORM, w, h, rt) || !image(smaa_weights_, VK_FORMAT_R8G8B8A8_UNORM, w, h, rt) ||
        !image(smaa_out_, kColorFormat, w, h, rt)) {
        LOG_FIRST(1, "vulkan: SMAA's targets could not be made at %ux%u", w, h);
        return VK_NULL_HANDLE;
    }
    const std::string& hlsl = SmaaSource();
    static const uint64_t key = Hash(hlsl.data(), hlsl.size());
    float p[8] = {1.0f / float(w), 1.0f / float(h), float(w), float(h)};
    VkViewport vp{0, float(h), float(w), -float(h), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {w, h}};
    auto pass = [&](Image& dst, VkFormat fmt, bool clear, const char* entry, VkImageView a, VkImageView b, VkImageView c3) {
        EndRendering();
        OrderWrites();
        ToLayout(dst.image, false, dst.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
        ca.imageView = dst.view;
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // edges discards
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
        ri.renderArea = sc;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &ca;
        vkCmdBeginRenderingKHR(cb, &ri);
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
        const VkImageView src[3] = {a, b, c3};
        bool ok = PassDraw(hlsl, key, entry, fmt, src, p);
        vkCmdEndRenderingKHR(cb);
        writes_open_ = true;
        ToLayout(dst.image, false, dst.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return ok;
    };
    // 1: edges (the pass discards edge-free pixels, so cleared first); 2: blending weights; 3: the frame blended
    if (!pass(smaa_edges_, VK_FORMAT_R8G8_UNORM, true, "edges", view, VK_NULL_HANDLE, VK_NULL_HANDLE) ||
        !pass(smaa_weights_, VK_FORMAT_R8G8B8A8_UNORM, true, "weights", smaa_edges_.view, smaa_area_.view,
              smaa_search_.view) ||
        !pass(smaa_out_, kColorFormat, false, "blend", view, smaa_weights_.view, VK_NULL_HANDLE)) {
        // off for good only when a pass's pipeline could not be made (its shaders); a full ring is this frame's
        for (auto& [k, pl] : pass_pipes_)
            if (!pl) {
                smaa_failed_ = true;
                LOGR("vulkan: SMAA's shaders did not compile; SMAA off");
                break;
            }
        return VK_NULL_HANDLE;
    }
    return smaa_out_.view;
}

void VkBackend::BlackSides(VkCommandBuffer cb, uint32_t w, uint32_t h, float aspect) {
    if (frame_sides_ >= 0.999f) return;
    float vw = std::min(float(w), float(h) * aspect), keep = vw * frame_sides_;
    int32_t l = int32_t((float(w) - keep) / 2), r = int32_t((float(w) + keep) / 2);
    VkClearAttachment at{};
    at.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    at.clearValue.color.float32[3] = 1.0f;
    VkClearRect rs[2];
    uint32_t n = 0;
    if (l > 0) rs[n++] = {{{0, 0}, {uint32_t(l), h}}, 0, 1};
    if (r < int32_t(w)) rs[n++] = {{{r, 0}, {w - uint32_t(r), h}}, 0, 1};
    if (n) vkCmdClearAttachments(cb, 1, &at, n, rs);
}

static double MovieClock() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// the movie frame just drawn, averaged down to up to twice the console's size into the ring (KeepMovieFrame)
void VkBackend::KeepMovieFrame(Target& t) {
    if (!movie_on_) {
        movie_on_ = true;
        movie_n_ = 0;
    }
    MovieFrame& f = movie_ring_[movie_n_ % kMovieFrames];
    uint32_t kw = std::min(t.w, std::max(t.gw, 1u) * 2), kh = std::min(t.h, std::max(t.gh, 1u) * 2);
    float k = std::max(float(t.w) / float(std::max(kw, 1u)), float(t.h) / float(std::max(kh, 1u)));
    if (f.img.image && (f.w != kw || f.h != kh)) Retire(f.img);  // the screen changed
    if (!f.img.image) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = kColorFormat;
        ci.extent = {kw, kh, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (!MakeImage(f.img, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) return;
        f.w = kw;
        f.h = kh;
    }
    EndRendering();
    OrderWrites();
    ToLayout(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ToLayout(f.img.image, false, f.img.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkCommandBuffer cb = Cmd();
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    ca.imageView = f.img.view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {kw, kh}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRenderingKHR(cb, &ri);
    VkViewport vp{0, float(kh), float(kw), -float(kh), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {kw, kh}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    float c[32];
    BlitConstants(c, t.w, t.h, false);
    c[5] = 1.0f;  // alpha kept (Direct3D 12's Shrink)
    c[6] = std::max(1.0f, std::ceil(k - 0.01f));
    c[8] = 0.0f;
    bool ok = BlitPass(t.view, c, kColorFormat);
    vkCmdEndRenderingKHR(cb);
    writes_open_ = true;
    ToLayout(f.img.image, false, f.img.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!ok) return;
    f.time = MovieClock();
    movie_n_++;
}

// the newest kept frame old enough for the cutscene sound delay, else the oldest kept; null without a movie
VkBackend::MovieFrame* VkBackend::PickMovieFrame() {
    if (!movie_on_ || !movie_n_) return nullptr;
    double due = MovieClock() - app::Get().movie_delay / 1000.0;
    uint32_t kept = std::min(movie_n_, kMovieFrames);
    MovieFrame* pick = &movie_ring_[(movie_n_ - kept) % kMovieFrames];
    for (uint32_t i = movie_n_; i-- > movie_n_ - kept;) {
        MovieFrame& f = movie_ring_[i % kMovieFrames];
        if (f.time <= due) {
            pick = &f;
            break;
        }
    }
    if (kept == kMovieFrames && pick->time > due)
        LOG_FIRST(1, "vulkan: cutscene sound delay held to %u frames (the game presents faster than the delay allows)",
                  kMovieFrames);
    return pick->img.image ? pick : nullptr;
}

// gpu::g_art_shot: the newest kept movie frame, its black bars cut off, as a PNG (as Direct3D 12's ArtShot)
void VkBackend::ArtShot() {
    std::string path;
    {
        std::lock_guard lock(g_art_shot_lock);
        if (g_art_shot.empty() || !movie_n_) return;
        path = g_art_shot;
    }
    MovieFrame& f = movie_ring_[(movie_n_ - 1) % kMovieFrames];
    if (!f.img.image) return;
    uint32_t w = f.w, h = f.h;
    if (!EnsureReadback(VkDeviceSize(w) * h * 4)) return;
    ToLayout(f.img.image, false, f.img.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(Cmd(), f.img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1, &cp);
    ToLayout(f.img.image, false, f.img.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ReadbackToHost();
    Submit(true);
    if (lost_ || !readback_cpu_) return;
    std::vector<uint8_t> px(readback_cpu_, readback_cpu_ + size_t(w) * h * 4);
    auto lum = [&](uint32_t x, uint32_t y) {
        const uint8_t* p = &px[(size_t(y) * w + x) * 4];
        return (int(p[0]) + 2 * int(p[1]) + int(p[2])) / 4;  // BGRA
    };
    auto row_dark = [&](uint32_t y) {
        int sum = 0;
        for (uint32_t x = 0; x < w; x += 4) sum += lum(x, y);
        return sum / int((w + 3) / 4) < 12;
    };
    auto col_dark = [&](uint32_t x, uint32_t y0, uint32_t y1) {
        int sum = 0;
        for (uint32_t y = y0; y < y1; y += 4) sum += lum(x, y);
        return sum / int(std::max(1u, (y1 - y0 + 3) / 4)) < 12;
    };
    uint32_t y0 = 0, y1 = h, x0 = 0, x1 = w;
    while (y0 < h && row_dark(y0)) y0++;
    while (y1 > y0 && row_dark(y1 - 1)) y1--;
    while (x0 < w && col_dark(x0, y0, y1)) x0++;
    while (x1 > x0 && col_dark(x1 - 1, y0, y1)) x1--;
    uint32_t cw = x1 > x0 ? x1 - x0 : 0, ch = y1 > y0 ? y1 - y0 : 0;
    if (cw < w / 2 || ch < h / 3) return;  // nearly black: a later frame
    long long total = 0;
    for (uint32_t y = y0; y < y1; y += 2)
        for (uint32_t x = x0; x < x1; x += 2) total += lum(x, y);
    if (total / std::max(1LL, (long long)((cw + 1) / 2) * ((ch + 1) / 2)) < 40) return;  // too dark to be a picture
    std::vector<uint8_t> out(size_t(cw) * ch * 4);
    for (uint32_t y = 0; y < ch; y++)
        for (uint32_t x = 0; x < cw; x++) {
            const uint8_t* sp = &px[(size_t(y + y0) * w + x + x0) * 4];
            uint8_t* d = &out[(size_t(y) * cw + x) * 4];
            d[0] = sp[2];
            d[1] = sp[1];
            d[2] = sp[0];
            d[3] = 255;
        }
    {
        std::lock_guard lock(g_art_shot_lock);
        if (g_art_shot != path) return;  // asked for another meanwhile
        g_art_shot.clear();
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    pack::WritePngLater(path, cw, ch, std::move(out));
    LOGR("art: %s (%ux%u)", path.c_str(), cw, ch);
}

// \ (or a test's XBE_SHOT_FRAMES / SHOT): the frame as the screen pass draws it, at the frame's own size (its height,
// its width at the frame's shape when widescreen), as Direct3D 12's Screenshot names and writes it. Movies and the
// colour looks come with S9 / S10.
void VkBackend::Screenshot(Target& t, const std::string& suffix) {
    uint32_t h = t.h, w = app::Widescreen() ? uint32_t(float(t.h) * app::FrameAspect() + 0.5f) : t.w;
    // a movie as the screen shows it (S9, as Direct3D 12): the screen's shape at the frame's height, the picture placed
    // by movie_display with its bars
    MovieFrame* movie = PickMovieFrame();
    float vx = 0, vy = 0, vw = 0, vh = 0;
    MoviePlace place;
    if (movie) {
        place = PlaceMovie(app::Get().movie_display, g_movie_shape);
        w = uint32_t(uint64_t(h) * std::max(extent_.width, 1u) / std::max(extent_.height, 1u));
        vw = std::min(float(w), float(h) * place.aspect);
        vh = vw / place.aspect;
        if (place.stretch) {
            vw = float(w);
            vh = float(h);
        }
        vx = (float(w) - vw) / 2;
        vy = (float(h) - vh) / 2;
    }
    Image shot;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = kColorFormat;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!EnsureReadback(VkDeviceSize(w) * h * 4) || !MakeImage(shot, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT)) {
        LOGW("screenshot: the frame could not be read back");
        return;
    }
    ToLayout(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ToLayout(shot.image, false, shot.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkCommandBuffer cb = Cmd();
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    ca.imageView = shot.view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ca.clearValue.color.float32[3] = 1.0f;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {w, h}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRenderingKHR(cb, &ri);
    VkViewport vp{0, float(h), float(w), -float(h), 0.0f, 1.0f};
    if (movie) vp = {vx, vy + vh, vw, -vh, 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {w, h}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    float c[32];
    bool ok;
    if (movie) {
        BlitConstants(c, t.gw, t.gh, gamma_, true);
        std::copy(place.uv, place.uv + 4, c);
        c[8] = std::min(app::Get().movie_sharpening, 100u) / 100.0f;
        ok = BlitPass(movie->img.view, c, kColorFormat);
    } else {
        BlitConstants(c, t.w, t.h, gamma_, true);
        ok = BlitPass(t.view, c, kColorFormat);
        BlackSides(cb, w, h, float(w) / float(std::max(h, 1u)));
    }
    vkCmdEndRenderingKHR(cb);
    ToLayout(shot.image, false, shot.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cb, shot.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1, &cp);
    ReadbackToHost();
    Submit(true);  // a screenshot waits for its frame (rare: a key or a test)
    if (!ok || lost_ || !readback_cpu_) {
        DestroyImage(shot);
        LOGW("screenshot: the frame could not be read back");
        return;
    }
    std::vector<uint8_t> px(readback_cpu_, readback_cpu_ + size_t(w) * h * 4);
    DestroyImage(shot);  // (the GPU is done with it: Submit waited)
    for (size_t i = 0; i < px.size(); i += 4) {  // BGRA to RGBA, opaque
        std::swap(px[i], px[i + 2]);
        px[i + 3] = 255;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::string prefix = app::kGame.log;
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
    char name[96];
    std::snprintf(name, sizeof(name), "%s_%04u-%02u-%02u_%02u-%02u-%02u_%03u.png", prefix.c_str(), st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::filesystem::path file = app::ScreenshotDir() / (suffix.empty() ? std::string(name) : prefix + suffix);
    pack::WritePngLater(file, w, h, std::move(px));
    LOGR("screenshot: %s (%ux%u)", file.string().c_str(), w, h);
}

// ---- F3 frame captures (S2)

void VkBackend::StartCapture() {
    SYSTEMTIME tm;
    GetLocalTime(&tm);
    char stamp[32];
    snprintf(stamp, sizeof(stamp), "%02u%02u%02u_%03u", tm.wHour, tm.wMinute, tm.wSecond, tm.wMilliseconds);
    std::error_code ec;
    // beside the log folder: logs\..\captures by default, or next to a launcher's log (as Direct3D 12)
    std::filesystem::path base = app::Args().log.empty() ? app::ExeDir() : app::LogFolder();
    cap_dir_ = std::filesystem::weakly_canonical(base / diag::CaptureDir() / stamp, ec);
    std::filesystem::create_directories(cap_dir_, ec);
    cap_ = _wfopen((cap_dir_ / "frame.txt").c_str(), L"w");
    cap_i_ = cap_rt_ = 0;
    cap_seen_.clear();
    cap_sh_.clear();
    cap_tex_.clear();
}

void VkBackend::FinishCapture(const Surface& front) {
    CapStep(Surface{});
    if (Target* f = FindTarget(front, false)) CapSave(*f, "final");
    for (const CapTex& t : cap_tex_) {
        char name[32];
        snprintf(name, sizeof(name), "tex_%08X", t.data);
        CapSave(t.view, t.layout, t.sw, t.sh, t.w, t.h, 1.0f, name);
    }
    fclose(cap_);
    cap_ = nullptr;
    cap_tex_.clear();
    // the images retired while it ran go the usual way now
    for (Image& im : cap_hold_) frames_[frame_].garbage.push_back(im);
    cap_hold_.clear();
    LOGI("vulkan: frame captured to %s", cap_dir_.string().c_str());
}

// the frame moves to another target: save the one it leaves
void VkBackend::CapStep(const Surface& rt) {
    uint32_t d = rt.header ? mem::VirtualFromPhysical(rt.data) : 0;
    if (d == cap_rt_) return;
    char name[32];
    auto it = targets_.find(TargetKey(cap_rt_, false));
    if (cap_rt_ && it != targets_.end()) {
        snprintf(name, sizeof(name), "%04u_%08X", cap_i_, cap_rt_);
        CapSave(it->second, name);
    }
    auto z = targets_.find(TargetKey(cap_rt_, true));
    if (cap_rt_ && z != targets_.end()) {
        snprintf(name, sizeof(name), "%04u_%08X_z", cap_i_, cap_rt_);
        CapSave(z->second, name);
    }
    cap_rt_ = d;
}

void VkBackend::CapSave(Target& t, const char* name) {
    if (!t.image) return;
    if (t.depth) {
        // its depth alone, read as the red channel (as Direct3D 12's R24 view)
        if (!t.sample_view) return;
        ToLayout(t, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        // read unfiltered where the depth format can't be filtered linearly (review of S2)
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(gpu_, depth_fmt_, &fp);
        bool point = !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
        CapSave(t.sample_view, t.layout, t.w, t.h, t.gw, t.gh, 1.0f, name, point);
        return;
    }
    ToLayout(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CapSave(t.view, t.layout, t.w, t.h, t.gw, t.gh, 1.0f, name);
}

void VkBackend::CapSave(VkImageView view, VkImageLayout layout, uint32_t src_w, uint32_t src_h, uint32_t w, uint32_t h,
                        float mul, const char* name, bool point) {
    if (!view || !w || !h || lost_) return;
    Image im;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = kColorFormat;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!EnsureReadback(VkDeviceSize(w) * h * 4) || !MakeImage(im, ci, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT))
        return;
    EndRendering();
    ToLayout(im.image, false, im.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkCommandBuffer cb = Cmd();
    VkRenderingAttachmentInfoKHR ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    ca.imageView = im.view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfoKHR ri{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = {{0, 0}, {w, h}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    vkCmdBeginRenderingKHR(cb, &ri);
    VkViewport vp{0, float(h), float(w), -float(h), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {w, h}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    // shrunk as Direct3D 12's CapSave shrinks: one bilinear tap a pixel, alpha kept (review of S2)
    float c[32];
    BlitConstants(c, src_w, src_h, false);
    c[5] = 1.0f;
    c[6] = 1.0f;
    c[7] = mul;
    c[8] = 0.0f;  // no sharpening
    bool ok = BlitPass(view, layout, c, kColorFormat, false, point);
    vkCmdEndRenderingKHR(cb);
    ToLayout(im.image, false, im.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cb, im.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1, &cp);
    ReadbackToHost();
    Submit(true);
    DestroyImage(im);  // (the GPU is done with it: Submit waited)
    if (!ok || lost_ || !readback_cpu_) return;
    std::vector<uint8_t> px(readback_cpu_, readback_cpu_ + size_t(w) * h * 4);
    WriteCaptureBmp(cap_dir_ / (std::string(name) + ".bmp"), w, h, px);
}

// the draw's line: its state, then its textures (Direct3D 12's fields; blend, depth and cull in Vulkan's terms)
void VkBackend::CaptureDraw(const State& s, uint32_t prim, uint32_t count, const PipeKey& k, const DrawConstants& c,
                            Target* ds, const StageBind sb[4], const TexInfo info[4]) {
    fprintf(cap_,
            "%04u draw rt %08X ds %08X prim %u count %u vs %08X ps %08X blend %u %X %X zen %u %X zwrite %u "
            "zbias %d %.2f cull %u cw %X c0 %.3f %.3f %.3f %.3f",
            cap_i_++, s.rt.data, ds ? s.zs.data : 0, prim, count, s.vs_handle, s.ps ? s.ps->handle : 0, k.blend, k.src,
            k.dst, k.zen, k.zfunc, k.zwrite, k.zbias, k.zslope, k.cull ? (k.ccw ? 3u : 2u) : 1u, k.wmask, c.pc[0][0],
            c.pc[0][1], c.pc[0][2], c.pc[0][3]);
    fprintf(cap_, " atest %g %g wvp", c.misc[1], c.misc[0]);
    for (int i = 0; i < 16; i++) fprintf(cap_, " %g", c.wvp[i]);
    for (int m : {256, 2, 3}) {  // world, view, projection: the diagonal and the translation row
        const float* t = s.transforms[size_t(m)].data();
        fprintf(cap_, " | T%d %g %g %g %g / %g %g %g", m, t[0], t[5], t[10], t[15], t[12], t[13], t[14]);
    }
    for (int i = 0; i < 4; i++) {
        TextureDesc d;
        if (!s.textures[size_t(i)] || !Describe(s.textures[size_t(i)], d)) continue;
        fprintf(cap_, " | t%d %08X fmt %X %ux%u%s%s", i, d.data, d.format, d.width, d.height, sb[i].target ? " target" : "",
                sb[i].view ? "" : " unbound");
        if (sb[i].view && !sb[i].target && info[i].kind == kTex2D && cap_seen_.insert(d.data).second)
            cap_tex_.push_back({sb[i].view, sb[i].layout, d.data, d.width, d.height, std::max(sb[i].host_w, 1u),
                                std::max(sb[i].host_h, 1u)});
    }
}

// each shader once, as translated
void VkBackend::CaptureShader(const char* kind, uint64_t key, const std::string& src) {
    if (!cap_sh_.insert(key).second) return;
    char name[48];
    snprintf(name, sizeof(name), "%s_%016llX.hlsl", kind, (unsigned long long)key);
    if (FILE* f = _wfopen((cap_dir_ / name).c_str(), L"w")) {
        fwrite(src.data(), 1, src.size(), f);
        fclose(f);
    }
    fprintf(cap_, "     %s\n", name);
}

void VkBackend::Present(const Surface& front) {
    PerfAdd perf{present_ms_};
    if (lost_ || !device_) return;
    {
        // the frame rate every 5 seconds in the log, as the Direct3D 12 renderer writes it (side-by-side tests)
        static auto fps_t = std::chrono::steady_clock::now();
        static uint64_t fps_n = 0;
        fps_n++;
        auto now = std::chrono::steady_clock::now();
        double el = std::chrono::duration<double, std::milli>(now - fps_t).count();
        if (el >= 5000.0) {
            LOGR("fps: %.1f (%.2f ms a frame, the last 5 s; vulkan)", fps_n * 1000.0 / el, el / double(fps_n));
            // XBE_PERF: draws a frame and how many went the repeat path (S16); the render thread's time a frame
            if (g_vk_perf) {
                double n = double(fps_n);
                LOGR("perf: vulkan %.0f draws a frame, %.0f%% repeats, %u descriptor sets in the last frame",
                     double(draws_) / n, draws_ ? 100.0 * double(repeats_) / double(draws_) : 0.0,
                     frames_[frame_].sets_used);
                LOGR("perf: vulkan render thread a frame: draws %.2f ms (%.1f us a draw), present %.2f ms, waits for "
                     "the GPU %.1f (%.2f ms, in those)",
                     draw_ms_ / n, draws_ ? 1000.0 * draw_ms_ / double(draws_) : 0.0, present_ms_ / n,
                     double(waits_) / n, wait_ms_ / n);
            }
            if (g_vk_perf && draws_)
                LOGR("perf: vulkan repeat path not taken: state changed %.0f%%, no draw to repeat %.0f%%, another "
                     "rendering %.0f%%, other 2D placement or vertex layout %.0f%%",
                     100.0 * no_rep_[0] / draws_, 100.0 * no_rep_[1] / draws_, 100.0 * no_rep_[2] / draws_,
                     100.0 * no_rep_[3] / draws_);
            for (uint64_t& n : no_rep_) n = 0;
            draws_ = repeats_ = waits_ = 0;
            draw_ms_ = present_ms_ = wait_ms_ = 0;
            fps_t = now;
            fps_n = 0;
        }
    }
    // the window resized, minimised or v-sync changed in the menu: a new swapchain
    // (S17: fast forward is not held to the screen's refresh, as on Direct3D 12: the swapchain is made again with
    // an immediate present mode while it is held)
    if (!swap_ || vsync_ != WantVsync() || hdr_wanted_ != app::Get().hdr) {
        Submit(true);
        vkDeviceWaitIdle(device_);
        if (!CreateSwapchain()) {
            LOG_FIRST(4, "vulkan: the swapchain could not be made again: %s", error_.c_str());
            return;
        }
        if (!swap_) {  // minimised: the frame's work still goes to the GPU
            Submit(false);
            return;
        }
    }

    if (cap_) FinishCapture(front);
    else if (CaptureOn() && (g_capture.exchange(false) || AutoCaptureDue())) StartCapture();
    if (ScaleChanged()) return;  // this frame's targets are gone; the next is drawn at the new size
    // the front buffer's target, made when the GPU never drew it, and what the CPU wrote into it (D4, as Direct3D 12)
    Target* t = EnsureTarget(front, false);
    if (t && CpuWrote(*t)) UploadTarget(*t, front);
    if (!t) LOG_FIRST(2, "vulkan: Present found no target for the front buffer %08X (black)", front.data);
    // room in the ring and the descriptor pool for the screen's passes (and a screenshot's), as Draw makes it
    Room(8);
    UpdateGammaRamp();
    frame_sides_ = g_frame_sides.exchange(1.0f);
    if (!g_movie) movie_on_ = false;  // first, so a screenshot just after a movie shows the game
    if (t && g_screenshot.exchange(false)) Screenshot(*t);  // (it submits and waits; the frame records on after it)
    // F7: this frame, then the next drawn with texture packs the other way round, as one pair of shots (as D3D12)
    if (t && pair_step_ == 1) {
        Screenshot(*t, pair_name_ + (g_pack_off ? "_original.png" : "_pack.png"));
        g_pack_off = !g_pack_off;
        pair_step_ = 0;
    } else if (t && g_pack_pair.exchange(false)) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char nm[64];
        std::snprintf(nm, sizeof(nm), "_%04u-%02u-%02u_%02u-%02u-%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
                      st.wMinute, st.wSecond);
        pair_name_ = nm;
        Screenshot(*t, pair_name_ + (g_pack_off ? "_original.png" : "_pack.png"));
        g_pack_off = !g_pack_off;
        pair_step_ = 1;
        LOGR("texture pack: comparison pair (F7)");
    }
    if (t && g_movie) {
        KeepMovieFrame(*t);
        ArtShot();
    }
    VkCommandBuffer cb = Cmd();
    if (!cb) return;
    EndRendering();
    Frame& f = frames_[frame_];
    uint32_t index = 0;
    VkResult r = vkAcquireNextImageKHR(device_, swap_, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        Submit(true);  // the frame's draws still run; the acquire semaphore was not signalled, so it stays usable
        vkDeviceWaitIdle(device_);
        if (!CreateSwapchain()) LOG_FIRST(4, "vulkan: the swapchain could not be made again: %s", error_.c_str());
        return;
    }
    if (r == VK_ERROR_DEVICE_LOST) {
        DeviceLost("acquire", r);
        return;
    }
    if (r == VK_ERROR_SURFACE_LOST_KHR && !::IsWindow(static_cast<HWND>(app::Window()))) {
        Submit(false);  // the window closed (the game is exiting): the frame's work still runs, nothing is shown
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        LOG_FIRST(4, "vulkan: vkAcquireNextImageKHR failed (%s)", ResultName(r));
        Submit(false);
        return;
    }
    presents_++;
    presents_seen_ = presents_;
    ReleaseOldTextures();
    if (t) ReadyRes(*t);  // an anti-aliased frame resolved before the screen pass reads it (S14)
    if (unsaved_ >= 64 || (unsaved_ && ::GetTickCount64() - saved_at_ > 60000)) {
        // new pipelines made while drawing: the driver's cache saved now (64 of them, or any after a minute), so a
        // crash or a closed window keeps them (S15; a minute, not 10 s: a big cache's save is a short hitch)
        unsaved_ = 0;
        saved_at_ = ::GetTickCount64();
        SavePipelineCache();
        LOGI("vulkan: pipelines so far: %u made while drawing, %u from the warm-up", made_drawing_, made_warm_);
    }
    BlitToScreen(cb, t, index);
    RenderUi(cb, index);
    CountFps();
    VkImageLayout swap_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // as the screen pass and the menu leave it
    ToLayout(images_[index], false, swap_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkEndCommandBuffer(cb);
    recording_ = false;
    vkResetFences(device_, 1, &f.done);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.acquired;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkSemaphore signals[2] = {rendered_[index], progress_};
    uint64_t values[2] = {0, serial_ + 1};  // (the binary semaphore's value is ignored)
    VkTimelineSemaphoreSubmitInfo tsi{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    si.signalSemaphoreCount = progress_ ? 2 : 1;
    si.pSignalSemaphores = signals;
    if (progress_) {
        tsi.signalSemaphoreValueCount = 2;
        tsi.pSignalSemaphoreValues = values;
        si.pNext = &tsi;
    }
    r = vkQueueSubmit(queue_, 1, &si, f.done);
    if (r == VK_SUCCESS) serial_ = values[1];
    if (r != VK_SUCCESS) {
        if (r == VK_ERROR_DEVICE_LOST) DeviceLost("queue submit", r);
        lost_ = true;  // the fence is reset and was never submitted: waiting on it would never end
        LOG_FIRST(4, "vulkan: vkQueueSubmit failed (%s)", ResultName(r));
        return;
    }

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &rendered_[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swap_;
    pi.pImageIndices = &index;
    // S17: each present numbered, so the next frame can wait for the one two back to reach the screen
    VkPresentIdKHR pid{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
    uint64_t id = present_id_ + 1;
    if (present_wait_) {
        pid.swapchainCount = 1;
        pid.pPresentIds = &id;
        pi.pNext = &pid;
    }
    r = vkQueuePresentKHR(queue_, &pi);
    if (present_wait_) {
        present_id_ = id;
        if (!swap_first_id_) swap_first_id_ = id;
        // a present that failed was never shown: its id is not waited for (review)
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) swap_first_id_ = id + 1;
    }
    frame_ = (frame_ + 1) % kFrames;
    // input lag, as Direct3D 12's waitable swap chain (latency 2): with v-sync at most two frames wait for the screen,
    // the title held here (never more than 100 ms) rather than three frames ahead
    if (present_wait_ && vsync_ && (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) && present_id_ >= swap_first_id_ + 2) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            LOGR("vulkan: at most 2 frames queued for the screen");
        }
        vkWaitForPresentKHR(device_, swap_, present_id_ - 2, 100000000ull);
    }
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        vkDeviceWaitIdle(device_);
        if (!CreateSwapchain()) LOG_FIRST(4, "vulkan: the swapchain could not be made again: %s", error_.c_str());
    } else if (r == VK_ERROR_DEVICE_LOST) {
        DeviceLost("present", r);
    } else if (r != VK_SUCCESS) {
        LOG_FIRST(4, "vulkan: vkQueuePresentKHR failed (%s)", ResultName(r));
    }
}

}  // namespace

std::unique_ptr<Backend> CreateVulkanBackend() { return std::make_unique<VkBackend>(); }

}  // namespace gpu
