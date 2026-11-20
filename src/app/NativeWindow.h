#pragma once
// Native React window: WebView2 on Windows, WKWebView on macOS. The engine and its HTTP /
// WebSocket API are unchanged; the window simply loads http://127.0.0.1:<port>/.
#include <string>

struct NativeWindowState {
    int x = -1, y = -1, width = 1440, height = 900;   // -1: centred by Windows
    bool maximized = true;
    bool fullscreen = false;
};

/** True when a native WebView2/WKWebView window is available. */
bool nativeWindowAvailable();

/** Opens the window on the calling thread and runs its message loop until it is closed.
 *  Windows asks for confirmation before closing; macOS currently uses standard Cocoa
 *  close behavior. Returns false if a native window cannot be created.
 *  The initial size comes from state; persistence of Mac window placement is pending. */
bool runNativeWindow(const std::string& url, NativeWindowState& state, std::string& error);
