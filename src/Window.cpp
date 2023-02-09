#include "Window.h"

namespace zp {

static const wchar_t* kClassName = L"ZPilotWindow";

// Opt in at the highest level the OS offers, so the backbuffer is native pixels
// instead of a stretched 96 DPI bitmap.
static void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetCtxFn = BOOL(WINAPI*)(HANDLE);
        auto setCtx = (SetCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        const HANDLE kPerMonitorV2 = (HANDLE)-4;
        if (setCtx && setCtx(kPerMonitorV2)) return;
    }
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        using SetAwarenessFn = HRESULT(WINAPI*)(int);
        auto setAwareness = (SetAwarenessFn)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (setAwareness && SUCCEEDED(setAwareness(2))) return;  // PROCESS_PER_MONITOR_DPI_AWARE
    }
    SetProcessDPIAware();
}

static UINT SystemDpi() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using GetDpiFn = UINT(WINAPI*)();
        if (auto get = (GetDpiFn)GetProcAddress(user32, "GetDpiForSystem")) return get();
    }
    HDC dc = GetDC(nullptr);
    UINT dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}

static void AdjustForDpi(RECT* rc, DWORD style, UINT dpi) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using AdjustFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        if (auto adjust = (AdjustFn)GetProcAddress(user32, "AdjustWindowRectExForDpi")) {
            adjust(rc, style, FALSE, 0, dpi);
            return;
        }
    }
    AdjustWindowRect(rc, style, FALSE);
}

bool Window::Create(const wchar_t* title, int w, int h) {
    EnableDpiAwareness();

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = &Window::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    // The requested size is in 96 DPI units; grow it to physical pixels, then
    // shrink to fit the work area if the monitor cannot hold it.
    UINT dpi = SystemDpi();
    float scale = dpi / 96.0f;
    RECT work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    float fit = std::min(1.0f, std::min((work.right - work.left - 40) / (w * scale),
                                        (work.bottom - work.top - 80) / (h * scale)));
    width_ = std::max(320, (int)(w * scale * fit));
    height_ = std::max(240, (int)(h * scale * fit));

    RECT rc = {0, 0, width_, height_};
    AdjustForDpi(&rc, WS_OVERLAPPEDWINDOW, dpi);

    hwnd_ = CreateWindowExW(0, kClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr,
                            wc.hInstance, this);
    if (!hwnd_) return false;

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void Window::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Window::PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) quit_ = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !quit_;
}

bool Window::SizeChanged() {
    bool c = sizeChanged_;
    sizeChanged_ = false;
    return c;
}

void Window::NewFrame() {
    memcpy(prevDown_, down_, sizeof(down_));
}

void Window::ToggleFullscreen() {
    DWORD style = GetWindowLong(hwnd_, GWL_STYLE);
    if (!fullscreen_) {
        MONITORINFO mi = {sizeof(MONITORINFO)};
        if (GetWindowPlacement(hwnd_, &placement_) &&
            GetMonitorInfo(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            fullscreen_ = true;
        }
    } else {
        SetWindowLong(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd_, &placement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen_ = false;
    }
}

void Window::SetVirtualKey(WPARAM vk, bool down) {
    switch (vk) {
        case VK_LEFT: case 'A': down_[KEY_LEFT] = down; break;
        case VK_RIGHT: case 'D': down_[KEY_RIGHT] = down; break;
        case VK_UP: case 'W': down_[KEY_UP] = down; break;
        case VK_DOWN: case 'S': down_[KEY_DOWN] = down; break;
        case VK_SPACE: down_[KEY_FIRE] = down; break;
        case VK_SHIFT: case 'E': down_[KEY_TRACTOR] = down; break;
        case VK_CONTROL: case 'Q': down_[KEY_SHIELD] = down; break;
        case VK_TAB: case 'F': down_[KEY_SLOW] = down; break;
        case VK_RETURN: down_[KEY_START] = down; break;
        case 'P': down_[KEY_PAUSE] = down; break;
        case 'R': down_[KEY_RESTART] = down; break;
        case VK_ESCAPE: down_[KEY_QUIT] = down; break;
        case VK_F1: down_[KEY_HELP] = down; break;
        case VK_F3: down_[KEY_MUTE] = down; break;
        case VK_F11: down_[KEY_FULLSCREEN] = down; break;
        case VK_F12: down_[KEY_SHOT] = down; break;
        default: break;
    }
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Window* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    } else if (self) {
        switch (msg) {
            case WM_DESTROY:
                self->quit_ = true;
                PostQuitMessage(0);
                return 0;
            case WM_SIZE:
                if (wp != SIZE_MINIMIZED) {
                    self->width_ = LOWORD(lp);
                    self->height_ = HIWORD(lp);
                    self->sizeChanged_ = true;
                }
                return 0;
            case WM_DPICHANGED: {
                const RECT* r = reinterpret_cast<const RECT*>(lp);
                SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left,
                             r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                self->SetVirtualKey(wp, true);
                return 0;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                self->SetVirtualKey(wp, false);
                return 0;
            case WM_KILLFOCUS:
                memset(self->down_, 0, sizeof(self->down_));
                return 0;
            case WM_LBUTTONDOWN: self->down_[KEY_FIRE] = true; return 0;
            case WM_LBUTTONUP: self->down_[KEY_FIRE] = false; return 0;
            case WM_RBUTTONDOWN: self->down_[KEY_TRACTOR] = true; return 0;
            case WM_RBUTTONUP: self->down_[KEY_TRACTOR] = false; return 0;
            default: break;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace zp
