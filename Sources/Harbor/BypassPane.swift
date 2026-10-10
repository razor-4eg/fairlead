import AppKit
import HarborCore
import SwiftUI

struct BypassPane: View {
    @EnvironmentObject private var model: AppModel
    var tunnel: Tunnel
    @State private var tab = 0
    @State private var draft = ""
    @State private var note = ""
    @State private var fieldError: String?
    @State private var openApps: Set<UUID> = []

    private var live: LiveTunnel { model.live(tunnel.id) }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Direct")
                        .font(.system(size: 18, weight: .medium))
                        .foregroundStyle(Theme.text)
                    Text("These addresses stay out of the tunnel and use the regular network.")
                        .font(.system(size: 12))
                        .foregroundStyle(Theme.muted)
                }
                Spacer()
                Picker("", selection: $tab) {
                    Text("IP").tag(0)
                    Text("Apps").tag(1)
                }
                .pickerStyle(.segmented)
                .frame(width: 220)
            }

            lanRow

            if tab == 0 {
                ipEditor
                if tunnel.ipRules.isEmpty {
                    Text("Nothing here yet. Add an IP or network that should bypass WireGuard.")
                        .font(.system(size: 13))
                        .foregroundStyle(Theme.muted)
                        .padding(.vertical, 8)
                } else {
                    VStack(spacing: 0) {
                        ForEach(tunnel.ipRules) { rule in
                            ipRow(rule)
                            if rule.id != tunnel.ipRules.last?.id {
                                Rectangle().fill(Theme.line).frame(height: 1)
                            }
                        }
                    }
                }
            } else {
                appIntro
                HStack {
                    HarborButton(title: "Add app", systemImage: "plus") {
                        model.addApplications(to: tunnel.id)
                    }
                    Spacer()
                }
                if tunnel.appRules.isEmpty {
                    Text("Choose an .app. Harbor remembers where it connects and keeps those addresses out of the tunnel.")
                        .font(.system(size: 13))
                        .foregroundStyle(Theme.muted)
                        .padding(.vertical, 8)
                } else {
                    VStack(spacing: 8) {
                        ForEach(tunnel.appRules) { rule in
                            appRow(rule)
                        }
                    }
                }
            }

            if !live.warnings.isEmpty, live.phase == .connected {
                Text(live.warnings.joined(separator: " "))
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.muted)
            }
        }
        .padding(18)
        .background(Theme.elevated, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .strokeBorder(Theme.line)
        )
    }

    private var lanRow: some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 3) {
                Text("Local network directly")
                    .font(.system(size: 13, weight: .medium))
                    .foregroundStyle(Theme.text)
                Text(lanCaption)
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.muted)
            }
            Spacer()
            Toggle("", isOn: Binding(
                get: { tunnel.excludeLAN },
                set: { model.setExcludeLAN(tunnel.id, $0) }
            ))
            .toggleStyle(.switch)
            .labelsHidden()
            .tint(Theme.accent)
        }
        .padding(12)
        .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var lanCaption: String {
        if live.lanCIDRs.isEmpty {
            return HarborL10n.text("The current Wi-Fi or Ethernet network. It appears after you connect.")
        }
        return live.lanCIDRs.joined(separator: ", ")
    }

    private var ipEditor: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                TextField("203.0.113.10 or 10.1.0.0/16", text: $draft)
                    .quietField()
                    .onSubmit { commitIP() }
                TextField("Note", text: $note)
                    .quietField()
                    .frame(width: 160)
                Button(action: commitIP) {
                    Image(systemName: "plus")
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundStyle(Theme.ink)
                        .frame(width: 32, height: 32)
                        .background(Theme.accent, in: Circle())
                }
                .buttonStyle(.plain)
            }
            if let fieldError {
                Text(fieldError)
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.danger)
            }
        }
    }

    private func ipRow(_ rule: IPRule) -> some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 2) {
                Text(rule.cidr)
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundStyle(rule.enabled ? Theme.text : Theme.faint)
                if !rule.note.isEmpty {
                    Text(rule.note)
                        .font(.system(size: 11))
                        .foregroundStyle(Theme.muted)
                }
            }
            Spacer()
            Toggle("", isOn: Binding(
                get: { rule.enabled },
                set: { model.setIP(rule.id, tunnel: tunnel.id, enabled: $0) }
            ))
            .toggleStyle(.switch)
            .labelsHidden()
            .tint(Theme.accent)
            Button {
                model.removeIP(rule.id, tunnel: tunnel.id)
            } label: {
                Image(systemName: "xmark")
                    .font(.system(size: 10, weight: .bold))
                    .foregroundStyle(Theme.faint)
            }
            .buttonStyle(.plain)
        }
        .padding(.vertical, 10)
    }

    private var appIntro: some View {
        Text("New addresses of the app are remembered and sent directly. On macOS the route is system-wide, so the same address becomes direct for every app. An open connection switches after it reconnects.")
            .font(.system(size: 12))
            .foregroundStyle(Theme.muted)
            .fixedSize(horizontal: false, vertical: true)
    }

    private func appRow(_ rule: AppRule) -> some View {
        let missing = !FileManager.default.fileExists(atPath: rule.path)
        let expanded = openApps.contains(rule.id)
        return VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 10) {
                Image(nsImage: NSWorkspace.shared.icon(forFile: rule.path))
                    .resizable()
                    .frame(width: 28, height: 28)
                    .opacity(missing ? 0.4 : 1)
                VStack(alignment: .leading, spacing: 2) {
                    Text(rule.name)
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(Theme.text)
                    Text(missing ? HarborL10n.text("App not found") : HarborL10n.addressCount(rule.learned.count))
                        .font(.system(size: 11))
                        .foregroundStyle(missing ? Theme.danger : Theme.muted)
                }
                Spacer()
                Toggle("Learn", isOn: Binding(
                    get: { rule.learn },
                    set: { model.setApp(rule.id, tunnel: tunnel.id, learn: $0) }
                ))
                .toggleStyle(.switch)
                .font(.system(size: 11))
                .tint(Theme.accent)
                Toggle("", isOn: Binding(
                    get: { rule.enabled },
                    set: { model.setApp(rule.id, tunnel: tunnel.id, enabled: $0) }
                ))
                .toggleStyle(.switch)
                .labelsHidden()
                .tint(Theme.accent)
                Button {
                    model.removeApp(rule.id, tunnel: tunnel.id)
                } label: {
                    Image(systemName: "xmark")
                        .font(.system(size: 10, weight: .bold))
                        .foregroundStyle(Theme.faint)
                }
                .buttonStyle(.plain)
            }
            if !rule.learned.isEmpty {
                Button(HarborL10n.text(expanded ? "Hide addresses" : "Show addresses")) {
                    if expanded { openApps.remove(rule.id) } else { openApps.insert(rule.id) }
                }
                .buttonStyle(.plain)
                .font(.system(size: 11, weight: .medium))
                .foregroundStyle(Theme.accent)
                if expanded {
                    FlowAddresses(addresses: rule.learned) { address in
                        model.forget(address, app: rule.id, tunnel: tunnel.id)
                    }
                }
            }
        }
        .padding(10)
        .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private func commitIP() {
        let error = model.addIP(to: tunnel.id, raw: draft, note: note)
        fieldError = error
        if error == nil {
            draft = ""
            note = ""
        }
    }
}

struct FlowAddresses: View {
    var addresses: [String]
    var remove: (String) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            ForEach(addresses, id: \.self) { address in
                HStack(spacing: 8) {
                    Text(address)
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(Theme.text)
                    Spacer()
                    Button {
                        remove(address)
                    } label: {
                        Image(systemName: "minus.circle")
                            .font(.system(size: 11))
                            .foregroundStyle(Theme.faint)
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .padding(.top, 2)
    }
}
