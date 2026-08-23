#include "wannaviewer/core/AppPaths.hpp"
#include "wannaviewer/core/Config.hpp"
#include "wannaviewer/core/Logger.hpp"
#include "wannaviewer/playback/MpvEngine.hpp"

#include <memory>

#import <AppKit/AppKit.h>

namespace {

struct State final {
    wannaviewer::AppPaths paths;
    wannaviewer::Config config;
    wannaviewer::Logger logger;
    std::unique_ptr<wannaviewer::MpvEngine> engine;
};

std::unique_ptr<State> state;

@interface WVWindow : NSWindow
@end

@implementation WVWindow
- (void)keyDown:(NSEvent*)event {
    if (!state || !state->engine) return;
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if ([key isEqualToString:@" "]) state->engine->TogglePause();
    else if ([key isEqualToString:@"f"]) [self toggleFullScreen:nil];
    else if ([key isEqualToString:@"m"]) state->engine->ToggleMute();
    else if ([key isEqualToString:@"s"]) state->engine->CycleSubtitles();
    else if ([key isEqualToString:@"a"]) state->engine->CycleAudio();
    else if (event.keyCode == 123) state->engine->SeekRelative((event.modifierFlags & NSEventModifierFlagShift) ? -30.0 : -5.0);
    else if (event.keyCode == 124) state->engine->SeekRelative((event.modifierFlags & NSEventModifierFlagShift) ? 30.0 : 5.0);
    else [super keyDown:event];
}
@end

@interface WVAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, retain) WVWindow* window;
@end

@implementation WVAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    auto paths = wannaviewer::AppPaths::Discover();
    paths.EnsureWritableDirectories();
    auto config = wannaviewer::Config::Load(paths.config / "player.conf");
    state = std::make_unique<State>();
    state->paths = std::move(paths);
    state->config = std::move(config);
    state->logger.Open(state->paths.logs, wannaviewer::ParseLogLevel(state->config.GetString("logging.level", "info")));
    state->engine = std::make_unique<wannaviewer::MpvEngine>(state->paths, state->config, state->logger);

    self.window = [[[WVWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1280, 720)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO] autorelease];
    self.window.title = @"WannaViewer";
    self.window.delegate = self;
    self.window.contentView.wantsLayer = YES;
    self.window.contentView.layer.backgroundColor = NSColor.blackColor.CGColor;
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    state->engine->Initialize(reinterpret_cast<std::uintptr_t>(self.window.contentView), [](wannaviewer::PlaybackEvent event) {
        if (event.type == wannaviewer::PlaybackEventType::Error) {
            const auto message = event.value;
            dispatch_async(dispatch_get_main_queue(), ^{
                NSAlert* alert = [[[NSAlert alloc] init] autorelease];
                alert.messageText = @"Playback error";
                alert.informativeText = [NSString stringWithUTF8String:message.c_str()];
                [alert runModal];
            });
        }
    });
    NSArray<NSString*>* arguments = NSProcessInfo.processInfo.arguments;
    if (arguments.count > 1) state->engine->Open(arguments[1].UTF8String);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { (void)sender; return YES; }
- (void)applicationWillTerminate:(NSNotification*)notification { (void)notification; if (state && state->engine) state->engine->Shutdown(); state.reset(); }

- (void)openDocument:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] == NSModalResponseOK && state && state->engine) state->engine->Open(panel.URL.path.UTF8String);
}
@end

} // namespace

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv;
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        WVAppDelegate* delegate = [[[WVAppDelegate alloc] init] autorelease];
        application.delegate = delegate;
        NSMenu* menuBar = [[[NSMenu alloc] init] autorelease];
        NSMenuItem* applicationItem = [[[NSMenuItem alloc] init] autorelease];
        [menuBar addItem:applicationItem];
        application.mainMenu = menuBar;
        NSMenu* applicationMenu = [[[NSMenu alloc] init] autorelease];
        [applicationMenu addItemWithTitle:@"Open…" action:@selector(openDocument:) keyEquivalent:@"o"];
        [applicationMenu addItem:[NSMenuItem separatorItem]];
        [applicationMenu addItemWithTitle:@"Quit WannaViewer" action:@selector(terminate:) keyEquivalent:@"q"];
        applicationItem.submenu = applicationMenu;
        [application run];
    }
    return 0;
}
