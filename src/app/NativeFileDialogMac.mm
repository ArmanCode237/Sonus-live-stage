// Cocoa UI-only helpers. NEVER called from RtAudio/CoreMIDI callbacks.
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
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

void stageMacApplyWindowAspect(void* nativeWindow) {
    const auto configure = ^{
        NSWindow* window = (__bridge NSWindow*)nativeWindow;
        if (!window) return;
        window.contentAspectRatio = NSMakeSize(16, 10);
        window.contentMinSize = NSMakeSize(800, 500);
        // Keep the native zoom button from entering an arbitrary aspect ratio.
        [[window standardWindowButton:NSWindowZoomButton] setEnabled:NO];
    };
    if ([NSThread isMainThread]) configure();
    else dispatch_sync(dispatch_get_main_queue(), configure);
}
