//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// SettingsSheet.swift -- Settings for login, language, the default tunnel, and helper.
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
                Text(FairleadL10n.text("Settings"))
                    .font(.system(size: 18, weight: .medium))
                    .foregroundStyle(Theme.text)
            }

            VStack(alignment: .leading, spacing: 16) {
                HStack(alignment: .center, spacing: 16) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(FairleadL10n.text("Open at login"))
                            .font(.system(size: 14, weight: .medium))
                            .foregroundStyle(Theme.text)
                        Text(FairleadL10n.text("Fairlead starts when you sign in. A default tunnel connects on its own."))
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

                HStack(alignment: .center, spacing: 16) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(FairleadL10n.text("Start hidden"))
                            .font(.system(size: 14, weight: .medium))
                            .foregroundStyle(Theme.text)
                        Text(FairleadL10n.text("Open in the menu bar only. Click the Fairlead icon to show the window."))
                            .font(.system(size: 12))
                            .foregroundStyle(Theme.muted)
                            .fixedSize(horizontal: false, vertical: true)
                    }

                    Spacer(minLength: 12)
                    FairleadSwitch(isOn: Binding(
                        get: { model.settings.startHidden },
                        set: { model.setStartHidden($0) }
                    ))
                }

                Rectangle().fill(Theme.line).frame(height: 1)

                VStack(alignment: .leading, spacing: 8) {
                    Text(FairleadL10n.text("Language"))
                        .font(.system(size: 14, weight: .medium))
                        .foregroundStyle(Theme.text)
                    Text(FairleadL10n.text("Language of the Fairlead interface. Auto follows the system language."))
                        .font(.system(size: 12))
                        .foregroundStyle(Theme.muted)
                        .fixedSize(horizontal: false, vertical: true)
                    FairleadSelect(
                        selection: model.settings.languageSelection,
                        options: [("auto", FairleadL10n.languageTitle("auto"))]
                            + FairleadL10n.languageCodes.map { ($0, FairleadL10n.languageTitle($0)) },
                        select: { model.setLanguage($0) }
                    )
                }

                Rectangle().fill(Theme.line).frame(height: 1)

                VStack(alignment: .leading, spacing: 8) {
                    Text(FairleadL10n.text("Default tunnel"))
                        .font(.system(size: 14, weight: .medium))
                        .foregroundStyle(Theme.text)
                    Text(FairleadL10n.text("Fairlead selects this tunnel when it opens and connects it."))
                        .font(.system(size: 12))
                        .foregroundStyle(Theme.muted)
                        .fixedSize(horizontal: false, vertical: true)
                    FairleadSelect(
                        selection: model.settings.defaultTunnelSelection,
                        options: [
                            ("", FairleadL10n.text("None")),
                            (AppSettings.lastActiveKey, FairleadL10n.text("Last active")),
                        ] + model.tunnels.map { ($0.id.uuidString, $0.name) },
                        select: { value in
                            model.setDefaultTunnel(value.isEmpty ? nil : value)
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

            HStack {
                FairleadButton(title: "Config folder") { model.openConfigFolder() }
                Spacer()
                FairleadButton(title: "Start helper", prominent: true) {
                    Task { await model.installHelper() }
                }
            }

            HStack {
                Spacer()
                FairleadButton(title: "Close") { dismiss() }
            }
        }
        .padding(24)
        .frame(width: 560)
        .background(Theme.bg)
        .onAppear { model.syncLoginItem() }
        .id(model.settings.languageSelection)
    }
}
