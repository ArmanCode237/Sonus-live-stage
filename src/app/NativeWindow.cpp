#include "app/NativeWindow.h"

#if defined(_WIN32) && defined(STAGE_NATIVE_WINDOW)
#include "webview/webview.h"
#include <windows.h>

namespace {
WNDPROC g_previousProc = nullptr;
NativeWindowState* g_state = nullptr;
bool g_fullscreen = false;
WINDOWPLACEMENT g_beforeFullscreen{sizeof(WINDOWPLACEMENT)};
LONG_PTR g_styleBeforeFullscreen = 0;

void saveState(HWND hwnd) {
    if (!g_state) return;
    g_state->fullscreen = g_fullscreen;
    WINDOWPLACEMENT wp{sizeof(WINDOWPLACEMENT)};
    if (g_fullscreen) wp = g_beforeFullscreen;
    else if (!GetWindowPlacement(hwnd, &wp)) return;
    g_state->maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    const RECT& r = wp.rcNormalPosition;
    g_state->x = r.left; g_state->y = r.top;
    g_state->width = r.right - r.left; g_state->height = r.bottom - r.top;
}

void setFullscreen(HWND hwnd, bool on) {
    if (on == g_fullscreen) return;
    if (on) {
        GetWindowPlacement(hwnd, &g_beforeFullscreen);
        g_styleBeforeFullscreen = GetWindowLongPtrW(hwnd, GWL_STYLE);
        MONITORINFO mi{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(hwnd, GWL_STYLE, g_styleBeforeFullscreen & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, g_styleBeforeFullscreen);
        SetWindowPlacement(hwnd, &g_beforeFullscreen);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    g_fullscreen = on;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) {
        // On stage, a stray click on X (or Alt+F4) must not silence the show.
        const int answer = MessageBoxW(hwnd,
            L"¿Cerrar Sonus Live Stage?\n\nEl audio se detendrá. La sesión se guarda automáticamente.",
            L"Sonus Live Stage", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (answer != IDYES) return 0;
        saveState(hwnd);
    }
    if (msg == WM_GETMINMAXINFO) {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize.x = 1024; info->ptMinTrackSize.y = 640;
    }
    return CallWindowProcW(g_previousProc, hwnd, msg, wp, lp);
}
} // namespace

bool nativeWindowAvailable() { return true; }

bool runNativeWindow(const std::string& url, NativeWindowState& state, std::string& error) {
    try {
        webview::webview w(false, nullptr);
        w.set_title("Sonus Live Stage");
        auto handle = w.window();
        if (!handle.ok()) { error = "WebView2: no window"; return false; }
        HWND hwnd = static_cast<HWND>(handle.value());
        g_state = &state;
        g_previousProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(windowProc)));
        if (HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1))) {
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        }
        // Last placement (normal rectangle), then maximised / full screen as it was.
        WINDOWPLACEMENT wp{sizeof(WINDOWPLACEMENT)};
        GetWindowPlacement(hwnd, &wp);
        if (state.width >= 800 && state.height >= 500) {
            RECT r{state.x, state.y, state.x + state.width, state.y + state.height};
            if (state.x < 0 || state.y < 0) {
                const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
                r = RECT{(sw - state.width) / 2, (sh - state.height) / 2, (sw + state.width) / 2, (sh + state.height) / 2};
            }
            wp.rcNormalPosition = r;
        }
        wp.showCmd = state.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        SetWindowPlacement(hwnd, &wp);
        if (state.fullscreen) setFullscreen(hwnd, true);
        // F11: full screen (stage mode). The page asks C++ through a bound function.
        w.bind("__liveStageFullscreen", [hwnd](const std::string&) -> std::string {
            setFullscreen(hwnd, !g_fullscreen);
            return g_fullscreen ? "true" : "false";
        });
        w.init("window.addEventListener('keydown',function(e){if(e.key==='F11'){e.preventDefault();"
               "window.__liveStageFullscreen&&window.__liveStageFullscreen();}},true);"
               "window.__liveStageNative=true;");
        w.navigate(url);
        w.run();
        g_state = nullptr;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

#elif defined(__APPLE__) && defined(STAGE_NATIVE_WINDOW)
#include "webview/webview.h"
#include <algorithm>
#include <exception>

bool nativeWindowAvailable() { return true; }
bool runNativeWindow(const std::string& url, NativeWindowState& state, std::string& error) {
    try {
        // Cocoa/WKWebView: the DSP continues independently on the audio callback.
        webview::webview window(false, nullptr);
        window.set_title("Sonus Live Stage");
        // MacBook Pro M1 13" often has a 1280x800 logical display. Start inside
        // the visible desktop rather than using the Windows default of 1440x900.
        // The user can resize normally, including on larger external monitors.
        window.set_size(std::clamp(state.width, 960, 1180),
                        std::clamp(state.height, 640, 740), WEBVIEW_HINT_NONE);
        window.navigate(url);
        window.run();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

#else
bool nativeWindowAvailable() { return false; }
bool runNativeWindow(const std::string&, NativeWindowState&, std::string& error) {
    error = "Ventana nativa no disponible en esta compilación";
    return false;
}
#endif
