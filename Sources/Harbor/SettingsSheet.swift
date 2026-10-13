import HarborCore
import SwiftUI

struct SettingsSheet: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 22) {
            HStack(spacing: 10) {
                Mark(size: 18)
                Text("Settings")
                    .font(.system(size: 18, weight: .medium))
                    .foregroundStyle(Theme.text)
            }

            VStack(alignment: .leading, spacing: 16) {
                HStack(alignment: .center, spacing: 16) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Open at login")
                            .font(.system(size: 14, weight: .medium))
                            .foregroundStyle(Theme.text)
                        Text("Harbor starts when you sign in. A default tunnel connects on its own.")
                            .font(.system(size: 12))
                            .foregroundStyle(Theme.muted)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    Spacer(minLength: 12)
                    Toggle("", isOn: Binding(
                        get: { model.settings.launchAtLogin },
                        set: { model.setLaunchAtLogin($0) }
                    ))
                    .labelsHidden()
                    .toggleStyle(.switch)
                    .tint(Theme.accent)
                }

                if let message = model.loginItemMessage {
                    Text(message)
                        .font(.system(size: 12))
                        .foregroundStyle(Theme.danger)
                        .fixedSize(horizontal: false, vertical: true)
                }

                Rectangle().fill(Theme.line).frame(height: 1)

                VStack(alignment: .leading, spacing: 8) {
                    Text("Default tunnel")
                        .font(.system(size: 14, weight: .medium))
                        .foregroundStyle(Theme.text)
                    Text("Harbor selects this tunnel when it opens and connects it.")
                        .font(.system(size: 12))
                        .foregroundStyle(Theme.muted)
                        .fixedSize(horizontal: false, vertical: true)
                    Picker("", selection: Binding(
                        get: { model.settings.defaultTunnelID?.uuidString ?? "" },
                        set: { value in
                            model.setDefaultTunnel(value.isEmpty ? nil : UUID(uuidString: value))
                        }
                    )) {
                        Text("None").tag("")
                        ForEach(model.tunnels) { tunnel in
                            Text(tunnel.name).tag(tunnel.id.uuidString)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(.menu)
                    .frame(width: 220, alignment: .leading)
                }
            }
            .padding(16)
            .background(Theme.elevated, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 14, style: .continuous)
                    .strokeBorder(Theme.line)
            )

            components

            HStack {
                Spacer()
                HarborButton(title: "Close") { dismiss() }
            }
        }
        .padding(24)
        .frame(width: 560)
        .background(Theme.bg)
        .onAppear { model.syncLoginItem() }
    }

    private var components: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Components")
                .font(.system(size: 14, weight: .medium))
                .foregroundStyle(Theme.text)
            toolRow("wireguard-go", path: model.tools.wireguardGo)
            toolRow("Helper", path: model.helperOnline ? HarborPaths.socket : nil)
            Text("wireguard-go is included. The helper asks for an administrator password once, then brings tunnels and direct routes up itself. Configs with keys stay on this Mac.")
                .font(.system(size: 12))
                .foregroundStyle(Theme.muted)
                .fixedSize(horizontal: false, vertical: true)
            if !model.toolLog.isEmpty {
                ScrollView {
                    Text(model.toolLog)
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(Theme.muted)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(height: 72)
            }
            HStack {
                HarborButton(title: "Config folder") { model.openConfigFolder() }
                Spacer()
                HarborButton(title: "Start helper", prominent: true) {
                    Task { await model.installHelper() }
                }
            }
        }
        .padding(16)
        .background(Theme.elevated, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                .strokeBorder(Theme.line)
        )
    }

    private func toolRow(_ title: LocalizedStringKey, path: String?) -> some View {
        HStack(spacing: 10) {
            Circle()
                .fill(path == nil ? Theme.danger : Theme.accent)
                .frame(width: 6, height: 6)
            Text(title)
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(Theme.text)
                .frame(width: 120, alignment: .leading)
            Text(path ?? HarborL10n.text("not found"))
                .font(.system(size: 12, design: .monospaced))
                .foregroundStyle(Theme.muted)
                .lineLimit(1)
                .truncationMode(.middle)
            Spacer(minLength: 0)
        }
    }
}
