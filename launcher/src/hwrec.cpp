// Athelas - "Recommended for this PC" (see hwrec.h)
#include "hwrec.h"
#include <windows.h>
#include <dxgi1_4.h>

typedef HRESULT (WINAPI *CreateFactory1_t)(REFIID, void **);

static void read_gpu(HwInfo &h)
{
    HMODULE m = LoadLibraryA("dxgi.dll");
    CreateFactory1_t create = m ? (CreateFactory1_t)(void *)GetProcAddress(m, "CreateDXGIFactory1") : nullptr;
    IDXGIFactory1 *f = nullptr;
    if (!create || FAILED(create(__uuidof(IDXGIFactory1), (void **)&f)) || !f) return;
    // the card with the most video memory that is real hardware (the one the games pick, as the fastest)
    IDXGIAdapter1 *a = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            // (DedicatedVideoMemory is a size_t: capped near 4 GB in this 32-bit program; the budget DXGI 1.4 reports
            // for the card's own memory is 64-bit and close to all of it)
            unsigned long long bytes = d.DedicatedVideoMemory;
            IDXGIAdapter3 *a3 = nullptr;
            if (SUCCEEDED(a->QueryInterface(__uuidof(IDXGIAdapter3), (void **)&a3)) && a3) {
                DXGI_QUERY_VIDEO_MEMORY_INFO vi;
                if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi)) && vi.Budget > bytes) bytes = vi.Budget;
                a3->Release();
            }
            unsigned mb = (unsigned)(bytes >> 20);
            if (h.gpu.empty() || mb > h.vramMB) {
                char name[128];
                WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name, nullptr, nullptr);
                h.gpu = name; h.vendor = d.VendorId; h.vramMB = mb;
            }
        }
        a->Release();
    }
    f->Release();
    // integrated graphics: Intel without its own memory, or any card with under 512 MB of its own
    h.integrated = h.gpu.empty() || h.vramMB < 512 || (h.vendor == 0x8086 && h.vramMB < 2048);
}

const HwInfo &hw_info()
{
    static HwInfo h;
    static bool done;
    if (done) return h;
    done = true;
    read_gpu(h);
    DEVMODEA dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        h.screenW = (int)dm.dmPelsWidth; h.screenH = (int)dm.dmPelsHeight;
        if (dm.dmDisplayFrequency > 1) h.refresh = (int)dm.dmDisplayFrequency;
    }
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    h.cores = (int)si.dwNumberOfProcessors;
    // the tier: mostly the card's own memory, which tracks how fast it is in these games; a slow processor (the
    // games run their logic on one or two cores) holds a strong card back one step
    if (h.integrated) h.tier = 0;
    else if (h.vramMB < 3500) h.tier = 1;      // (the budget is a little under the card's full memory)
    else if (h.vramMB < 9000) h.tier = 2;
    else h.tier = 3;
    if (h.cores < 4 && h.tier > 1) h.tier--;
    return h;
}

const char *hw_tier_name(int tier)
{
    static const char *n[] = { "light (built-in graphics or a handheld)", "mid-range", "strong", "very strong" };
    return n[tier < 0 ? 0 : tier > 3 ? 3 : tier];
}
