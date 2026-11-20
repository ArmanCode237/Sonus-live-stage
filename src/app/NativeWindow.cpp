#include "app/NativeWindow.h"

#if defined(_WIN32) && defined(STAGE_NATIVE_WINDOW)
#include "webview/webview.h"
#include <windows.h>
#include <shobjidl.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>

namespace {
WNDPROC g_previousProc = nullptr;
NativeWindowState* g_state = nullptr;
bool g_fullscreen = false;
WINDOWPLACEMENT g_beforeFullscreen{sizeof(WINDOWPLACEMENT)};
LONG_PTR g_styleBeforeFullscreen = 0;

// Native Windows picker: returns a real, UTF-8 absolute path to the C++ host.
// The dialog is opened from the WebView2 UI binding, not from the audio thread.
std::string selectLocalSamplePath(bool folder, HWND parent) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED(init);
    IFileOpenDialog* dialog = nullptr;
    std::string selected;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options)))
            dialog->SetOptions(options | FOS_FORCEFILESYSTEM |
                               (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
        if (!folder) {
            const COMDLG_FILTERSPEC types[] = {
                {L"Instrumentos y muestras", L"*.npno;*.nsp;*.nsmp;*.nsmp3;*.nsmp4;*.sf2;*.sfz"},
                {L"Todos los archivos", L"*.*"}
            };
            dialog->SetFileTypes(2, types);
        }
        dialog->SetTitle(folder ? L"Seleccionar carpeta de samples" : L"Seleccionar instrumento de samples");
        if (SUCCEEDED(dialog->Show(parent))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    const int count = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (count > 1) {
                        std::string utf8(static_cast<size_t>(count), '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), count, nullptr, nullptr);
                        utf8.pop_back();
                        selected = std::move(utf8);
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (uninit) CoUninitialize();
    return selected;
}

// 16:10 content aspect, including the nonclient window border.
void constrainSizing(HWND hwnd, WPARAM edge, RECT* r) {
    RECT client{}, window{};
    GetClientRect(hwnd, &client); GetWindowRect(hwnd, &window);
    const int decorationW = (window.right - window.left) - (client.right - client.left);
    const int decorationH = (window.bottom - window.top) - (client.bottom - client.top);
    constexpr double ratio = 1.6;
    const bool verticalOnly = edge == WMSZ_TOP || edge == WMSZ_BOTTOM;
    int newClientW = std::max(800, r->right - r->left - decorationW);
    int newClientH = std::max(500, r->bottom - r->top - decorationH);
    if (verticalOnly) newClientW = static_cast<int>(std::lround(newClientH * ratio));
    else newClientH = static_cast<int>(std::lround(newClientW / ratio));
    const int outW = newClientW + decorationW, outH = newClientH + decorationH;
    switch(edge) {
      case WMSZ_LEFT: case WMSZ_TOPLEFT: case WMSZ_BOTTOMLEFT: r->left = r->right - outW; break;
      default: r->right = r->left + outW; break;
    }
    switch(edge) {
      case WMSZ_TOP: case WMSZ_TOPLEFT: case WMSZ_TOPRIGHT: r->top = r->bottom - outH; break;
      default: r->bottom = r->top + outH; break;
    }
}

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
        RECT c{}, r{}; GetClientRect(hwnd, &c); GetWindowRect(hwnd, &r);
        info->ptMinTrackSize.x = 800 + (r.right - r.left) - (c.right - c.left);
        info->ptMinTrackSize.y = 500 + (r.bottom - r.top) - (c.bottom - c.top);
    }
    if (msg == WM_SIZING && !g_fullscreen) {
        constrainSizing(hwnd, wp, reinterpret_cast<RECT*>(lp));
        return TRUE;
    }
    if (msg == WM_SYSCOMMAND && (wp & 0xfff0) == SC_MAXIMIZE) return 0;
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
        // Maximizing on a 16:9 or 4:3 screen would break the fixed-width console.
        // Keep manual resize, minimize and F11; disable ordinary maximize.
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~WS_MAXIMIZEBOX);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        state.maximized = false;
        g_previousProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(windowProc)));
        if (HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1))) {
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        }
        // Fit the first window into the active monitor's WORK area, preserving
        // the client aspect on HiDPI/low-resolution displays (not just 1440x900).
        WINDOWPLACEMENT wp{sizeof(WINDOWPLACEMENT)};
        GetWindowPlacement(hwnd, &wp);
        MONITORINFO display{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &display);
        RECT rc{}, current{};
        GetWindowRect(hwnd, &rc);
        GetClientRect(hwnd, &current);
        const int chromeW = (rc.right - rc.left) - (current.right - current.left);
        const int chromeH = (rc.bottom - rc.top) - (current.bottom - current.top);
        const int maxW = std::max(800, display.rcWork.right - display.rcWork.left - chromeW - 12);
        const int maxH = std::max(500, display.rcWork.bottom - display.rcWork.top - chromeH - 12);
        const int desiredW = state.width >= 800 ? state.width : 1280;
        const int contentW = std::max(800, std::min({desiredW, maxW, maxH * 8 / 5}));
        const int outerW = contentW + chromeW;
        const int outerH = contentW * 5 / 8 + chromeH;
        int x = state.x, y = state.y;
        if (x < display.rcWork.left || y < display.rcWork.top ||
            x + outerW > display.rcWork.right || y + outerH > display.rcWork.bottom) {
            x = display.rcWork.left + (display.rcWork.right - display.rcWork.left - outerW) / 2;
            y = display.rcWork.top + (display.rcWork.bottom - display.rcWork.top - outerH) / 2;
        }
        wp.rcNormalPosition = RECT{x, y, x + outerW, y + outerH};
        wp.showCmd = SW_SHOWNORMAL;
        SetWindowPlacement(hwnd, &wp);
        if (state.fullscreen) setFullscreen(hwnd, true);
        w.bind("__liveStageSelectSamplePath", [hwnd](const std::string& args) -> std::string {
            const bool folder = args.find("folder") != std::string::npos;
            return nlohmann::json(selectLocalSamplePath(folder, hwnd)).dump();
        });
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
#include <nlohmann/json.hpp>

std::string stageMacSelectSamplePath(bool directory);
void stageMacApplyWindowAspect(void* nativeWindow);

bool nativeWindowAvailable() { return true; }
bool runNativeWindow(const std::string& url, NativeWindowState& state, std::string& error) {
    try {
        // Cocoa/WKWebView: the DSP continues independently on the audio callback.
        webview::webview window(false, nullptr);
        window.set_title("Sonus Live Stage");
        // MacBook Pro M1 13" often has a 1280x800 logical display. Start inside
        // the visible desktop rather than using the Windows default of 1440x900.
        // The user can resize normally, including on larger external monitors.
        const int cw = std::clamp(state.width, 800, 1152);
        window.set_size(cw, cw * 5 / 8, WEBVIEW_HINT_NONE);
        auto handle = window.window();
        if (!handle.ok()) { error = "WKWebView: no native window"; return false; }
        stageMacApplyWindowAspect(handle.value());
        window.bind("__liveStageSelectSamplePath", [](const std::string& args) -> std::string {
            const bool folder = args.find("folder") != std::string::npos;
            return nlohmann::json(stageMacSelectSamplePath(folder)).dump();
        });
        window.init("window.__liveStageNative=true;");
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
