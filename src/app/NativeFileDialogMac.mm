// Cocoa UI-only helpers. NEVER called from RtAudio/CoreMIDI callbacks.
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>
#include <algorithm>
#include <cmath>
#import <dispatch/dispatch.h>
#include <string>

std::string stageMacSelectSamplePath(bool directory) {
    __block std::string selected;
    const auto show = ^{
        @autoreleasepool {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.canChooseFiles = !directory;
            panel.canChooseDirectories = directory;
            panel.allowsMultipleSelection = NO;
            panel.canCreateDirectories = YES;
            panel.title = directory ? @"Seleccionar carpeta de samples" : @"Seleccionar archivo de samples";
            if ([panel runModal] == NSModalResponseOK) {
                NSURL* url = panel.URL;
                if (url.isFileURL && url.path) {
                    const char* utf8 = url.path.fileSystemRepresentation;
                    if (utf8) selected = utf8;
                }
            }
        }
    };
    if ([NSThread isMainThread]) show();
    else dispatch_sync(dispatch_get_main_queue(), show);
    return selected;
}

// Runs `block` on the main thread without deadlocking when already there.
static void stageMacOnMain(dispatch_block_t block) {
    if ([NSThread isMainThread]) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
}

void stageMacApplyWindowAspect(void* nativeWindow) {
    stageMacOnMain(^{
        NSWindow* window = (__bridge NSWindow*)nativeWindow;
        if (!window) return;
        // 16:10 while resizing by hand; the React console adapts to other ratios
        // (full screen on 16:9 displays or the notched MacBook screens).
        window.contentAspectRatio = NSMakeSize(16, 10);
        window.contentMinSize = NSMakeSize(800, 500);
        // The green button enters native full screen (stage mode).
        window.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;
        [[window standardWindowButton:NSWindowZoomButton] setEnabled:YES];
    });
}

// First size: as large as the saved/desired width allows inside the visible
// desktop of the window's screen (menu bar, Dock and notch excluded), 16:10.
void stageMacFitWindow(void* nativeWindow, int desiredWidth) {
    stageMacOnMain(^{
        NSWindow* window = (__bridge NSWindow*)nativeWindow;
        if (!window) return;
        NSScreen* screen = window.screen ?: [NSScreen mainScreen];
        if (!screen) return;
        const NSRect visible = screen.visibleFrame;
        const NSRect frame = window.frame;
        const NSRect content = [window contentRectForFrameRect:frame];
        const CGFloat chromeH = frame.size.height - content.size.height;  // title bar
        const CGFloat margin = 16;
        const CGFloat maxW = std::max<CGFloat>(800, visible.size.width - 2 * margin);
        const CGFloat maxH = std::max<CGFloat>(500, visible.size.height - chromeH - 2 * margin);
        CGFloat w = desiredWidth >= 800 ? desiredWidth : 1440;
        w = std::min({w, maxW, maxH * 1.6});
        w = std::floor(std::max<CGFloat>(800, w));
        [window setContentSize:NSMakeSize(w, std::floor(w / 1.6))];
        [window center];
    });
}

// WKWebView page zoom (macOS 11+): the same zoom as Safari's ⌘+/⌘−, so layout,
// viewport units, media queries and pointer coordinates stay consistent.
// zoom <= 0 only queries. Returns the zoom in effect, or 0 when unsupported.
double stageMacSetPageZoom(void* webViewHandle, double zoom) {
    __block double result = 0;
    stageMacOnMain(^{
        id view = (__bridge id)webViewHandle;
        if (!view || ![view isKindOfClass:[WKWebView class]]) return;
        WKWebView* webView = (WKWebView*)view;
        if (@available(macOS 11.0, *)) {
            if (zoom > 0) webView.pageZoom = std::clamp(zoom, 0.3, 4.0);
            result = webView.pageZoom;
        }
    });
    return result;
}

void stageMacToggleFullScreen(void* nativeWindow) {
    stageMacOnMain(^{
        NSWindow* window = (__bridge NSWindow*)nativeWindow;
        if (window) [window toggleFullScreen:nil];
    });
}
