// The game's hooks into the D3D12 renderer.
//
// Water (seen in Rohan) runs a wave simulation on three 128x128 colour targets. Each pass reads the last ones at offsets
// of exactly one texel, made for 128 texels across. At render scale those reads land several host texels apart: the
// grid comes apart into interleaved lattices, the waves saturate to stripes, and the water drawn from them turns flat
// and striped. Those targets stay at the console's size.
#ifdef _WIN32
#include "gpu/ext.h"

namespace tt {

namespace {

class Renderer final : public gpu::Extension {
public:
    void MakeTarget(const gpu::Surface&, bool depth, uint32_t gw, uint32_t gh, float& scale, DXGI_FORMAT&) override {
        if (!depth && gw == 128 && gh == 128) scale = 1;
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
