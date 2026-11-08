//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// BypassPane.swift -- The route list and the window that adds one.
//
//===============================================================

import AppKit
import FairleadCore
import SwiftUI

struct BypassPane: View {
    @EnvironmentObject private var model: AppModel
    var tunnel: Tunnel
    @State private var addingRoutes = false
    @State private var openApps: Set<UUID> = []
    @State private var openDomains: Set<UUID> = []

    private var live: LiveTunnel { model.live(tunnel.id) }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 4) {
                    Text(LocalizedStringKey(tunnel.routePolicy == .selected ? "Through the tunnel" : "Direct"))
                        .font(.system(size: 18, weight: .medium))
                        .foregroundStyle(Theme.text)
                    Text(LocalizedStringKey(tunnel.routePolicy == .selected
                         ? "Only these addresses use the tunnel. Everything else stays on the regular network."
                         : "These addresses stay out of the tunnel and use the regular network."))
                        .font(.system(size: 12))
                        .foregroundStyle(Theme.muted)
                        .fixedSize(horizontal: false, vertical: true)
                }

                Spacer(minLength: 16)
                FairleadChoice(
                    selection: tunnel.routePolicy,
                    options: [
                        (.exceptions, "Direct"),
                        (.selected, "Through"),
                    ],
                    select: { model.setRoutePolicy(tunnel.id, $0) }
                )
            }

            lanRow
            listNote

            if listIsEmpty {
                Text("Nothing here yet. Add an IP, a domain, or an app.")
                    .font(.system(size: 13))
                    .foregroundStyle(Theme.muted)
                    .padding(.vertical, 8)
            } else {
                VStack(spacing: 8) {
                    ForEach(tunnel.ipRules) { rule in
                        ipRow(rule)
                    }

                    ForEach(tunnel.domainRules) { rule in
                        domainRow(rule)
                    }

                    ForEach(tunnel.appRules) { rule in
                        appRow(rule)
                    }
                }
            }

            if !live.warnings.isEmpty, live.phase == .connected {
                Text(live.warnings.joined(separator: " "))
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.muted)
            }

            HStack(spacing: 8) {
                FairleadButton(title: "Add route", systemImage: "plus") {
                    addingRoutes = true
                }

                FairleadButton(title: "Add app", systemImage: "plus") {
                    model.addApplications(to: tunnel.id)
                }

                Spacer()
            }
        }
        .sheet(isPresented: $addingRoutes) {
            AddRouteSheet(tunnelID: tunnel.id)
                .environmentObject(model)
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
            FairleadSwitch(isOn: Binding(
                get: { tunnel.excludeLAN },
                set: { model.setExcludeLAN(tunnel.id, $0) }
            ))
        }
        .padding(12)
        .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var lanCaption: String {
        if live.lanCIDRs.isEmpty {
            return FairleadL10n.text("The current Wi-Fi or Ethernet network. It appears after you connect.")
        }

        return live.lanCIDRs.joined(separator: ", ")
    }

    private var listIsEmpty: Bool {
        tunnel.ipRules.isEmpty && tunnel.domainRules.isEmpty && tunnel.appRules.isEmpty
    }

    private var listNote: some View {
        Text("A domain is resolved on the regular network. An app's addresses are learned as it connects. On macOS the route applies to every app.")
            .font(.system(size: 12))
            .foregroundStyle(Theme.muted)
            .fixedSize(horizontal: false, vertical: true)
    }

/*
===============
ipRow
===============
*/
    private func ipRow(_ rule: IPRule) -> some View {
        HStack(spacing: 12) {
            kindMark("IP")
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
            FairleadSwitch(isOn: Binding(
                get: { rule.enabled },
                set: { model.setIP(rule.id, tunnel: tunnel.id, enabled: $0) }
            ))
            Button {
                model.removeIP(rule.id, tunnel: tunnel.id)
            } label: {
                Image(systemName: "xmark")
                    .font(.system(size: 10, weight: .bold))
                    .foregroundStyle(Theme.faint)
            }
            .buttonStyle(.plain)
        }
        .padding(10)
        .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

/*
===============
domainRow
===============
*/
    private func domainRow(_ rule: DomainRule) -> some View {
        let expanded = openDomains.contains(rule.id)
        return VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 12) {
                kindMark("Domain")
                VStack(alignment: .leading, spacing: 2) {
                    Text(rule.name)
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(rule.enabled ? Theme.text : Theme.faint)
                    if !rule.note.isEmpty {
                        Text(rule.note)
                            .font(.system(size: 11))
                            .foregroundStyle(Theme.muted)
                    }

                    if !domainStatus(rule).isEmpty {
                        Text(domainStatus(rule))
                            .font(.system(size: 11))
                            .foregroundStyle(domainFailed(rule) ? Theme.danger : Theme.muted)
                    }
                }

                Spacer()
                FairleadSwitch(isOn: Binding(
                    get: { rule.enabled },
                    set: { model.setDomain(rule.id, tunnel: tunnel.id, enabled: $0) }
                ))
                Button {
                    model.removeDomain(rule.id, tunnel: tunnel.id)
                } label: {
                    Image(systemName: "xmark")
                        .font(.system(size: 10, weight: .bold))
                        .foregroundStyle(Theme.faint)
                }
                .buttonStyle(.plain)
            }

            if !rule.addresses.isEmpty {
                Button(FairleadL10n.text(expanded ? "Hide addresses" : "Show addresses")) {
                    if expanded { openDomains.remove(rule.id) } else { openDomains.insert(rule.id) }
                }
                .buttonStyle(.plain)
                .font(.system(size: 11, weight: .medium))
                .foregroundStyle(Theme.accent)
                if expanded {
                    VStack(alignment: .leading, spacing: 6) {
                        ForEach(rule.addresses, id: \.self) { address in
                            Text(address)
                                .font(.system(size: 11, design: .monospaced))
                                .foregroundStyle(Theme.text)
                        }
                    }
                    .padding(.top, 2)
                }
            }
        }
        .padding(10)
        .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

/*
===============
domainStatus
===============
*/
    private func domainStatus(_ rule: DomainRule) -> String {
        if !rule.addresses.isEmpty {
            return FairleadL10n.addressCount(rule.addresses.count)
        }

        if rule.enabled, rule.refreshAt == nil {
            return FairleadL10n.text("Resolving…")
        }

        if rule.enabled {
            return FairleadL10n.format("Could not resolve %@.", rule.name)
        }

        return ""
    }

/*
===============
domainFailed
===============
*/
    private func domainFailed(_ rule: DomainRule) -> Bool {
        rule.enabled && rule.addresses.isEmpty && rule.refreshAt != nil
    }

/*
===============
kindMark
===============
*/
    private func kindMark(_ title: LocalizedStringKey) -> some View {
        Text(title)
            .font(.system(size: 10, weight: .semibold))
            .foregroundStyle(Theme.muted)
            .frame(minWidth: 44, alignment: .leading)
    }

/*
===============
appRow
===============
*/
    private func appRow(_ rule: AppRule) -> some View {
        let missing = !FileManager.default.fileExists(atPath: rule.path)
        let expanded = openApps.contains(rule.id)
        return VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 10) {
                kindMark("App")
                Image(nsImage: NSWorkspace.shared.icon(forFile: rule.path))
                    .resizable()
                    .frame(width: 28, height: 28)
                    .opacity(missing ? 0.4 : 1)
                VStack(alignment: .leading, spacing: 2) {
                    Text(rule.name)
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(Theme.text)
                    Text(missing ? FairleadL10n.text("App not found") : FairleadL10n.addressCount(rule.learned.count))
                        .font(.system(size: 11))
                        .foregroundStyle(missing ? Theme.danger : Theme.muted)
                }

                Spacer()
                FairleadSwitch(
                    isOn: Binding(
                        get: { rule.learn },
                        set: { model.setApp(rule.id, tunnel: tunnel.id, learn: $0) }
                    ),
                    label: "Learn"
                )
                FairleadSwitch(isOn: Binding(
                    get: { rule.enabled },
                    set: { model.setApp(rule.id, tunnel: tunnel.id, enabled: $0) }
                ))
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
                Button(FairleadL10n.text(expanded ? "Hide addresses" : "Show addresses")) {
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

}

struct AddRouteSheet: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    var tunnelID: UUID
    @State private var text = ""
    @State private var failures: [String] = []
    @FocusState private var focused: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Add route")
                .font(.system(size: 18, weight: .medium))
                .foregroundStyle(Theme.text)
            Text("One address per line. Write a note after the address on the same line.")
                .font(.system(size: 12))
                .foregroundStyle(Theme.muted)
                .fixedSize(horizontal: false, vertical: true)
            ZStack(alignment: .topLeading) {
                TextEditor(text: $text)
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundStyle(Theme.text)
                    .scrollContentBackground(.hidden)
                    .focused($focused)
                    .padding(10)
                if text.isEmpty {
                    Text("example.com office\n203.0.113.10\n10.1.0.0/16 lab")
                        .font(.system(size: 13, design: .monospaced))
                        .foregroundStyle(Theme.faint)
                        .padding(.horizontal, 15)
                        .padding(.vertical, 18)
                        .allowsHitTesting(false)
                }
            }
            .frame(height: 180)
            .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Theme.line))
            if !failures.isEmpty {
                VStack(alignment: .leading, spacing: 4) {
                    ForEach(failures, id: \.self) { failure in
                        Text(failure)
                            .font(.system(size: 12))
                            .foregroundStyle(Theme.danger)
                    }
                }
            }

            HStack {
                Spacer()
                FairleadButton(title: "Cancel") { dismiss() }
                FairleadButton(title: "Add", prominent: canAdd) { commit() }
                    .disabled(!canAdd)
                    .opacity(canAdd ? 1 : 0.45)
            }
        }
        .padding(24)
        .frame(width: 480)
        .background(Theme.bg)
        .onAppear { focused = true }
    }

    private var canAdd: Bool {
        !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

/*
===============
commit
===============
*/
    private func commit() {
        let problems = model.addRoutes(to: tunnelID, text: text)
        if problems.isEmpty {
            dismiss()
            return
        }

        text = problems.map(\.line).joined(separator: "\n")
        failures = problems.map { "\($0.line): \($0.message)" }
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
