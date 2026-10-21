// The game's hooks into the D3D12 renderer.
//
// Water (seen in Rohan) runs a wave simulation on three 128x128 colour targets. Each pass reads the last ones at offsets
// of exactly one texel, made for 128 texels across. At render scale those reads land several host texels apart: the
// grid comes apart into interleaved lattices, the waves saturate to stripes, and the water drawn from them turns flat
// and striped. Those targets stay at the console's size.
//
// The passes read the state both ways at once: the centre texel unfiltered, the neighbours filtered at 0.85 of a texel
// (the filter's blend is what damps the waves). Read half a texel back, as the renderer reads other targets, the
// unfiltered taps land a texel off from the filtered ones: the step no longer damps, and a character wading through
// the lake (Gates of Moria) left ripples that grew into full strength stripes and never faded. Read where the game's
// coordinates say, they fade as on the console (tried at the lake, against the half texel back).
#ifdef _WIN32
#include "gpu/ext.h"

namespace tt {

namespace {

class Renderer final : public gpu::Extension {
public:
    void MakeTarget(const gpu::Surface&, bool depth, uint32_t gw, uint32_t gh, float& scale, DXGI_FORMAT&) override {
        if (!depth && gw == 128 && gh == 128) scale = 1;
    }
    void Sampled(const gpu::TextureDesc&, const gpu::Target* t, uint8_t, gpu::TexInfo& info) override {
        if (t && !t->depth && t->gw == 128 && t->gh == 128) info.flags |= gpu::kTexExact;
    }
};

}  // namespace

void InitRenderer() { gpu::SetExtension(std::make_unique<Renderer>()); }

}  // namespace tt
#else
namespace tt {
void InitRenderer() {}
}  // namespace tt
#endif
