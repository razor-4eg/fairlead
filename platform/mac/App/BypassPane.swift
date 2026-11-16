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
                    Text(FairleadL10n.text(tunnel.routePolicy == .selected ? "Through the tunnel" : "Direct"))
                        .font(.system(size: 18, weight: .medium))
                        .foregroundStyle(Theme.text)
                    Text(FairleadL10n.text(tunnel.routePolicy == .selected
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
            quicRow
            listNote

            if listIsEmpty {
                Text(FairleadL10n.text("Nothing here yet. Add an IP, a domain, or an app."))
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
                AppPathSheet(tunnelID: tunnel.id, ruleID: id)
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
                Text(FairleadL10n.text("Local network directly"))
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

    private var quicRow: some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 3) {
                Text(FairleadL10n.text("Block browser QUIC"))
                    .font(.system(size: 14, weight: .medium))
                    .foregroundStyle(Theme.text)
                Text(FairleadL10n.text("Keeps HTTP/3 off for tunnel traffic so browsers recover after rule changes."))
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.muted)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer(minLength: 12)
            FairleadSwitch(isOn: Binding(
                get: { tunnel.blockQUIC },
                set: { model.setBlockQUIC(tunnel.id, $0) }
            ))
        }
        .padding(11)
        .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var lanCaption: String {
        if live.lanCIDRs.isEmpty {
            return FairleadL10n.text("Private LAN ranges stay off the tunnel when this is on.")
        }

        return live.lanCIDRs.joined(separator: ", ")
    }

    private var listIsEmpty: Bool {
        tunnel.routeLists.isEmpty && tunnel.appRules.isEmpty
    }

    private var listNote: some View {
        Text(FairleadL10n.text("IPs, domains, and apps for the Direct / Through mode."))
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
        HStack(spacing: 10) {
            ruleGlyph("list.bullet")
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
        return HStack(spacing: 10) {
            Image(nsImage: NSWorkspace.shared.icon(forFile: rule.path))
                .resizable()
                .frame(width: 28, height: 28)
                .opacity(missing ? 0.4 : 1)
            VStack(alignment: .leading, spacing: 2) {
                Text(rule.name)
                    .font(.system(size: 14, weight: .medium))
                    .foregroundStyle(rule.enabled ? Theme.text : Theme.faint)
                Text(missing ? FairleadL10n.text("App not found") : rule.path)
                    .font(.system(size: 12))
                    .foregroundStyle(missing ? Theme.danger : Theme.muted)
                    .lineLimit(1)
                    .truncationMode(.middle)
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
                Image(systemName: "pencil")
                    .font(.system(size: 10, weight: .bold))
                    .foregroundStyle(Theme.faint)
            }
            .buttonStyle(.plain)
            .help(FairleadL10n.text("Edit"))
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
ruleGlyph
Fixed 28×28 slot so list and app rows share the same leading column.
===============
*/
    private func ruleGlyph(_ systemName: String) -> some View {
        Image(systemName: systemName)
            .font(.system(size: 14, weight: .semibold))
            .foregroundStyle(Theme.muted)
            .frame(width: 28, height: 28)
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
            Text(FairleadL10n.text("Add route"))
                .font(.system(size: 18, weight: .medium))
                .foregroundStyle(Theme.text)
            Text(FairleadL10n.text("One address or domain per line."))
                .font(.system(size: 12))
                .foregroundStyle(Theme.muted)
                .fixedSize(horizontal: false, vertical: true)
            TextField(FairleadL10n.text("Name"), text: $name)
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
                    Text(FairleadL10n.text("example.com\n203.0.113.10\n10.1.0.0/16"))
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
            Text(FairleadL10n.text("Edit list"))
                .font(.system(size: 18, weight: .medium))
                .foregroundStyle(Theme.text)
            Text(FairleadL10n.text("One address or domain per line."))
                .font(.system(size: 12))
                .foregroundStyle(Theme.muted)
            TextField(FairleadL10n.text("Name"), text: $name)
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

struct AppPathSheet: View {
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
        rule?.path ?? ""
    }
}
