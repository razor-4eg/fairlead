import HarborCore
import SwiftUI

struct DetailView: View {
    @EnvironmentObject private var model: AppModel
    @Binding var pendingDelete: Tunnel?

    var body: some View {
        Group {
            if let tunnel = model.selectedTunnel {
                tunnelBody(tunnel)
            } else {
                empty
            }
        }
        .background(Theme.bg)
    }

    private var empty: some View {
        VStack(spacing: 16) {
            Mark(size: 42)
            Text("No tunnels")
                .font(.system(size: 22, weight: .medium))
                .foregroundStyle(Theme.text)
            Text("Import a WireGuard config or create a new one.\nYou can drop a .conf file on the window.")
                .font(.system(size: 13))
                .foregroundStyle(Theme.muted)
                .multilineTextAlignment(.center)
            HStack(spacing: 8) {
                HarborButton(title: "Create", prominent: true) { model.createTunnel() }
                HarborButton(title: "Import") { model.importPanel() }
            }
            .padding(.top, 6)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func tunnelBody(_ tunnel: Tunnel) -> some View {
        let live = model.live(tunnel.id)
        return ScrollView {
            VStack(alignment: .leading, spacing: 26) {
                if let message = statusMessage {
                    Notice(text: message, tone: .setup) {
                        model.showSettings = true
                    }
                }
                if let banner = model.banner {
                    Notice(text: banner, tone: .danger) {
                        model.banner = nil
                    }
                }

                HStack(alignment: .firstTextBaseline) {
                    VStack(alignment: .leading, spacing: 6) {
                        Text(tunnel.name)
                            .font(.system(size: 28, weight: .medium))
                            .foregroundStyle(Theme.text)
                        Text(live.endpoint ?? tunnel.endpointLabel)
                            .font(.system(size: 13))
                            .foregroundStyle(Theme.muted)
                        Text(tunnel.addressLabel)
                            .font(.system(size: 12, design: .monospaced))
                            .foregroundStyle(Theme.faint)
                    }
                    Spacer()
                    HarborButton(title: "Edit") { model.editing = tunnel }
                    Menu {
                        Button("Export…") { model.export(tunnel) }
                        Button("Delete", role: .destructive) { pendingDelete = tunnel }
                    } label: {
                        Image(systemName: "ellipsis")
                            .font(.system(size: 14, weight: .medium))
                            .foregroundStyle(Theme.muted)
                            .frame(width: 32, height: 32)
                            .background(Color.white.opacity(0.05), in: Circle())
                    }
                    .menuStyle(.borderlessButton)
                    .frame(width: 32)
                }

                HStack(alignment: .center, spacing: 28) {
                    PowerButton(phase: live.phase) { model.toggle(tunnel.id) }
                    VStack(alignment: .leading, spacing: 6) {
                        Text(statusTitle(live.phase))
                            .font(.system(size: 22, weight: .medium))
                            .foregroundStyle(Theme.text)
                        Text(statusDetail(live))
                            .font(.system(size: 13))
                            .foregroundStyle(Theme.muted)
                        if let iface = live.realInterface, live.phase == .connected {
                            Text(HarborL10n.format("%@  ·  gateway %@", iface, live.gateway ?? "—"))
                                .font(.system(size: 12, design: .monospaced))
                                .foregroundStyle(Theme.faint)
                        }
                    }
                    Spacer()
                }

                HStack(spacing: 10) {
                    StatCell(caption: "Down", value: Format.rate(live.rateDown))
                    StatCell(caption: "Up", value: Format.rate(live.rateUp))
                    StatCell(caption: "Received", value: Format.bytes(live.rx))
                    StatCell(caption: "Sent", value: Format.bytes(live.tx))
                }

                BypassPane(tunnel: tunnel)
            }
            .padding(.horizontal, 32)
            .padding(.top, 28)
            .padding(.bottom, 36)
            .frame(maxWidth: 860, alignment: .leading)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }

    private var statusMessage: String? {
        if model.tools.wireguardGo == nil {
            return HarborL10n.text("wireguard-go is not included in this copy of Harbor.")
        }
        if !model.helperOnline {
            return HarborL10n.text("The system helper is not running. A tunnel cannot start without it.")
        }
        return nil
    }

    private func statusTitle(_ phase: Phase) -> String {
        switch phase {
        case .idle: return HarborL10n.text("Off")
        case .connecting: return HarborL10n.text("Connecting")
        case .connected: return HarborL10n.text("Connected")
        case .disconnecting: return HarborL10n.text("Disconnecting")
        }
    }

    private func statusDetail(_ live: LiveTunnel) -> String {
        switch live.phase {
        case .connected:
            return HarborL10n.format("Handshake %@", Format.ago(live.handshake, now: model.now))
        case .connecting:
            return HarborL10n.text("Bringing up the interface and routes")
        case .disconnecting:
            return HarborL10n.text("Restoring the previous routes")
        case .idle:
            return HarborL10n.text("Traffic is bypassing WireGuard")
        }
    }
}

struct PowerButton: View {
    var phase: Phase
    var action: () -> Void
    @State private var spin = false

    private var connected: Bool { phase == .connected }
    private var busy: Bool { phase == .connecting || phase == .disconnecting }

    var body: some View {
        Button(action: action) {
            ZStack {
                Circle()
                    .stroke(connected ? Theme.accent.opacity(0.9) : Theme.line, lineWidth: 1.5)
                if busy {
                    Circle()
                        .trim(from: 0.08, to: 0.32)
                        .stroke(Theme.accent, style: StrokeStyle(lineWidth: 2, lineCap: .round))
                        .rotationEffect(.degrees(spin ? 360 : 0))
                }
                Image(systemName: "power")
                    .font(.system(size: 26, weight: .light))
                    .foregroundStyle(connected ? Theme.accent : Theme.muted)
            }
            .frame(width: 92, height: 92)
            .background(
                Circle().fill(connected ? Theme.accent.opacity(0.12) : Color.white.opacity(0.03))
            )
        }
        .buttonStyle(.plain)
        .disabled(busy)
        .onAppear { spin = busy }
        .onChange(of: busy) { _, isBusy in
            spin = isBusy
        }
        .animation(busy ? .linear(duration: 0.9).repeatForever(autoreverses: false) : .default, value: spin)
        .help(Text(HarborL10n.text(connected ? "Disconnect" : "Connect")))
    }
}

struct StatCell: View {
    var caption: String
    var value: String

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(HarborL10n.text(caption).uppercased())
                .font(.system(size: 10, weight: .semibold))
                .tracking(0.8)
                .foregroundStyle(Theme.faint)
            Text(value)
                .font(.system(size: 16, weight: .medium, design: .rounded))
                .foregroundStyle(Theme.text)
                .monospacedDigit()
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(Theme.elevated, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(Theme.line)
        )
    }
}

struct Notice: View {
    enum Tone { case setup, danger }
    var text: String
    var tone: Tone
    var action: () -> Void

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: tone == .danger ? "exclamationmark.triangle" : "info.circle")
                .foregroundStyle(tone == .danger ? Theme.danger : Theme.accent)
                .padding(.top, 1)
            Text(text)
                .font(.system(size: 13))
                .foregroundStyle(Theme.text)
                .frame(maxWidth: .infinity, alignment: .leading)
            Button(HarborL10n.text(tone == .danger ? "Close" : "Set up")) { action() }
                .buttonStyle(.plain)
                .font(.system(size: 12, weight: .medium))
                .foregroundStyle(tone == .danger ? Theme.danger : Theme.accent)
        }
        .padding(12)
        .background(
            (tone == .danger ? Theme.danger : Theme.accent).opacity(0.1),
            in: RoundedRectangle(cornerRadius: 10, style: .continuous)
        )
    }
}
