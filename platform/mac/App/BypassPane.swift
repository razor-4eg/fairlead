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
    @State private var sheet: Sheet?

    private enum Sheet: Identifiable {
        case add
        case list(UUID)
        case app(UUID)

        var id: String {
            switch self {
            case .add:
                return "add"
            case .list(let id):
                return "list-" + id.uuidString
            case .app(let id):
                return "app-" + id.uuidString
            }
        }
    }

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
                    ForEach(tunnel.routeLists) { list in
                        listRow(list)
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
                    sheet = .add
                }

                FairleadButton(title: "Add app", systemImage: "plus") {
                    model.addApplications(to: tunnel.id)
                }

                Spacer()
            }
        }
        .sheet(item: $sheet) { item in
            switch item {
            case .add:
                AddRouteSheet(tunnelID: tunnel.id)
                    .environmentObject(model)
            case .list(let id):
                RouteListSheet(tunnelID: tunnel.id, listID: id)
                    .environmentObject(model)
            case .app(let id):
                AppAddressesSheet(tunnelID: tunnel.id, ruleID: id)
                    .environmentObject(model)
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
                    .font(.system(size: 14, weight: .medium))
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
        .padding(11)
        .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var lanCaption: String {
        if live.lanCIDRs.isEmpty {
            return FairleadL10n.text("The current Wi-Fi or Ethernet network. It appears after you connect.")
        }

        return live.lanCIDRs.joined(separator: ", ")
    }

    private var listIsEmpty: Bool {
        tunnel.routeLists.isEmpty && tunnel.appRules.isEmpty
    }

    private var listNote: some View {
        Text("A domain is resolved on the regular network. An app's addresses are learned as it connects.")
            .font(.system(size: 12))
            .foregroundStyle(Theme.muted)
            .fixedSize(horizontal: false, vertical: true)
    }

/*
===============
listRow
===============
*/
    private func listRow(_ list: RouteList) -> some View {
        HStack(spacing: 8) {
            kindMark("List")
            VStack(alignment: .leading, spacing: 2) {
                Text(list.name)
                    .font(.system(size: 14, weight: .medium))
                    .foregroundStyle(list.enabled ? Theme.text : Theme.faint)
                Text(listCaption(list))
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.muted)
            }

            Spacer(minLength: 8)
            rowActions(
                view: { sheet = .list(list.id) },
                enabled: Binding(
                    get: { list.enabled },
                    set: { model.setRouteList(list.id, tunnel: tunnel.id, enabled: $0) }
                ),
                remove: { model.removeRouteList(list.id, tunnel: tunnel.id) }
            )
        }
        .padding(11)
        .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

/*
===============
appRow
===============
*/
    private func appRow(_ rule: AppRule) -> some View {
        let missing = !FileManager.default.fileExists(atPath: rule.path)
        return HStack(spacing: 8) {
            kindMark("App")
            HStack(spacing: 10) {
                Image(nsImage: NSWorkspace.shared.icon(forFile: rule.path))
                    .resizable()
                    .frame(width: 28, height: 28)
                    .opacity(missing ? 0.4 : 1)
                VStack(alignment: .leading, spacing: 2) {
                    Text(rule.name)
                        .font(.system(size: 14, weight: .medium))
                        .foregroundStyle(rule.enabled ? Theme.text : Theme.faint)
                    Text(missing ? FairleadL10n.text("App not found") : FairleadL10n.addressCount(rule.learned.count))
                        .font(.system(size: 12))
                        .foregroundStyle(missing ? Theme.danger : Theme.muted)
                }
            }

            Spacer(minLength: 8)
            rowActions(
                view: { sheet = .app(rule.id) },
                enabled: Binding(
                    get: { rule.enabled },
                    set: { model.setApp(rule.id, tunnel: tunnel.id, enabled: $0) }
                ),
                remove: { model.removeApp(rule.id, tunnel: tunnel.id) }
            )
        }
        .padding(11)
        .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

/*
===============
rowActions
===============
*/
    private func rowActions(view: @escaping () -> Void, enabled: Binding<Bool>, remove: @escaping () -> Void) -> some View {
        HStack(spacing: 8) {
            Button(action: view) {
                Text(FairleadL10n.text("View"))
                    .font(.system(size: 12, weight: .medium))
                    .foregroundStyle(Theme.accent)
            }
            .buttonStyle(.plain)
            FairleadSwitch(isOn: enabled)
            Button(action: remove) {
                Image(systemName: "xmark")
                    .font(.system(size: 10, weight: .bold))
                    .foregroundStyle(Theme.faint)
            }
            .buttonStyle(.plain)
        }
    }

/*
===============
listCaption
===============
*/
    private func listCaption(_ list: RouteList) -> String {
        let domains = list.entries.filter { $0.kind == .domain }.count
        let addresses = list.entries.filter { $0.kind == .ip }.count
        if domains > 0, addresses > 0 {
            return FairleadL10n.domainCount(domains) + ", " + FairleadL10n.addressCount(addresses)
        }
        if domains > 0 {
            return FairleadL10n.domainCount(domains)
        }
        return FairleadL10n.addressCount(addresses)
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
            .frame(width: 28, alignment: .leading)
    }
}

struct AddRouteSheet: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    var tunnelID: UUID
    @State private var name = ""
    @State private var text = ""
    @State private var failures: [String] = []
    @State private var progress = ""
    @State private var busy = false
    @State private var addTask: Task<Void, Never>?
    @FocusState private var focused: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Add route")
                .font(.system(size: 18, weight: .medium))
                .foregroundStyle(Theme.text)
            Text("One address or domain per line.")
                .font(.system(size: 12))
                .foregroundStyle(Theme.muted)
                .fixedSize(horizontal: false, vertical: true)
            TextField("Name", text: $name)
                .textFieldStyle(.plain)
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(Theme.text)
                .padding(10)
                .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Theme.line))
                .disabled(busy)
            ZStack(alignment: .topLeading) {
                TextEditor(text: $text)
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundStyle(Theme.text)
                    .scrollContentBackground(.hidden)
                    .focused($focused)
                    .padding(10)
                    .disabled(busy)
                if text.isEmpty {
                    Text("example.com\n203.0.113.10\n10.1.0.0/16")
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
                Text(progress)
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.muted)
                    .lineLimit(1)
                Spacer()
                FairleadButton(title: "Cancel") {
                    addTask?.cancel()
                    dismiss()
                }
                FairleadButton(title: "Add", prominent: canAdd) { commit() }
                    .disabled(!canAdd)
                    .opacity(canAdd ? 1 : 0.45)
            }
        }
        .padding(24)
        .frame(width: 480)
        .background(Theme.bg)
        .onAppear { focused = true }
        .onDisappear { addTask?.cancel() }
    }

    private var canAdd: Bool {
        !busy
            && !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            && !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

/*
===============
commit
===============
*/
    private func commit() {
        let listName = name
        let body = text
        busy = true
        failures = []
        progress = ""
        addTask?.cancel()
        addTask = Task {
            let problems = await model.addRouteList(to: tunnelID, name: listName, text: body) { message in
                progress = message
            }
            guard !Task.isCancelled else { return }
            busy = false
            progress = ""
            let kept = problems.filter { !$0.message.isEmpty }
            if kept.isEmpty {
                dismiss()
                return
            }

            text = problems.map(\.line).joined(separator: "\n")
            failures = kept.map { problem in
                problem.line.isEmpty ? problem.message : "\(problem.line): \(problem.message)"
            }
        }
    }
}

struct RouteListSheet: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    var tunnelID: UUID
    var listID: UUID
    @State private var name = ""
    @State private var text = ""
    @State private var failures: [String] = []
    @State private var progress = ""
    @State private var busy = false
    @State private var saveTask: Task<Void, Never>?
    @State private var loaded = false

    private var list: RouteList? {
        model.tunnels.first { $0.id == tunnelID }?.routeLists.first { $0.id == listID }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Edit list")
                .font(.system(size: 18, weight: .medium))
                .foregroundStyle(Theme.text)
            Text("One address or domain per line.")
                .font(.system(size: 12))
                .foregroundStyle(Theme.muted)
            TextField("Name", text: $name)
                .textFieldStyle(.plain)
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(Theme.text)
                .padding(10)
                .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Theme.line))
                .disabled(busy)
            TextEditor(text: $text)
                .font(.system(size: 13, design: .monospaced))
                .foregroundStyle(Theme.text)
                .scrollContentBackground(.hidden)
                .padding(10)
                .frame(height: 180)
                .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Theme.line))
                .disabled(busy)
            if !resolvedPreview.isEmpty {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Resolved")
                        .font(.system(size: 12, weight: .medium))
                        .foregroundStyle(Theme.muted)
                    ScrollView {
                        Text(resolvedPreview)
                            .font(.system(size: 12, design: .monospaced))
                            .foregroundStyle(Theme.muted)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .frame(height: 120)
                    .padding(10)
                    .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Theme.line))
                }
            }

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
                Text(progress)
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.muted)
                    .lineLimit(1)
                Spacer()
                FairleadButton(title: "Cancel") {
                    saveTask?.cancel()
                    dismiss()
                }
                FairleadButton(title: "Save", prominent: canSave) { commit() }
                    .disabled(!canSave)
                    .opacity(canSave ? 1 : 0.45)
            }
        }
        .padding(24)
        .frame(width: 480)
        .background(Theme.bg)
        .onAppear(perform: load)
        .onDisappear { saveTask?.cancel() }
    }

    private var canSave: Bool {
        !busy
            && !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            && !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    private var resolvedPreview: String {
        guard let list else { return "" }
        var lines: [String] = []
        for entry in list.entries where entry.kind == .domain {
            lines.append(entry.value)
            if entry.addresses.isEmpty {
                lines.append("  " + (entry.refreshAt == nil
                    ? FairleadL10n.text("Resolving…")
                    : FairleadL10n.format("Could not resolve %@.", entry.value)))
            } else {
                for address in entry.addresses {
                    lines.append("  " + address)
                }
            }
        }
        return lines.joined(separator: "\n")
    }

/*
===============
load
===============
*/
    private func load() {
        guard !loaded, let list else { return }
        loaded = true
        name = list.name
        text = list.entries.map(\.value).joined(separator: "\n")
    }

/*
===============
commit
===============
*/
    private func commit() {
        let listName = name
        let body = text
        busy = true
        failures = []
        progress = ""
        saveTask?.cancel()
        saveTask = Task {
            let problems = await model.updateRouteList(listID, tunnel: tunnelID, name: listName, text: body) { message in
                progress = message
            }
            guard !Task.isCancelled else { return }
            busy = false
            progress = ""
            let kept = problems.filter { !$0.message.isEmpty }
            if kept.isEmpty {
                dismiss()
                return
            }

            text = problems.map(\.line).joined(separator: "\n")
            failures = kept.map { problem in
                problem.line.isEmpty ? problem.message : "\(problem.line): \(problem.message)"
            }
        }
    }
}

struct AppAddressesSheet: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    var tunnelID: UUID
    var ruleID: UUID

    private var rule: AppRule? {
        model.tunnels.first { $0.id == tunnelID }?.appRules.first { $0.id == ruleID }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(rule?.name ?? "")
                .font(.system(size: 18, weight: .medium))
                .foregroundStyle(Theme.text)
            ScrollView {
                Text(bodyText)
                    .font(.system(size: 13, design: .monospaced))
                    .foregroundStyle(Theme.text)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(height: 280)
            .padding(10)
            .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Theme.line))
            HStack {
                Spacer()
                FairleadButton(title: "Close") { dismiss() }
            }
        }
        .padding(24)
        .frame(width: 480)
        .background(Theme.bg)
    }

    private var bodyText: String {
        guard let rule else { return "" }
        if rule.learned.isEmpty {
            return FairleadL10n.text("Nothing learned yet.")
        }
        return rule.learned.joined(separator: "\n")
    }
}
