import AppKit
import HarborCore
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
            Text(HarborL10n.format("Delete the tunnel “%@”?", pendingDelete?.name ?? "")),
            isPresented: Binding(
                get: { pendingDelete != nil },
                set: { if !$0 { pendingDelete = nil } }
            ),
            titleVisibility: .visible
        ) {
            Button("Delete", role: .destructive) {
                if let pendingDelete { model.delete(pendingDelete) }
                pendingDelete = nil
            }
            Button("Cancel", role: .cancel) { pendingDelete = nil }
        }
    }

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

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 10) {
                Mark(size: 16)
                Text("HARBOR")
                    .font(.system(size: 12, weight: .semibold))
                    .tracking(2.2)
                    .foregroundStyle(Theme.text)
                Spacer()
                Button {
                    model.showSettings = true
                } label: {
                    Image(systemName: "gearshape")
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(Theme.muted)
                        .frame(width: 28, height: 28)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .focusEffectDisabled()
                .help("Settings")
            }
            .padding(.top, 18)
            .padding(.horizontal, 18)
            .padding(.bottom, 16)

            HStack(spacing: 8) {
                Image(systemName: "magnifyingglass")
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.faint)
                TextField("Search", text: $model.query)
                    .textFieldStyle(.plain)
                    .font(.system(size: 13))
                    .foregroundStyle(Theme.text)
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 8)
            .background(Color.white.opacity(0.04), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            .padding(.horizontal, 14)

            Text(HarborL10n.text("Tunnels").uppercased())
                .font(.system(size: 10, weight: .semibold))
                .tracking(1.1)
                .foregroundStyle(Theme.faint)
                .padding(.horizontal, 18)
                .padding(.top, 22)
                .padding(.bottom, 8)

            ScrollView {
                LazyVStack(spacing: 2) {
                    ForEach(model.filtered) { tunnel in
                        SidebarRow(tunnel: tunnel, pendingDelete: $pendingDelete)
                    }
                }
                .padding(.horizontal, 10)
            }

            Spacer(minLength: 0)

            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 8) {
                    Circle()
                        .fill(model.helperOnline && model.tools.ready && !model.helperStale ? Theme.accent : Theme.faint)
                        .frame(width: 6, height: 6)
                    Text(HarborL10n.text(helperStatus))
                        .font(.system(size: 11))
                        .foregroundStyle(Theme.muted)
                    Spacer()
                }
                .contentShape(Rectangle())
                .onTapGesture { model.showSettings = true }

                HStack(spacing: 8) {
                    HarborButton(title: "New", systemImage: "plus") { model.createTunnel() }
                    HarborButton(title: "Import", systemImage: "square.and.arrow.down") { model.importPanel() }
                }
            }
            .padding(14)
        }
        .background(Theme.sidebar)
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
    @State private var hovered = false

    private var selected: Bool { model.selection == tunnel.id }
    private var connected: Bool { model.live(tunnel.id).phase == .connected }

    var body: some View {
        Button {
            model.selection = tunnel.id
        } label: {
            HStack(spacing: 10) {
                Circle()
                    .fill(connected ? Theme.accent : Theme.faint)
                    .frame(width: 7, height: 7)
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
                    .fill(selected ? Color.white.opacity(0.07) : (hovered ? Color.white.opacity(0.035) : Color.clear))
            )
        }
        .buttonStyle(.plain)
        .onHover { hovered = $0 }
        .contextMenu {
            Button(HarborL10n.text(connected ? "Disconnect" : "Connect")) { model.toggle(tunnel.id) }
            Button("Edit") { model.editing = tunnel }
            Button("Export…") { model.export(tunnel) }
            Divider()
            Button("Delete", role: .destructive) { pendingDelete = tunnel }
        }
    }
}
