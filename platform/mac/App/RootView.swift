//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// RootView.swift -- The window: sidebar and the selected tunnel.
//
//===============================================================

import AppKit
import FairleadCore
import SwiftUI
import UniformTypeIdentifiers

struct RootView: View {
    @EnvironmentObject private var model: AppModel
    @State private var dropTargeted = false
    @State private var pendingDelete: Tunnel?

    var body: some View {
        HStack(spacing: 0) {
            SidebarView(pendingDelete: $pendingDelete)
                .frame(width: 280)
            Rectangle()
                .fill(Theme.line)
                .frame(width: 1)
            DetailView(pendingDelete: $pendingDelete)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(Theme.bg)
        .background(WindowTuner())
        .overlay {
            if dropTargeted {
                RoundedRectangle(cornerRadius: 18, style: .continuous)
                    .strokeBorder(Theme.accent, style: StrokeStyle(lineWidth: 1.5, dash: [7, 5]))
                    .padding(10)
                    .allowsHitTesting(false)
            }
        }
        .onDrop(of: [.fileURL], isTargeted: $dropTargeted) { providers in
            loadDrop(providers)
            return true
        }
        .sheet(item: $model.editing) { tunnel in
            EditorSheet(tunnel: tunnel) { updated in
                model.update(updated)
                model.editing = nil
            }
        }
        .sheet(isPresented: $model.showSettings) {
            SettingsSheet()
        }
        .confirmationDialog(
            Text(FairleadL10n.format("Delete the tunnel “%@”?", pendingDelete?.name ?? "")),
            isPresented: Binding(
                get: { pendingDelete != nil },
                set: { if !$0 { pendingDelete = nil } }
            ),
            titleVisibility: .visible
        ) {
            Button(FairleadL10n.text("Delete"), role: .destructive) {
                if let pendingDelete { model.delete(pendingDelete) }
                pendingDelete = nil
            }

            Button(FairleadL10n.text("Cancel"), role: .cancel) { pendingDelete = nil }
        }
    }

/*
===============
loadDrop
===============
*/
    private func loadDrop(_ providers: [NSItemProvider]) {
        for provider in providers {
            provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
                guard let data = item as? Data, let url = URL(dataRepresentation: data, relativeTo: nil) else {
                    if let url = item as? URL {
                        Task { @MainActor in model.importFiles([url]) }
                    }

                    return
                }

                Task { @MainActor in model.importFiles([url]) }
            }
        }
    }
}

struct SidebarView: View {
    @EnvironmentObject private var model: AppModel
    @Binding var pendingDelete: Tunnel?
    @State private var draggedID: UUID?

    private var canReorder: Bool {
        model.query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 10) {
                Mark(size: 16)
                Text("FAIRLEAD")
                    .font(.system(size: 12, weight: .semibold))
                    .tracking(2.2)
                    .foregroundStyle(Theme.text)
                Spacer(minLength: 8)
                Circle()
                    .fill(model.helperOnline && model.tools.ready && !model.helperStale ? Theme.accent : Theme.faint)
                    .frame(width: 7, height: 7)
                    .animation(Motion.soft, value: model.helperOnline)
                    .animation(Motion.soft, value: model.helperStale)
                    .help(FairleadL10n.text(helperStatus))
                    .accessibilityLabel(FairleadL10n.text(helperStatus))
                Button {
                    model.showSettings = true
                } label: {
                    Image(systemName: "gearshape")
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(Theme.muted)
                        .frame(width: 28, height: 28)
                        .contentShape(Rectangle())
                }
                .buttonStyle(FairleadPressStyle(pressedScale: 0.9))
                .focusEffectDisabled()
                .help(FairleadL10n.text("Settings"))
            }
            .padding(.top, 18)
            .padding(.horizontal, 18)
            .padding(.bottom, 16)

            HStack(spacing: 8) {
                Image(systemName: "magnifyingglass")
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.faint)
                TextField(FairleadL10n.text("Search"), text: $model.query)
                    .textFieldStyle(.plain)
                    .font(.system(size: 13))
                    .foregroundStyle(Theme.text)
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 8)
            .background(Color.white.opacity(0.04), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            .padding(.horizontal, 14)

            Text(FairleadL10n.text("Tunnels").uppercased())
                .font(.system(size: 10, weight: .semibold))
                .tracking(1.1)
                .foregroundStyle(Theme.faint)
                .padding(.horizontal, 18)
                .padding(.top, 22)
                .padding(.bottom, 8)

            ScrollView {
                // VStack (not Lazy): macOS onDrag hit-testing is unreliable in LazyVStack.
                VStack(spacing: 2) {
                    ForEach(model.filtered) { tunnel in
                        SidebarRow(
                            tunnel: tunnel,
                            pendingDelete: $pendingDelete,
                            draggedID: $draggedID,
                            canReorder: canReorder
                        )
                    }
                }
                .padding(.horizontal, 10)
            }

            Spacer(minLength: 0)

            HStack(spacing: 8) {
                FairleadButton(title: "New", systemImage: "plus") { model.createTunnel() }
                FairleadButton(title: "Import", systemImage: "square.and.arrow.down") { model.importPanel() }
            }
            .padding(14)
        }
        .background(Theme.sidebar)
        .onChange(of: canReorder) { _, ok in
            if !ok {
                draggedID = nil
            }
        }
    }

    private var helperStatus: String {
        if model.helperStale { return "Helper is out of date" }
        return model.helperOnline ? "Helper is running" : "Helper is not running"
    }
}

struct SidebarRow: View {
    @EnvironmentObject private var model: AppModel
    var tunnel: Tunnel
    @Binding var pendingDelete: Tunnel?
    @Binding var draggedID: UUID?
    var canReorder: Bool
    @State private var hovered = false
    @State private var pressed = false

    private var selected: Bool { model.selection == tunnel.id }
    private var connected: Bool { model.live(tunnel.id).phase == .connected }
    private var dragging: Bool { draggedID == tunnel.id }
    private var showPress: Bool { pressed && !dragging }

    var body: some View {
        HStack(spacing: 10) {
            Circle()
                .fill(connected ? Theme.accent : Theme.faint)
                .frame(width: 7, height: 7)
                .animation(Motion.soft, value: connected)
            VStack(alignment: .leading, spacing: 2) {
                Text(tunnel.name)
                    .font(.system(size: 13, weight: selected ? .semibold : .regular))
                    .foregroundStyle(Theme.text)
                    .lineLimit(1)
                Text(tunnel.endpointLabel)
                    .font(.system(size: 11))
                    .foregroundStyle(Theme.muted)
                    .lineLimit(1)
            }

            Spacer(minLength: 0)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 9)
        .background(
            RoundedRectangle(cornerRadius: 9, style: .continuous)
                .fill(selected ? Color.white.opacity(0.07) : (hovered || showPress ? Color.white.opacity(0.035) : Color.clear))
                .animation(Motion.soft, value: selected)
                .animation(.easeOut(duration: 0.12), value: hovered)
        )
        .scaleEffect(showPress ? 0.97 : 1)
        .animation(Motion.press, value: showPress)
        .contentShape(Rectangle())
        .background(RowPressMonitor(pressed: $pressed, onClick: {
            draggedID = nil
            model.selection = tunnel.id
        }))
        .onHover { hovered = $0 }
        .modifier(
            TunnelRowDrag(
                enabled: canReorder,
                tunnelID: tunnel.id,
                draggedID: $draggedID,
                begin: {
                    pressed = false
                    model.selection = tunnel.id
                },
                move: { id, target in model.moveTunnel(id, over: target, save: false) },
                commit: { model.saveOrder() }
            )
        )
        .contextMenu {
            Button(FairleadL10n.text(connected ? "Disconnect" : "Connect")) { model.toggle(tunnel.id) }
            Button(FairleadL10n.text("Edit")) { model.editing = tunnel }
            Button(FairleadL10n.text("Duplicate")) { model.duplicate(tunnel) }
            Button(FairleadL10n.text("Export…")) { model.export(tunnel) }
            Divider()
            Button(FairleadL10n.text("Delete"), role: .destructive) { pendingDelete = tunnel }
        }
    }
}

/// AppKit mouse monitor: press spring without stealing hits from onDrag.
private struct RowPressMonitor: NSViewRepresentable {
    @Binding var pressed: Bool
    var onClick: () -> Void

    func makeNSView(context: Context) -> MonitorView {
        let view = MonitorView()
        view.onPressed = { pressed = $0 }
        view.onClick = onClick
        return view
    }

    func updateNSView(_ view: MonitorView, context: Context) {
        view.onPressed = { pressed = $0 }
        view.onClick = onClick
    }

    final class MonitorView: NSView {
        var onPressed: ((Bool) -> Void)?
        var onClick: (() -> Void)?
        private var monitor: Any?
        private var downInside = false
        private var dragSlop = false
        private var downPoint: NSPoint?

        override func hitTest(_ point: NSPoint) -> NSView? { nil }

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            clearMonitor()
            guard window != nil else { return }
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .leftMouseUp, .leftMouseDragged]) { [weak self] event in
                self?.handle(event)
                return event
            }
        }

        override func removeFromSuperview() {
            clearMonitor()
            super.removeFromSuperview()
        }

        deinit { clearMonitor() }

        private func clearMonitor() {
            if let monitor {
                NSEvent.removeMonitor(monitor)
                self.monitor = nil
            }
        }

        private func handle(_ event: NSEvent) {
            guard let window, event.window == window else { return }
            let local = convert(event.locationInWindow, from: nil)
            let inside = bounds.contains(local)
            switch event.type {
            case .leftMouseDown:
                guard inside else { return }
                downInside = true
                dragSlop = false
                downPoint = event.locationInWindow
                onPressed?(true)
            case .leftMouseDragged:
                guard downInside else { return }
                if !dragSlop, let downPoint {
                    let distance = hypot(event.locationInWindow.x - downPoint.x, event.locationInWindow.y - downPoint.y)
                    if distance > 6 {
                        dragSlop = true
                        onPressed?(false)
                    }
                }
                if !inside {
                    onPressed?(false)
                }
            case .leftMouseUp:
                let wasDown = downInside
                let wasDrag = dragSlop
                downInside = false
                dragSlop = false
                downPoint = nil
                onPressed?(false)
                if wasDown && inside && !wasDrag {
                    onClick?()
                }
            default:
                break
            }
        }
    }
}

private struct TunnelRowDrag: ViewModifier {
    var enabled: Bool
    var tunnelID: UUID
    @Binding var draggedID: UUID?
    var begin: () -> Void
    var move: (UUID, UUID) -> Bool
    var commit: () -> Void

    @ViewBuilder
    func body(content: Content) -> some View {
        if enabled {
            content
                // Empty preview: live reorder already moves the row in the list; a cursor
                // ghost on top of it just flickers.
                .onDrag {
                    begin()
                    draggedID = tunnelID
                    return NSItemProvider(object: tunnelID.uuidString as NSString)
                } preview: {
                    Color.clear.frame(width: 1, height: 1)
                }
                .onDrop(
                    of: [.text, .utf8PlainText, .plainText],
                    delegate: TunnelReorderDrop(
                        targetID: tunnelID,
                        draggedID: $draggedID,
                        move: move,
                        commit: commit
                    )
                )
        } else {
            content
        }
    }
}

private struct TunnelReorderDrop: DropDelegate {
    var targetID: UUID
    @Binding var draggedID: UUID?
    var move: (UUID, UUID) -> Bool
    var commit: () -> Void

    func validateDrop(info: DropInfo) -> Bool {
        draggedID != nil
    }

    func dropUpdated(info: DropInfo) -> DropProposal? {
        DropProposal(operation: draggedID != nil ? .move : .cancel)
    }

    func dropEntered(info: DropInfo) {
        guard let draggedID, draggedID != targetID else { return }
        withAnimation(Motion.soft) {
            _ = move(draggedID, targetID)
        }
    }

    func performDrop(info: DropInfo) -> Bool {
        defer { draggedID = nil }
        guard draggedID != nil else { return false }
        commit()
        return true
    }
}
