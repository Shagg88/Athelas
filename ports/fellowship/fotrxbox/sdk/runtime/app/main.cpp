// the host: window, message loop, crash dumps; the game is described by app::kGame
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdlib>
#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <timeapi.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cwchar>
#include <thread>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include "app/app.h"
#include "core/diag.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "image_symbols.h"
#include "kernel/kernel.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace app {

namespace {

HWND g_hwnd = nullptr;
constexpr UINT kApplyDisplay = WM_APP + 1, kOpenWindow = WM_APP + 2;
bool g_mode_changed = false;
bool g_apply_posted = false;  // a display apply is waiting in the queue or running

std::filesystem::path LogDir() { return LogFolder(); }

// crash dump in CrashDumps beside the exe, or beside the log when --log puts it elsewhere
LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* e) {
    // the dump holds no guest memory: the guest registers and return addresses go to the log
    if (kernel::Thread* t = kernel::Current(); e && t && t->ctx) {
        auto* r = e->ExceptionRecord;
        LOGE("exception %08lX at %p on guest thread %X", r->ExceptionCode, r->ExceptionAddress, t->id);
        if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
            uint64_t va = r->ExceptionInformation[1], g = uint64_t(reinterpret_cast<uintptr_t>(kernel::g_base));
            LOGE("  %s %llX, guest %08llX", r->ExceptionInformation[0] ? "write" : "read", (unsigned long long)va,
                 (unsigned long long)(va - g));
        }
        kernel::LogGuestStack(*t->ctx, 256);
    }
    auto dir = Args().log.empty() ? ExeDir() / "CrashDumps" : LogDir() / "CrashDumps";
    CreateDirectoryW(dir.c_str(), nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf(name, 64, L"%ls.%04u.%02u.%02u_%02u.%02u.%02u.dmp", kGame.name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    HANDLE f = CreateFileW((dir / name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), e, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                      MINIDUMP_TYPE(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo |
                                    MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory),
                      e ? &info : nullptr, nullptr, nullptr);
    CloseHandle(f);
    logx::Flush();
    return EXCEPTION_CONTINUE_SEARCH;
}

void DumpThreads() {
    gpu::g_capture = true;
    for (auto& t : kernel::Threads())
        if (t->ctx) {
            LOGI("thread %X", t->id);
            kernel::LogGuestStack(*t->ctx, 32);
        }
}

// window thread only
// the highest refresh rate the screen offers for a size, 0 when it has none
DWORD BestRefresh(const wchar_t* device, DWORD w, DWORD h) {
    DWORD best = 0;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(device, i, &dm); i++)
        if (dm.dmPelsWidth == w && dm.dmPelsHeight == h && dm.dmBitsPerPel == 32) best = std::max(best, dm.dmDisplayFrequency);
    return best;
}

void ApplyNow() {
    const Settings& s = Get();
    // the chosen screen, read from it alone: its own modes and refresh rates, not the main screen's
    HMONITOR mon = static_cast<HMONITOR>(ScreenMonitor());
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    if (g_mode_changed) {
        ChangeDisplaySettingsExW(mi.szDevice, nullptr, nullptr, 0, nullptr);
        g_mode_changed = false;
    }
    Mode out = Output();
    LONG_PTR visible = IsWindowVisible(g_hwnd) ? WS_VISIBLE : 0;  // a window held back for a splash or handover stays so
    DEVMODEW desktop{};
    desktop.dmSize = sizeof(desktop);
    EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &desktop);
    // fullscreen at another size or refresh rate switches the screen's mode; the desktop's own mode is no change,
    // switching to it anyway blanks the screen for nothing
    DWORD fw = s.width && s.height ? s.width : desktop.dmPelsWidth, fh = s.width && s.height ? s.height : desktop.dmPelsHeight;
    bool other_size = fw != desktop.dmPelsWidth || fh != desktop.dmPelsHeight;
    DWORD hz = s.refresh ? s.refresh : other_size ? BestRefresh(mi.szDevice, fw, fh) : 0;
    bool other_rate = hz && hz != desktop.dmDisplayFrequency;
    if (s.display_mode == DisplayMode::Fullscreen && (other_size || other_rate)) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        dm.dmPelsWidth = fw;
        dm.dmPelsHeight = fh;
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        if (hz) {
            dm.dmDisplayFrequency = hz;
            dm.dmFields |= DM_DISPLAYFREQUENCY;
        }
        LONG r = ChangeDisplaySettingsExW(mi.szDevice, &dm, nullptr, CDS_FULLSCREEN, nullptr);
        if (r != DISP_CHANGE_SUCCESSFUL && hz) {  // a rate the screen does not take: the size alone
            dm.dmFields &= ~DWORD(DM_DISPLAYFREQUENCY);
            r = ChangeDisplaySettingsExW(mi.szDevice, &dm, nullptr, CDS_FULLSCREEN, nullptr);
        }
        g_mode_changed = r == DISP_CHANGE_SUCCESSFUL;
        LOGR("display: %ls %lux%lu at %lu Hz%s", mi.szDevice, fw, fh, hz, g_mode_changed ? "" : " failed, the desktop mode kept");
        GetMonitorInfoW(mon, &mi);
    }
    if (s.display_mode == DisplayMode::Windowed) {
        RECT r{0, 0, LONG(out.width), LONG(out.height)};
        // the frame at the window's own scaling (150%, 200%), so the picture inside is the size asked for
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(g_hwnd));
        int w = r.right - r.left, h = r.bottom - r.top;
        const RECT& a = mi.rcWork;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | visible);
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, a.left + (a.right - a.left - w) / 2, a.top + (a.bottom - a.top - h) / 2, w, h,
                     SWP_FRAMECHANGED);
    } else {
        const RECT& m = mi.rcMonitor;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | visible);
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    {
        std::lock_guard lock(UiLock());
        if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    }
    if (intptr_t result = 0; OverlayMessage(hwnd, msg, wp, lp, &result)) return result;
    if (intptr_t result = 0; kGame.message && kGame.message(hwnd, msg, wp, lp, &result)) return result;
    switch (msg) {
        case WM_KEYDOWN:
            // F3: a frame capture and a thread dump, for testing only (XBE_CAPTURE_DIR set, capture.bat): a player who
            // presses it by accident would otherwise find alarming "error:" lines in the log
            if (wp == VK_F3 && !diag::CaptureDir().empty()) DumpThreads();
            // \, the key above Enter on a US keyboard (VK_OEM_5): a screenshot, once a press
            if (wp == VK_OEM_5 && !(lp & (1 << 30))) gpu::g_screenshot = true;
            // F8: texture packs off and on, to compare; F7: a pair of screenshots, with and without them
            if (wp == VK_F8 && !(lp & (1 << 30))) {
                bool off = !gpu::g_pack_off;
                gpu::g_pack_off = off;
                LOGR("texture packs %s (F8)", off ? "off" : "on");
            }
            if (wp == VK_F7 && !(lp & (1 << 30))) gpu::g_pack_pair = true;
            // ] (VK_OEM_6): the next colour look, as ROTK Fix
            if (wp == VK_OEM_6 && !(lp & (1 << 30))) {
                static const char* const kNames[] = {"original", "film", "vivid", "dark fantasy"};
                uint32_t n = (gpu::g_colour_look + 1) % gpu::kColourLooks;
                gpu::g_colour_look = n;
                LOGR("colour look %s (])", kNames[n]);
            }
            // [ (VK_OEM_4): sharpening off and on, to compare, as ROTK Fix
            if (wp == VK_OEM_4 && !(lp & (1 << 30)) && Get().sharpening) {
                bool off = !gpu::g_sharpen_off;
                gpu::g_sharpen_off = off;
                LOGR("sharpening %s ([)", off ? "off" : "on");
            }
            return 0;
        case WM_ACTIVATE:
            SetFocus(LOWORD(wp) != WA_INACTIVE);
            if (LOWORD(wp) == WA_INACTIVE) {
                // a changed display mode steps aside for the desktop, minimized, and comes back on return; not while
                // handing over to the next instance, which runs in the same mode
                if (g_mode_changed && !kernel::g_relaunching) {
                    MONITORINFOEXW mi{};
                    mi.cbSize = sizeof(mi);
                    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
                    ChangeDisplaySettingsExW(mi.szDevice, nullptr, nullptr, 0, nullptr);
                    g_mode_changed = false;
                    ShowWindow(hwnd, SW_MINIMIZE);
                }
            } else if (Get().display_mode == DisplayMode::Fullscreen && Get().width && Get().height && !g_mode_changed &&
                       IsWindowVisible(hwnd) && !g_apply_posted) {
                // once: restoring the window activates it again from inside the apply, and a second apply switched
                // the mode back and forth, after which presents were not taken for a while (the game stood still
                // until the next alt-tab)
                g_apply_posted = true;
                PostMessageW(hwnd, kApplyDisplay, 0, 0);
            }
            break;
        case kApplyDisplay:
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            ApplyNow();
            g_apply_posted = false;
            return 0;
        case kOpenWindow:
            CloseSplash();
            if (wp) ApplyNow();
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

std::filesystem::path ExeDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return std::filesystem::path(p).parent_path();
}

const CommandLine& Args() {
    static const CommandLine c = [] {
        CommandLine c;
        int n = 0;
        wchar_t** v = CommandLineToArgvW(GetCommandLineW(), &n);
        for (int i = 1; v && i < n; i++) {
            std::wstring a = v[i];
            auto next = [&]() -> std::filesystem::path { return i + 1 < n ? std::filesystem::path(v[++i]) : std::filesystem::path(); };
            if (a == L"--game") c.game = next();
            else if (a == L"--config") c.config = next();
            else if (a == L"--textures") c.textures = next();
            else if (a == L"--log") c.log = next();
            else if (a == L"--saves") c.saves = next();
            else if (a == L"--shader-cache") c.shader_cache = next();
            else if (a == L"--screenshots") c.screenshots = next();
            else if (a == L"--instance") c.instance = std::max(0, std::min(8, _wtoi(next().c_str())));
            else if (a == L"--no-dialogs") c.no_dialogs = true;
            else if (a == L"--any-release") c.any_release = true;
            else if (a == L"--version") c.version = true;
            else if (a == L"--describe-settings") c.describe = true;
        }
        if (v) LocalFree(v);
        // relative paths are taken from where the launcher started us, before anything changes the directory
        std::error_code ec;
        for (auto* p : {&c.game, &c.config, &c.textures, &c.log, &c.saves, &c.shader_cache, &c.screenshots})
            if (!p->empty()) *p = std::filesystem::absolute(*p, ec);
        return c;
    }();
    return c;
}

int Instance() { return Args().instance; }

std::filesystem::path TextureDir() { return Args().textures.empty() ? ExeDir() / "textures" : Args().textures; }

std::filesystem::path LogFolder() { return Args().log.empty() ? ExeDir() / "logs" : Args().log.parent_path(); }

std::filesystem::path SavesDir() {
    // started on its own: the saves Athelas keeps beside the exe (saves) when it has played the game, else hdd
    std::error_code ec;
    std::filesystem::path base = !Args().saves.empty()                              ? Args().saves
                                 : std::filesystem::is_directory(ExeDir() / "saves", ec) ? ExeDir() / "saves"
                                                                                      : ExeDir() / "hdd";
    // a second copy for system link tests keeps a disk of its own
    if (Instance() > 1) base += std::to_wstring(Instance());
    return base;
}

std::filesystem::path ScreenshotDir() { return Args().screenshots.empty() ? ExeDir() / "Screenshots" : Args().screenshots; }

std::filesystem::path ShaderCacheDir() {
    return Args().shader_cache.empty() ? ExeDir() / "shadercache" : Args().shader_cache;
}

void Fail(int code, const std::wstring& message) {
    std::string narrow;
    int n = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), int(message.size()), nullptr, 0, nullptr, nullptr);
    narrow.resize(size_t(std::max(n, 0)));
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, message.c_str(), int(message.size()), narrow.data(), n, nullptr, nullptr);
    LOGE("Exit (%d): %s", code, narrow.c_str());
    logx::Flush();
    if (!Args().no_dialogs) MessageBoxW(nullptr, message.c_str(), kGame.short_title, MB_ICONERROR);
    // as kernel::Shutdown: NVIDIA's Streamline crashes in its own clean-up at exit, so with it loaded no DLL clean-up
    if (GetModuleHandleW(L"sl.interposer.dll")) TerminateProcess(GetCurrentProcess(), UINT(code));
    ExitProcess(UINT(code));
}

void* Window() { return g_hwnd; }

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

void ApplyDisplay() { PostMessageW(g_hwnd, kApplyDisplay, 0, 0); }
void OpenGameWindow(bool apply) { SendMessageW(g_hwnd, kOpenWindow, apply, 0); }

void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t cap = Get().fps_cap;
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    // Windows 10 1803 and later: a high resolution timer wakes within about half a millisecond, so only the last
    // quarter millisecond is spun (at 240 fps the 1 ms sleeps left up to 2 ms of each 4 ms frame spinning a core)
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer) {
        for (;;) {
            auto left = next - clock::now() - std::chrono::microseconds(250);
            if (left <= std::chrono::microseconds(100)) break;
            LARGE_INTEGER due;
            due.QuadPart = -std::max<int64_t>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(left).count() / 100);
            if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) || WaitForSingleObject(timer, 50) != WAIT_OBJECT_0)
                break;
        }
    }
    while (clock::now() + std::chrono::milliseconds(2) < next) Sleep(1);
    while (clock::now() < next) std::this_thread::yield();
}

}  // namespace app

namespace {

// a line for whoever started us: a launcher's pipe, or the console it was typed in
void Print(const std::string& text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) out = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    if (!out || out == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(out, text.data(), DWORD(text.size()), &written, nullptr);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    // a launcher's questions, answered before anything else starts
    if (app::Args().version) {
        Print(std::string(app::kGame.version) + "\n");
        return app::kExitOk;
    }
    if (app::Args().describe) {
        Print(cfg::Describe(app::SettingsList(), app::kGame.presets));
        return app::kExitOk;
    }
    SetUnhandledExceptionFilter(app::WriteCrashDump);
    std::signal(SIGABRT, [](int) { app::WriteCrashDump(nullptr); });
    timeBeginPeriod(1);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // one log a session: a relaunch into another image carries on its predecessor's
    bool relaunched = std::wcsstr(GetCommandLineW(), L"--relaunched") != nullptr;
    std::string log = app::kGame.log;
    if (app::Instance() > 1) log += "-" + std::to_string(app::Instance());
    if (!app::Args().log.empty()) log = app::Args().log.stem().string();
    logx::Init(app::LogDir().string(), log, relaunched);
    app::Load();
    if (app::kGame.init) app::kGame.init();
    if (relaunched && !logx::kVerbose) LOGR("Relaunched: %s", IMAGE_XBE);
    else app::LogSystem();

    app::GameFiles game;
    if (!app::FindGame(game)) return app::kExitNoGame;

    bool splash = app::OpenSplash(), handover = app::StartHandover();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = app::WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = app::kGame.name;
    RegisterClassExW(&wc);
    app::g_hwnd = CreateWindowExW(0, wc.lpszClassName, app::kGame.title, WS_POPUP, 0, 0, 1280, 720,
                                  nullptr, nullptr, inst, nullptr);
    // behind a splash the window stays hidden until the game's first frame; handed over, it is set up hidden
    if (handover) {
        app::ApplyNow();
    } else if (!splash) {
        app::ApplyNow();
        ShowWindow(app::g_hwnd, SW_SHOW);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplWin32_Init(app::g_hwnd);
    app::OverlayInit();

    input::Init();
    ach::Start();
    discord::Start();
    if (app::kGame.window) app::kGame.window(app::g_hwnd);
    if (app::Instance())
        SetWindowTextW(app::g_hwnd, (std::wstring(app::kGame.title) + L" [" + std::to_wstring(app::Instance()) + L"]").c_str());
    if (app::Get().one_core) {
        uint64_t m = app::OneCoreMask();
        kernel::SetGuestAffinity(m);
        LOGR("system: the game's threads on one core (mask %llX)", (unsigned long long)m);
    }
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), app::SavesDir().string())) {
        MessageBoxW(app::g_hwnd, L"The game failed to start, see the log in the logs folder.", app::kGame.short_title,
                    MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (app::g_mode_changed) ChangeDisplaySettingsW(nullptr, 0);
    ach::Flush();
    logx::Flush();
    kernel::Shutdown(0);
}
