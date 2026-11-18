// Online co-op: a small window while the two PCs connect (xdk/netplay.h), before the game's own window opens: what it
// waits for, the address to give the friend, and Cancel. Drawn in the launcher's look: dark brown, a gold frame and
// heading, the in-game menu's faces (Cinzel Decorative, IM Fell English; menu_look.cpp), a dark title bar.
#ifndef NOMINMAX
#define NOMINMAX  // std::max below, not windows.h's macro
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>

#include "app/app.h"

namespace app::look {
// menu_look.cpp
void FontData(const unsigned char*& head, size_t& head_size, const unsigned char*& body, size_t& body_size,
              const unsigned char*& digits, size_t& digits_size);
}

namespace app {

namespace {

constexpr UINT kClose = WM_APP + 1;  // closed by the game (connected), not by the player
// the launcher's colours (launcher/src/app.h, ui.cpp)
constexpr COLORREF kBack = RGB(24, 19, 14), kText = RGB(234, 223, 200), kGold = RGB(206, 166, 78),
                   kGoldLight = RGB(232, 196, 112), kButton = RGB(44, 35, 25), kButtonHot = RGB(70, 55, 34),
                   kLine = RGB(120, 95, 50);
constexpr int kWidth = 600, kPad = 24, kHeadH = 40, kButtonW = 120, kButtonH = 34;

HWND g_wait = nullptr;
HFONT g_head = nullptr, g_body = nullptr, g_digits = nullptr, g_button_font = nullptr;
HBRUSH g_back = nullptr;
std::wstring g_textw;  // the body text shown (guarded by g_m)
int g_hot = -1, g_down = -1;  // the button under the mouse / pressed: 0 Cancel, 1 Copy address, -1 none
std::wstring g_address;       // the first address:port in the text, for Copy (empty: no Copy button)
std::atomic<bool> g_copied{false};  // Copy pressed: its label says so until the text changes
std::atomic<bool> g_cancel{false};
bool g_failed = false;  // the window couldn't be made
std::mutex g_m;
std::condition_variable g_cv;

std::wstring Wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

// "Online co-op: waiting for ..." -> "Waiting for ..." (the heading already says Online co-op)
std::wstring Body(const std::string& text) {
    std::wstring w = Wide(text);
    const std::wstring prefix = L"Online co-op: ";
    if (w.rfind(prefix, 0) == 0) {
        w.erase(0, prefix.size());
        if (!w.empty()) w[0] = wchar_t(towupper(w[0]));
    }
    return w;
}

RECT ButtonRect(const RECT& client, int which = 0) {
    // Cancel at the right; Copy address left of it, wider
    if (which == 1)
        return {client.right - kPad - kButtonW - 12 - 170, client.bottom - kPad - kButtonH, client.right - kPad - kButtonW - 12,
                client.bottom - kPad};
    return {client.right - kPad - kButtonW, client.bottom - kPad - kButtonH, client.right - kPad, client.bottom - kPad};
}

// the first "a.b.c.d:port" in the text (the address player 1 sends their friend), or empty; not the home-network one
// (its own paragraph, for a friend in the same house)
std::wstring FindAddress(const std::wstring& text) {
    const std::wstring t = text.substr(0, text.find(L"A friend on the same home network"));
    for (size_t i = 0; i < t.size(); i++) {
        if (!iswdigit(t[i]) || (i > 0 && (iswdigit(t[i - 1]) || t[i - 1] == L'.'))) continue;
        size_t j = i;
        int dots = 0;
        while (j < t.size() && (iswdigit(t[j]) || t[j] == L'.')) dots += t[j++] == L'.';
        if (dots != 3 || j >= t.size() || t[j] != L':' || j + 1 >= t.size() || !iswdigit(t[j + 1])) continue;
        size_t k = j + 1;
        while (k < t.size() && iswdigit(t[k])) k++;
        return t.substr(i, k - i);
    }
    return {};
}

void CopyText(HWND h, const std::wstring& w) {
    if (!OpenClipboard(h)) return;
    EmptyClipboard();
    if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
        if (void* p = GlobalLock(g)) {
            std::memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(g);
            if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
        } else {
            GlobalFree(g);
        }
    }
    CloseClipboard();
}

bool Digit(wchar_t c) { return c >= L'0' && c <= L'9'; }

// one word's width: its digits in g_digits, the rest in g_body
int WordWidth(HDC dc, const std::wstring& w) {
    int x = 0;
    for (size_t i = 0; i < w.size();) {
        size_t j = i;
        bool d = Digit(w[i]);
        while (j < w.size() && Digit(w[j]) == d) j++;
        SelectObject(dc, d ? g_digits : g_body);
        SIZE sz{};
        GetTextExtentPoint32W(dc, w.c_str() + i, int(j - i), &sz);
        x += sz.cx;
        i = j;
    }
    return x;
}

// the text wrapped to the width, digits in plain figures; drawn when draw is set; returns its height
int Text(HDC dc, const std::wstring& text, int left, int top, int width, bool draw) {
    HGDIOBJ old = SelectObject(dc, g_body);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    const int line_h = tm.tmHeight + tm.tmExternalLeading + 2;
    SIZE space{};
    GetTextExtentPoint32W(dc, L" ", 1, &space);
    int x = 0, y = 0;
    const UINT align = SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    auto put = [&](const std::wstring& w) {
        int ww = WordWidth(dc, w);
        if (x > 0 && x + ww > width) x = 0, y += line_h;  // wrap before a word that doesn't fit
        if (draw) {
            int cx = left + x;
            for (size_t i = 0; i < w.size();) {
                size_t j = i;
                bool d = Digit(w[i]);
                while (j < w.size() && Digit(w[j]) == d) j++;
                SelectObject(dc, d ? g_digits : g_body);
                TextOutW(dc, cx, top + y + tm.tmAscent, w.c_str() + i, int(j - i));  // on one baseline
                SIZE sz{};
                GetTextExtentPoint32W(dc, w.c_str() + i, int(j - i), &sz);
                cx += sz.cx;
                i = j;
            }
        }
        x += ww + space.cx;
    };
    std::wstring word;
    for (wchar_t c : text) {
        if (c == L' ' || c == L'\n') {
            if (!word.empty()) put(word), word.clear();
            if (c == L'\n') x = 0, y += line_h;
        } else if (c != L'\r') {
            word += c;
        }
    }
    if (!word.empty()) put(word);
    SetTextAlign(dc, align);
    SelectObject(dc, old);
    return y + line_h;
}

int TextHeight(HDC dc, const std::wstring& w) { return Text(dc, w, 0, 0, kWidth - 2 * kPad, false); }

void Line(HDC dc, int x0, int y, int x1, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x0, y, nullptr);
    LineTo(dc, x1, y);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void Frame(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FrameRect(dc, &r, b);
    DeleteObject(b);
}

void Paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    // drawn off screen, then copied: no flicker when the text changes
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
    HGDIOBJ old_bmp = SelectObject(dc, bmp);
    FillRect(dc, &rc, g_back);
    SetBkMode(dc, TRANSPARENT);
    // the frame: gold, and a fainter one inside it
    Frame(dc, {6, 6, rc.right - 6, rc.bottom - 6}, kGold);
    Frame(dc, {10, 10, rc.right - 10, rc.bottom - 10}, kLine);
    // the heading and its rule
    HGDIOBJ old_font = SelectObject(dc, g_head);
    SetTextColor(dc, kGold);
    RECT head{kPad, kPad - 4, rc.right - kPad, kPad - 4 + kHeadH};
    DrawTextW(dc, L"Online Co-op", -1, &head, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    Line(dc, kPad, kPad + kHeadH, rc.right - kPad, kLine);
    // the text
    std::wstring text;
    {
        std::lock_guard<std::mutex> l(g_m);
        text = g_textw;
    }
    SetTextColor(dc, kText);
    Text(dc, text, kPad, kPad + kHeadH + 14, rc.right - 2 * kPad, true);
    // the buttons: the launcher's, gold edged (Copy address only when the text has one)
    bool copy;
    {
        std::lock_guard<std::mutex> l(g_m);
        copy = !g_address.empty();
    }
    SelectObject(dc, g_button_font);
    for (int i = copy ? 1 : 0; i >= 0; i--) {
        RECT b = ButtonRect(rc, i);
        HBRUSH fill = CreateSolidBrush(g_down == i ? kLine : g_hot == i ? kButtonHot : kButton);
        FillRect(dc, &b, fill);
        DeleteObject(fill);
        Frame(dc, b, g_hot == i ? kGoldLight : kGold);
        SetTextColor(dc, g_hot == i ? kGoldLight : kText);
        const wchar_t* label = i == 0 ? L"Cancel" : g_copied ? L"Copied" : L"Copy address";
        DrawTextW(dc, label, -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, old_font);
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(h, &ps);
}

// the button at the mouse: 0 Cancel, 1 Copy address (when shown), -1 none
int OnButton(HWND h, LPARAM lp) {
    RECT rc;
    GetClientRect(h, &rc);
    POINT p{short(LOWORD(lp)), short(HIWORD(lp))};
    bool copy;
    {
        std::lock_guard<std::mutex> l(g_m);
        copy = !g_address.empty();
    }
    for (int i = 0; i <= (copy ? 1 : 0); i++) {
        RECT b = ButtonRect(rc, i);
        if (PtInRect(&b, p)) return i;
    }
    return -1;
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;  // Paint fills it
        case WM_PAINT:
            Paint(h);
            return 0;
        case WM_MOUSEMOVE: {
            int hot = OnButton(h, lp);
            if (hot != g_hot) {
                g_hot = hot;
                InvalidateRect(h, nullptr, FALSE);
            }
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            return 0;
        }
        case WM_MOUSELEAVE:
            g_hot = g_down = -1;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            if (OnButton(h, lp) >= 0) {
                g_down = OnButton(h, lp);
                SetCapture(h);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP: {
            const int was = g_down;
            g_down = -1;
            ReleaseCapture();
            InvalidateRect(h, nullptr, FALSE);
            if (was >= 0 && OnButton(h, lp) == was) {
                if (was == 0) {
                    g_cancel = true;
                    DestroyWindow(h);
                } else {
                    std::wstring a;
                    {
                        std::lock_guard<std::mutex> l(g_m);
                        a = g_address;
                    }
                    CopyText(h, a);
                    g_copied = true;
                }
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {  // Ctrl+C: the address too
                std::wstring a;
                {
                    std::lock_guard<std::mutex> l(g_m);
                    a = g_address;
                }
                if (!a.empty()) {
                    CopyText(h, a);
                    g_copied = true;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            if (wp == VK_ESCAPE) {
                g_cancel = true;
                DestroyWindow(h);
            }
            return 0;
        case WM_CLOSE:  // the window's X: the same as Cancel
            g_cancel = true;
            DestroyWindow(h);
            return 0;
        case kClose:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// the faces from the game's own data (no install), by name once added; Georgia when they can't be
HFONT MakeFont(const wchar_t* face, int height, int weight) {
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

void LoadFonts() {
    static bool done = false;
    if (done) return;  // once: the window may open again (a message after the waiting window closed)
    done = true;
    const unsigned char *head, *body, *digits;
    size_t head_n, body_n, digits_n;
    look::FontData(head, head_n, body, body_n, digits, digits_n);
    DWORD n = 0;
    bool ok_head = AddFontMemResourceEx(const_cast<unsigned char*>(head), DWORD(head_n), nullptr, &n) != nullptr;
    bool ok_body = AddFontMemResourceEx(const_cast<unsigned char*>(body), DWORD(body_n), nullptr, &n) != nullptr;
    g_head = MakeFont(ok_head ? L"Cinzel Decorative" : L"Georgia", 26, FW_NORMAL);
    bool ok_digits = AddFontMemResourceEx(const_cast<unsigned char*>(digits), DWORD(digits_n), nullptr, &n) != nullptr;
    g_body = MakeFont(ok_body ? L"IM FELL English" : L"Georgia", 19, FW_NORMAL);
    // the digits' face a touch larger: EB Garamond's figures are smaller than IM Fell's letters at one size
    g_digits = ok_body && ok_digits ? MakeFont(L"EB Garamond", 20, FW_NORMAL) : MakeFont(L"Georgia", 19, FW_NORMAL);
    g_button_font = MakeFont(ok_body ? L"IM FELL English" : L"Georgia", 19, FW_NORMAL);
}

// the title bar dark as well (Windows 10 2004 and later; older ones keep theirs)
void DarkTitle(HWND h) {
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        using Set = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        if (auto set = reinterpret_cast<Set>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")))) {
            BOOL on = TRUE;
            set(h, 20, &on, sizeof(on));  // DWMWA_USE_IMMERSIVE_DARK_MODE
            COLORREF cap = kBack, txt = kGold;
            set(h, 35, &cap, sizeof(cap));  // DWMWA_CAPTION_COLOR (Windows 11)
            set(h, 36, &txt, sizeof(txt));  // DWMWA_TEXT_COLOR
        }
    }
}

// the window's outer size for a client area this tall, centred on the screen
void Place(HWND h, int client_h) {
    RECT win{0, 0, kWidth, client_h};
    AdjustWindowRectEx(&win, WS_CAPTION | WS_SYSMENU, FALSE, 0);
    const int ww = win.right - win.left, wh = win.bottom - win.top;
    SetWindowPos(h, nullptr, (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

int ClientHeight(int text_h) { return kPad + kHeadH + 14 + (std::max)(text_h, 24) + 18 + kButtonH + kPad; }

void Run() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    LoadFonts();
    if (!g_back) g_back = CreateSolidBrush(kBack);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.hbrBackground = g_back;
    wc.lpszClassName = L"AthelasOnlineCoop";
    RegisterClassExW(&wc);
    HWND win = CreateWindowExW(0, wc.lpszClassName, kGame.title, WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                               kWidth, 200, nullptr, nullptr, inst, nullptr);
    if (!win) {  // no window: the connection goes on without one (the log says what happens)
        std::lock_guard<std::mutex> l(g_m);
        g_failed = true;
        g_cv.notify_all();
        return;
    }
    DarkTitle(win);
    Place(win, ClientHeight(0));
    ShowWindow(win, SW_SHOW);
    SetForegroundWindow(win);
    {
        std::lock_guard<std::mutex> l(g_m);
        g_wait = win;
    }
    g_cv.notify_all();
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    std::lock_guard<std::mutex> l(g_m);
    g_wait = nullptr;
    // the fonts and brush stay for the life of the game: the game thread may still be measuring text with them (a few
    // handles, and the window can open again for a message)
}

}  // namespace

void NetplayStatus(const std::string& text) {
    std::unique_lock<std::mutex> l(g_m);
    if (text.empty()) {
        if (g_wait) PostMessageW(g_wait, kClose, 0, 0);
        return;
    }
    if (g_cancel || g_failed) return;  // cancelled (the game is ending) or no window possible
    if (!g_wait) {
        std::thread(Run).detach();
        g_cv.wait(l, [] { return g_wait != nullptr || g_failed; });
        if (!g_wait) return;
    }
    g_textw = Body(text);
    // only player 1's waiting window has an address to send (an error's example address isn't one)
    std::wstring address = g_textw.rfind(L"Waiting for player 2", 0) == 0 ? FindAddress(g_textw) : std::wstring();
    if (address != g_address) g_copied = false;
    g_address = address;
    std::wstring w = g_textw;
    HWND h = g_wait;
    l.unlock();
    // as tall as the text needs (the host's has several paragraphs), then drawn again
    int text_h = 0;
    if (HDC dc = GetDC(h)) {
        text_h = TextHeight(dc, w);
        ReleaseDC(h, dc);
    }
    Place(h, ClientHeight(text_h));
    InvalidateRect(h, nullptr, FALSE);
}

bool NetplayCancelled() { return g_cancel.load(); }

// no window could be made: the connection goes on without one, and a message that waits for Cancel doesn't wait
bool NetplayWindowless() {
    std::lock_guard<std::mutex> l(g_m);
    return g_failed;
}

}  // namespace app
