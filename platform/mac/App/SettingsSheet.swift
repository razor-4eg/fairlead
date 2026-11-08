//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// SettingsSheet.swift -- Settings for login, the default tunnel, and components.
//
//===============================================================

import FairleadCore
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
                        Text("Fairlead starts when you sign in. A default tunnel connects on its own.")
                            .font(.system(size: 12))
                            .foregroundStyle(Theme.muted)
                            .fixedSize(horizontal: false, vertical: true)
                    }

                    Spacer(minLength: 12)
                    FairleadSwitch(isOn: Binding(
                        get: { model.settings.launchAtLogin },
                        set: { model.setLaunchAtLogin($0) }
                    ))
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
                    Text("Fairlead selects this tunnel when it opens and connects it.")
                        .font(.system(size: 12))
                        .foregroundStyle(Theme.muted)
                        .fixedSize(horizontal: false, vertical: true)
                    FairleadSelect(
                        selection: model.settings.defaultTunnelID?.uuidString ?? "",
                        options: [("", FairleadL10n.text("None"))] + model.tunnels.map { ($0.id.uuidString, $0.name) },
                        select: { value in
                            model.setDefaultTunnel(value.isEmpty ? nil : UUID(uuidString: value))
                        }

                    )
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
                FairleadButton(title: "Close") { dismiss() }
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
            toolRow("amneziawg-go", path: model.tools.amneziaWG)
            toolRow("Helper", path: model.helperOnline ? FairleadPaths.socket : nil)
            Text("wireguard-go is included. The helper asks for an administrator password once, then brings tunnels and direct routes up itself. Configs with keys stay on this Mac.")
                .font(.system(size: 12))
                .foregroundStyle(Theme.muted)
                .fixedSize(horizontal: false, vertical: true)
            Text("AmneziaWG configs use the included amneziawg-go. Ordinary WireGuard configs stay on wireguard-go.")
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
                FairleadButton(title: "Config folder") { model.openConfigFolder() }
                Spacer()
                FairleadButton(title: "Start helper", prominent: true) {
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

/*
===============
toolRow
===============
*/
    private func toolRow(_ title: LocalizedStringKey, path: String?) -> some View {
        HStack(spacing: 10) {
            Circle()
                .fill(path == nil ? Theme.danger : Theme.accent)
                .frame(width: 6, height: 6)
            Text(title)
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(Theme.text)
                .frame(width: 132, alignment: .leading)
            Text(path ?? FairleadL10n.text("not found"))
                .font(.system(size: 12, design: .monospaced))
                .foregroundStyle(Theme.muted)
                .lineLimit(1)
                .truncationMode(.middle)
            Spacer(minLength: 0)
        }
    }
}
