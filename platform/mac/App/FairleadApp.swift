//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// FairleadApp.swift -- Application entry, the main window, and the menu bar.
//
//================================================================

import AppKit
import SwiftUI

@MainActor
final class FairleadDelegate: NSObject, NSApplicationDelegate {
    static var terminating = false
    let model = AppModel()
    private var didShutdown = false

/*
===============
applicationWillFinishLaunching
Accessory policy must land before any window or Dock icon is created.
===============
*/
    func applicationWillFinishLaunching(_ notification: Notification) {
        model.prepareStartHidden()
    }

/*
===============
applicationDidFinishLaunching
===============
*/
    func applicationDidFinishLaunching(_ notification: Notification) {
        model.finishStartHidden()
        // Restoration can create the window a moment after didFinish.
        DispatchQueue.main.async { [weak self] in
            self?.model.finishStartHidden()
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in
            self?.model.finishStartHidden()
        }
    }

/*
===============
applicationShouldTerminateAfterLastWindowClosed
===============
*/
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        false
    }

/*
===============
applicationShouldHandleReopen
===============
*/
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        // Returning true lets AppKit bring a window forward — refuse that while Start hidden.
        if model.shouldIgnoreReopen() {
            model.finishStartHidden()
            return false
        }

        if !flag {
            model.requestOpenMainWindow()
        } else {
            NSApp.setActivationPolicy(.regular)
            NSApp.activate(ignoringOtherApps: true)
        }

        return true
    }

/*
===============
applicationShouldTerminate
===============
*/
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        Self.terminating = true
        stopHelper()
        return .terminateNow
    }

/*
===============
applicationWillTerminate
===============
*/
    func applicationWillTerminate(_ notification: Notification) {
        Self.terminating = true
        stopHelper()
    }

/*
===============
stopHelper
Down tunnels and quit the helper before the app exits.
===============
*/
    private func stopHelper() {
        guard !didShutdown else { return }
        didShutdown = true
        model.shutdown()
    }
}

@main
struct FairleadApp: App {
    @NSApplicationDelegateAdaptor(FairleadDelegate.self) private var delegate

    var body: some Scene {
        // First scene is what SwiftUI presents at launch — keep the menu bar first so
        // Start hidden never flashes the main window (or Dock icon) on cold start.
        MenuBarExtra {
            MenuBarContent()
                .environmentObject(delegate.model)
        } label: {
            MenuBarLabel(model: delegate.model)
        }
        .menuBarExtraStyle(.menu)

        Window("Fairlead", id: "main") {
            RootView()
                .environmentObject(delegate.model)
                .preferredColorScheme(.dark)
                .background(WindowRestorableFix())
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1120, height: 760)
        .defaultLaunchBehavior(.suppressed)
        .restorationBehavior(.disabled)
        .commands {
            CommandGroup(replacing: .appSettings) {
                Button("Settings…") {
                    delegate.model.requestOpenMainWindow()
                    delegate.model.showSettings = true
                }
                .keyboardShortcut(",", modifiers: .command)
            }

            CommandGroup(replacing: .newItem) {
                Button("New tunnel") { delegate.model.createTunnel() }
                    .keyboardShortcut("n", modifiers: .command)
                Button("Import config…") { delegate.model.importPanel() }
                    .keyboardShortcut("i", modifiers: .command)
            }

            CommandMenu("Tunnel") {
                Button("Connect or disconnect") {
                    if let id = delegate.model.selection { delegate.model.toggle(id) }
                }
                .keyboardShortcut("k", modifiers: .command)
                Button("Edit") {
                    delegate.model.editing = delegate.model.selectedTunnel
                }
                .keyboardShortcut("e", modifiers: .command)
                .disabled(delegate.model.selectedTunnel == nil)
            }
        }
    }
}

struct WindowRestorableFix: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        DispatchQueue.main.async {
            view.window?.isRestorable = false
            view.window?.isReleasedWhenClosed = true
        }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        nsView.window?.isRestorable = false
    }
}

struct MenuBarLabel: View {
    @ObservedObject var model: AppModel
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Image(nsImage: MenuBarMark.image(connected: model.anyConnected))
            .onAppear {
                model.finishStartHidden()
                model.revealMainWindowAtLaunchIfNeeded(open: openWindow)
            }
            .onReceive(NotificationCenter.default.publisher(for: AppModel.openMainNotification)) { _ in
                openWindow(id: "main")
                NSApp.activate(ignoringOtherApps: true)
            }
    }
}

struct MenuBarContent: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        if model.tunnels.isEmpty {
            Button("Open Fairlead") { model.requestOpenMainWindow() }
        } else {
            ForEach(model.tunnels) { tunnel in
                Button(model.menuTitle(tunnel)) { model.toggle(tunnel.id) }
            }

            Divider()
            Button("Open Fairlead") { model.requestOpenMainWindow() }
        }

        Divider()
        Button("Settings…") {
            model.requestOpenMainWindow()
            model.showSettings = true
        }

        Button("Quit Fairlead") { NSApp.terminate(nil) }
    }
}
