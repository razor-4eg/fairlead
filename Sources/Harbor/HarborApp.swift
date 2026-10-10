import AppKit
import SwiftUI

@MainActor
final class HarborDelegate: NSObject, NSApplicationDelegate {
    static var terminating = false
    let model = AppModel()
    private var stoppedTunnels = false

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        false
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        NSApp.setActivationPolicy(.regular)
        if !flag {
            sender.windows.first { $0.canBecomeMain }?.makeKeyAndOrderFront(nil)
        }
        return true
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        Self.terminating = true
        stopTunnels()
        return .terminateNow
    }

    func applicationWillTerminate(_ notification: Notification) {
        Self.terminating = true
        stopTunnels()
    }

    private func stopTunnels() {
        guard !stoppedTunnels else { return }
        stoppedTunnels = true
        model.shutdownTunnels()
    }
}

@main
struct HarborApp: App {
    @NSApplicationDelegateAdaptor(HarborDelegate.self) private var delegate

    var body: some Scene {
        WindowGroup(id: "main") {
            RootView()
                .environmentObject(delegate.model)
                .preferredColorScheme(.dark)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1120, height: 760)
        .commands {
            CommandGroup(replacing: .appSettings) {
                Button("Settings…") {
                    NSApp.setActivationPolicy(.regular)
                    NSApp.windows.first { $0.canBecomeMain }?.makeKeyAndOrderFront(nil)
                    NSApp.activate(ignoringOtherApps: true)
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

        MenuBarExtra {
            MenuBarContent()
                .environmentObject(delegate.model)
        } label: {
            MenuBarLabel(model: delegate.model)
        }
        .menuBarExtraStyle(.menu)
    }
}

struct MenuBarLabel: View {
    @ObservedObject var model: AppModel

    var body: some View {
        Image(nsImage: MenuBarMark.image(connected: model.anyConnected))
    }
}

struct MenuBarContent: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        if model.tunnels.isEmpty {
            Button("Open Harbor") { showWindow() }
        } else {
            ForEach(model.tunnels) { tunnel in
                Button(model.menuTitle(tunnel)) { model.toggle(tunnel.id) }
            }
            Divider()
            Button("Open Harbor") { showWindow() }
        }
        Divider()
        Button("Settings…") {
            showWindow()
            model.showSettings = true
        }
        Button("Quit Harbor") { NSApp.terminate(nil) }
    }

    private func showWindow() {
        NSApp.setActivationPolicy(.regular)
        if let window = NSApp.windows.first(where: { $0.canBecomeMain }) {
            window.makeKeyAndOrderFront(nil)
        } else {
            openWindow(id: "main")
        }
        NSApp.activate(ignoringOtherApps: true)
    }
}
