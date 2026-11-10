//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// AppModel.swift -- Window state: the tunnel list, the connection, and the route rules.
//
//===============================================================

import AppKit
import Foundation
import FairleadCore
import SwiftUI
import UniformTypeIdentifiers

@MainActor
final class AppModel: ObservableObject {
    @Published var tunnels: [Tunnel] = []
    @Published var selection: UUID?
    @Published var lives: [UUID: LiveTunnel] = [:]
    @Published var helperOnline = false
    @Published var tools = ToolReport()
    @Published var banner: String?
    @Published var now = Date()
    @Published var query = ""
    @Published var toolLog = ""
    @Published var editing: Tunnel?
    @Published var showSettings = false
    @Published var settings = AppSettings.empty
    @Published var loginItemMessage: String?

    private let store = TunnelStore()
    private let settingsStore = SettingsStore()
    private var didAutoConnect = false
    private var pollTask: Task<Void, Never>?
    private var learnTask: Task<Void, Never>?
    private var saveTask: Task<Void, Never>?
    private var pushedRoutes: [String: [String]] = [:]
    private var helperStartTask: Task<Void, Never>?
    private var resolverCache: (at: Date, snap: ScutilDNS.Snapshot)?
    /// While bypass RPC runs, status polls only fill the socket backlog.
    private var routesBusy = 0
    /// While up/down RPC runs — not while we wait for a handshake after up returns.
    private var controlBusy = 0
    /// Open the main window once after launch when Start hidden is off.
    private var pendingMainWindow = false
    /// User (or Finder reopen after launch) asked for the main window.
    private var mainWindowRequested = false
    @Published var helperStale = false

    var selectedTunnel: Tunnel? {
        tunnels.first { $0.id == selection }
    }

    var filtered: [Tunnel] {
        let text = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return tunnels }
        return tunnels.filter {
            $0.name.localizedStandardContains(text) || $0.endpointLabel.localizedStandardContains(text)
        }
    }

/*
===============
init
===============
*/
    init() {
        // Restored windows reopen before Start hidden can take effect.
        UserDefaults.standard.set(false, forKey: "NSQuitAlwaysKeepsWindows")
        tunnels = store.load()
        settings = settingsStore.load()
        selection = preferredStartupSelection()
        pendingMainWindow = !settings.startHidden
        if settings.startHidden {
            clearSavedWindowState()
        }

        applySavedLoginItem()
        pollTask = Task { [weak self] in
            while !Task.isCancelled {
                await self?.refresh()
                try? await Task.sleep(nanoseconds: 1_000_000_000)
            }
        }

        learnTask = Task { [weak self] in
            while !Task.isCancelled {
                await self?.learnDestinations()
                await self?.refreshDomains()
                try? await Task.sleep(nanoseconds: 1_000_000_000)
            }
        }

        Task { [weak self] in
            guard let self else { return }
            // First install / helper update may need the password once. After that,
            // launchd KeepAlive keeps the helper up without prompting again.
            let needsInstall = HelperInstaller.needsUpdate()
                || !HelperInstaller.isInstalled()
                || !HelperInstaller.plistHasKeepAlive()
            await self.ensureHelper(interactive: needsInstall)
            await self.connectDefaultIfNeeded()
        }
    }

/*
===============
preferredStartupSelection
===============
*/
    private func preferredStartupSelection() -> UUID? {
        if let id = resolvedDefaultTunnelID(), tunnels.contains(where: { $0.id == id }) {
            return id
        }

        return tunnels.first?.id
    }

/*
===============
resolvedDefaultTunnelID
===============
*/
    private func resolvedDefaultTunnelID() -> UUID? {
        let key = settings.defaultTunnelSelection
        if key == AppSettings.lastActiveKey {
            return settings.lastActiveTunnelID
        }

        guard !key.isEmpty else { return nil }
        return UUID(uuidString: key)
    }

/*
===============
setLaunchAtLogin
===============
*/
    func setLaunchAtLogin(_ enabled: Bool) {
        do {
            try LoginItem.setEnabled(enabled)
            settings.launchAtLogin = enabled || LoginItem.isOn
            settingsStore.save(settings)
            syncLoginItem()
        } catch {
            loginItemMessage = error.localizedDescription
            settings.launchAtLogin = LoginItem.isOn
            settingsStore.save(settings)
        }
    }

/*
===============
syncLoginItem
===============
*/
    func syncLoginItem() {
        let approval = FairleadL10n.text("macOS still needs approval. Open System Settings → General → Login Items and allow Fairlead.")
        let status = LoginItem.status
        if status == .enabled {
            settings.launchAtLogin = true
            if loginItemMessage == approval {
                loginItemMessage = nil
            }
        } else if status == .requiresApproval {
            settings.launchAtLogin = true
            loginItemMessage = approval
        } else if status == .notRegistered || status == .notFound {
            settings.launchAtLogin = false
        }

        settingsStore.save(settings)
    }

/*
===============
setDefaultTunnel
`nil` / empty = none; `last` = last active; otherwise a tunnel id string.
===============
*/
    func setDefaultTunnel(_ key: String?) {
        let trimmed = key?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        if trimmed.isEmpty {
            settings.defaultTunnel = nil
        } else if trimmed == AppSettings.lastActiveKey {
            settings.defaultTunnel = AppSettings.lastActiveKey
        } else if let id = UUID(uuidString: trimmed), tunnels.contains(where: { $0.id == id }) {
            settings.defaultTunnel = id.uuidString
        } else {
            settings.defaultTunnel = nil
        }

        settingsStore.save(settings)
    }

/*
===============
setStartHidden
===============
*/
    func setStartHidden(_ enabled: Bool) {
        settings.startHidden = enabled
        settingsStore.save(settings)
    }

/*
===============
rememberLastActive
===============
*/
    private func rememberLastActive(_ id: UUID) {
        guard settings.lastActiveTunnelID != id else { return }
        settings.lastActiveTunnelID = id
        settingsStore.save(settings)
    }

    static let openMainNotification = Notification.Name("FairleadOpenMain")

/*
===============
prepareStartHidden
Call from applicationWillFinishLaunching so the Dock icon never appears.
===============
*/
    func prepareStartHidden() {
        guard settings.startHidden else { return }
        NSApp.setActivationPolicy(.accessory)
    }

/*
===============
finishStartHidden
Drop any window AppKit restored after launch.
===============
*/
    func finishStartHidden() {
        guard settings.startHidden, !mainWindowRequested else { return }
        pendingMainWindow = false
        for window in NSApp.windows where window.canBecomeMain || window.isVisible {
            window.isRestorable = false
            window.orderOut(nil)
            window.close()
        }
        NSApp.setActivationPolicy(.accessory)
    }

/*
===============
clearSavedWindowState
===============
*/
    private func clearSavedWindowState() {
        let base = NSHomeDirectory() + "/Library/Saved Application State/com.fairlead.client.savedState"
        try? FileManager.default.removeItem(atPath: base)
    }

/*
===============
requestOpenMainWindow
Ask the menu-bar scene to run openWindow — AppKit cannot open a SwiftUI Window alone.
===============
*/
    func requestOpenMainWindow() {
        mainWindowRequested = true
        pendingMainWindow = false
        NSApp.setActivationPolicy(.regular)
        if let window = NSApp.windows.first(where: { $0.canBecomeMain }) {
            window.isRestorable = false
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        NotificationCenter.default.post(name: Self.openMainNotification, object: nil)
    }

/*
===============
revealMainWindowAtLaunchIfNeeded
Window is not opened by default (so Start hidden stays invisible). Open it once
when the menu-bar item appears and the user did not ask to start hidden.
===============
*/
    func revealMainWindowAtLaunchIfNeeded(open: OpenWindowAction) {
        guard pendingMainWindow else { return }
        pendingMainWindow = false
        mainWindowRequested = true
        NSApp.setActivationPolicy(.regular)
        open(id: "main")
        NSApp.activate(ignoringOtherApps: true)
    }

/*
===============
shouldIgnoreReopen
Login / restoration can send reopen while Start hidden is on.
===============
*/
    func shouldIgnoreReopen() -> Bool {
        settings.startHidden && !mainWindowRequested
    }

/*
===============
applySavedLoginItem
===============
*/
    private func applySavedLoginItem() {
        guard settings.launchAtLogin else { return }
        if LoginItem.status == .enabled || LoginItem.status == .requiresApproval {
            syncLoginItem()
            return
        }

        do {
            try LoginItem.setEnabled(true)
        } catch {
            loginItemMessage = error.localizedDescription
        }

        syncLoginItem()
    }

/*
===============
connectDefaultIfNeeded
===============
*/
    private func connectDefaultIfNeeded() async {
        guard !didAutoConnect else { return }
        didAutoConnect = true
        guard let id = resolvedDefaultTunnelID(),
              let tunnel = tunnels.first(where: { $0.id == id }) else { return }
        selection = id
        guard live(id).phase == .idle || live(id).phase == .disconnecting else { return }
        await connect(tunnel)
    }

/*
===============
ensureHelper
Use the live helper when it answers. The administrator password is only for the
first install, or when this app ships a newer helper binary / LaunchDaemon.
Ordinary connect and disconnect never call osascript.
===============
*/
    func ensureHelper(interactive: Bool) async {
        if let helperStartTask {
            await helperStartTask.value
            return
        }

        let task = Task { @MainActor in
            await self.startHelper(interactive: interactive)
        }
        helperStartTask = task
        await task.value
        helperStartTask = nil
    }

/*
===============
startHelper
===============
*/
    private func startHelper(interactive: Bool) async {
        let mustInstall = HelperInstaller.needsUpdate()
            || !HelperInstaller.isInstalled()
            || !HelperInstaller.plistHasKeepAlive()

        if await pingHelper() {
            markHelperOnline()
            if mustInstall {
                helperStale = true
                if interactive {
                    banner = FairleadL10n.text("The running helper is an older copy. Fairlead will replace it.")
                    await replaceHelper()
                }
                return
            }
            helperStale = false
            return
        }

        helperOnline = false
        helperStale = mustInstall
        if mustInstall {
            if !interactive {
                return
            }
            if HelperInstaller.needsUpdate() || !HelperInstaller.plistHasKeepAlive() {
                banner = FairleadL10n.text("The running helper is an older copy. Fairlead will replace it.")
            }
            await replaceHelper()
            return
        }

        // Installed and current: launchd KeepAlive / RunAtLoad brings it back without a password.
        if await waitHelper(tries: 40) {
            helperStale = false
            banner = nil
            return
        }
        if interactive {
            banner = FairleadL10n.text("The helper is installed, but the socket is not responding yet.")
        }
    }

/*
===============
replaceHelper
Full privileged install. Password once per helper version.
===============
*/
    private func replaceHelper() async {
        if let error = await HelperInstaller.install() {
            banner = error
            helperOnline = false
            helperStale = HelperInstaller.needsUpdate() || !HelperInstaller.plistHasKeepAlive()
            return
        }
        if await waitHelper(tries: 40) {
            helperStale = false
            banner = nil
            return
        }
        helperOnline = false
        helperStale = HelperInstaller.needsUpdate() || !HelperInstaller.plistHasKeepAlive()
        banner = FairleadL10n.text("The helper is installed, but the socket is not responding yet.")
    }

/*
===============
pingHelper
===============
*/
    private func pingHelper() async -> Bool {
        let response = await rpc(RPCRequest(op: "ping"), timeout: 0.4)
        return response?.ok == true
    }

/*
===============
waitHelper
===============
*/
    private func waitHelper(tries: Int) async -> Bool {
        for _ in 0..<tries {
            try? await Task.sleep(nanoseconds: 150_000_000)
            if await pingHelper() {
                markHelperOnline()
                return true
            }
        }
        return false
    }

/*
===============
markHelperOnline
===============
*/
    private func markHelperOnline() {
        helperOnline = true
        if tools.amneziaWG == nil {
            tools = ToolLocator.locate()
        }
    }

/*
===============
live
===============
*/
    func live(_ id: UUID) -> LiveTunnel {
        lives[id] ?? LiveTunnel()
    }

/*
===============
createTunnel
===============
*/
    func createTunnel() {
        let number = tunnels.count + 1
        editing = Tunnel.blank(name: FairleadL10n.format("Tunnel %d", number))
    }

/*
===============
importFiles
===============
*/
    func importFiles(_ urls: [URL]) {
        var unsupported = false
        var unreadable = false
        for url in urls {
            guard let text = try? String(contentsOf: url, encoding: .utf8), !text.isEmpty else { continue }
            switch ConfigImport.normalize(text) {
            case .wireGuard(let config, let suggested):
                let trimmed = suggested?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                let name = trimmed.isEmpty ? url.deletingPathExtension().lastPathComponent : trimmed
                var tunnel = Tunnel(name: name, configText: config)
                if tunnels.contains(where: { $0.name == tunnel.name }) {
                    tunnel.name = "\(name) \(tunnels.count + 1)"
                }

                tunnels.append(tunnel)
                selection = tunnel.id
            case .unsupported:
                unsupported = true
            case .unreadable:
                unreadable = true
            }
        }

        if unsupported {
            banner = FairleadL10n.text("This Amnezia share is not AmneziaWG or WireGuard.")
        } else if unreadable {
            banner = FairleadL10n.text("The Amnezia share could not be read.")
        }

        persist()
    }

/*
===============
importPanel
===============
*/
    func importPanel() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [UTType(filenameExtension: "conf"), .json, .plainText].compactMap { $0 }
        panel.prompt = FairleadL10n.text("Import")
        guard panel.runModal() == .OK else { return }
        importFiles(panel.urls)
    }

/*
===============
export
===============
*/
    func export(_ tunnel: Tunnel) {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "conf") ?? .plainText]
        panel.allowsOtherFileTypes = false
        panel.isExtensionHidden = false
        // The panel appends .conf itself. A name that already ends in .conf would be saved as .conf.conf.
        panel.nameFieldStringValue = exportStem(tunnel.name)
        panel.prompt = FairleadL10n.text("Export")
        guard panel.runModal() == .OK, let url = panel.url else { return }
        try? tunnel.configText.write(to: confFileURL(url), atomically: true, encoding: .utf8)
    }

/*
===============
update
===============
*/
    func update(_ tunnel: Tunnel) {
        if let index = tunnels.firstIndex(where: { $0.id == tunnel.id }) {
            tunnels[index] = tunnel
            persist()
            if live(tunnel.id).phase == .connected {
                Task { await pushRoutes(for: tunnel) }
            }

            return
        }

        tunnels.append(tunnel)
        selection = tunnel.id
        persist()
    }

/*
===============
delete
===============
*/
    func delete(_ tunnel: Tunnel) {
        Task {
            if live(tunnel.id).phase == .connected || live(tunnel.id).phase == .connecting {
                await disconnect(tunnel)
            }

            tunnels.removeAll { $0.id == tunnel.id }
            lives[tunnel.id] = nil
            if settings.defaultTunnelSelection == tunnel.id.uuidString {
                setDefaultTunnel(nil)
            }

            if settings.lastActiveTunnelID == tunnel.id {
                settings.lastActiveTunnelID = nil
                settingsStore.save(settings)
            }

            if selection == tunnel.id {
                selection = tunnels.first?.id
            }

            persist()
        }
    }

/*
===============
toggle
===============
*/
    func toggle(_ id: UUID) {
        guard let tunnel = tunnels.first(where: { $0.id == id }) else { return }
        switch live(id).phase {
        case .connected, .connecting:
            Task { await disconnect(tunnel) }
        case .idle, .disconnecting:
            Task { await connect(tunnel) }
        }
    }

/*
===============
connect
===============
*/
    func connect(_ tunnel: Tunnel) async {
        let issues = ConfigCheck.issues(in: tunnel.document)
        if let issue = issues.first {
            banner = issue.message
            editing = tunnel
            return
        }

        for other in tunnels where other.id != tunnel.id && isUp(other.id) {
            await disconnect(other)
        }

        await ensureHelper(interactive: true)
        if !helperOnline || HelperInstaller.needsUpdate() {
            mutate(tunnel.id) { $0.phase = .idle }
            if banner == nil {
                banner = HelperInstaller.needsUpdate()
                    ? FairleadL10n.text("The running helper is an older copy. Fairlead will replace it.")
                    : FairleadL10n.text("The helper did not respond. Start it and try again.")
            }
            showSettings = true
            return
        }

        await rememberResolvers()
        mutate(tunnel.id) { $0.phase = .connecting }
        banner = nil
        controlBusy += 1
        let response = await rpc(
            RPCRequest(
                op: "up",
                name: tunnel.slug,
                config: tunnel.configText,
                cidrs: tunnel.directCIDRs(),
                excludeLAN: tunnel.excludeLAN,
                policy: tunnel.routePolicy.rawValue
            ),
            timeout: 90
        )
        controlBusy = max(0, controlBusy - 1)
        guard let response else {
            mutate(tunnel.id) { $0.phase = .idle }
            helperOnline = false
            banner = FairleadL10n.text("The helper did not respond. Start it and try again.")
            showSettings = true
            return
        }

        if let managed = response.managed?.first(where: { $0.name == tunnel.slug }) {
            // Interface is up, but WireGuard may not have shaken hands yet.
            apply(managed, to: tunnel.id, phase: .connecting)
            pushedRoutes[tunnel.slug] = managed.bypass
            rememberLastActive(tunnel.id)
            if let devices = response.devices {
                absorb(devices, interface: managed.realInterface, id: tunnel.id)
                promoteIfHandshaken(tunnel.id)
            }
        } else {
            mutate(tunnel.id) { $0.phase = .idle }
        }

        if let warnings = response.warnings, !warnings.isEmpty {
            mutate(tunnel.id) { $0.warnings = warnings.map(FairleadL10n.display) }
        }

        noteRouteFailure(response.error, ok: response.ok, tunnel: tunnel)
        await refresh()
    }

/*
===============
shutdown
Bring every tunnel down before the app exits. The helper stays under launchd
(KeepAlive) so the next launch does not need the administrator password.
===============
*/
    func shutdown() {
        guard helperOnline || FileManager.default.fileExists(atPath: FairleadPaths.socket) else {
            helperOnline = false
            return
        }
        if let status = try? RPCClient.call(RPCRequest(op: "status"), timeout: 0.8), status.ok {
            for tunnel in status.managed ?? [] {
                _ = try? RPCClient.call(RPCRequest(op: "down", name: tunnel.name), timeout: 8)
            }
        }
        helperOnline = false
    }

/*
===============
noteRouteFailure
===============
*/
    private func noteRouteFailure(_ error: String?, ok: Bool, tunnel: Tunnel) {
        guard let error, !ok else { return }
        if tunnel.directCIDRs().isEmpty,
           error.contains("direct routes were not applied") || error.contains("прямых маршрутов") {
            return
        }

        banner = FairleadL10n.display(error)
    }

/*
===============
disconnect
===============
*/
    func disconnect(_ tunnel: Tunnel) async {
        mutate(tunnel.id) { $0.phase = .disconnecting }
        controlBusy += 1
        let response = await rpc(RPCRequest(op: "down", name: tunnel.slug), timeout: 60)
        controlBusy = max(0, controlBusy - 1)
        mutate(tunnel.id) { live in
            live.phase = .idle
            live.rateDown = 0
            live.rateUp = 0
            live.realInterface = nil
            live.handshake = nil
        }

        pushedRoutes[tunnel.slug] = nil
        if response == nil {
            helperOnline = false
            banner = FairleadL10n.text("Could not reach the helper to disconnect.")
        } else if let error = response?.error, response?.ok == false {
            banner = FairleadL10n.display(error)
        }

        await refresh()
    }

/*
===============
addRouteList
Read one named list. A line is an address or a domain. Resolves domains before the list is stored.
===============
*/
    func addRouteList(to id: UUID, name: String, text: String, progress: @escaping (String) -> Void) async -> [(line: String, message: String)] {
        let listName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let tunnel = tunnels.first(where: { $0.id == id }), !listName.isEmpty else { return [] }
        var failures: [(line: String, message: String)] = []
        var entries: [RouteEntry] = []
        var seen = Set(tunnel.routeLists.flatMap { list in list.entries.map { "\($0.kind.rawValue) \($0.value)" } })
        var reportedCap = false
        let cap = 4096
        for rawLine in text.split(whereSeparator: \.isNewline) {
            if Task.isCancelled { return [] }
            let line = rawLine.trimmingCharacters(in: .whitespaces)
            if line.isEmpty { continue }
            if entries.count >= cap {
                if !reportedCap {
                    failures.append((line, FairleadL10n.text("A list can hold 4096 entries.")))
                    reportedCap = true
                } else {
                    failures.append((line, ""))
                }
                continue
            }
            if line.contains(where: { $0.isWhitespace }) {
                failures.append((line, FairleadL10n.text("Enter an IP, a network, or a domain.")))
                continue
            }
            if let cidr = CIDR.parse(line) {
                if cidr.prefix == 0 {
                    failures.append((line, FairleadL10n.text("That network is too broad and would disable the whole tunnel.")))
                } else if !seen.insert("ip \(cidr.string)").inserted {
                    failures.append((line, FairleadL10n.text("That address is already listed.")))
                } else {
                    entries.append(RouteEntry(kind: .ip, value: cidr.string))
                }
            } else if line.contains("*") {
                failures.append((line, FairleadL10n.text("Wildcards such as *.example.com are not supported.")))
            } else if let domain = DomainName.normalize(line) {
                if !seen.insert("domain \(domain)").inserted {
                    failures.append((line, FairleadL10n.text("That domain is already listed.")))
                } else {
                    entries.append(RouteEntry(kind: .domain, value: domain))
                }
            } else {
                failures.append((line, FairleadL10n.text("Enter an IP, a network, or a domain.")))
            }
        }

        if entries.isEmpty {
            if failures.isEmpty {
                failures.append(("", FairleadL10n.text("Enter an IP, a network, or a domain.")))
            }
            return failures
        }

        let domainIndexes = entries.indices.filter { entries[$0].kind == .domain }
        if domainIndexes.isEmpty {
            progress(FairleadL10n.text("Adding the list"))
            await Task.yield()
        } else {
            progress(FairleadL10n.format("Resolving %d of %d", 1, domainIndexes.count))
            await Task.yield()
            if Task.isCancelled { return [] }
            await rememberResolvers()
            let target = domainLookupTarget(id)
            let skip = Set(live(id).endpointIPs)
            if !target.servers.isEmpty {
                for (offset, index) in domainIndexes.enumerated() {
                    if Task.isCancelled { return [] }
                    progress(FairleadL10n.format("Resolving %d of %d", offset + 1, domainIndexes.count))
                    let host = entries[index].value
                    let answer = await Task.detached(priority: .utility) {
                        DomainLookup.resolve(name: host, servers: target.servers, interface: target.interface)
                    }.value
                    if Task.isCancelled { return [] }
                    if let answer {
                        entries[index].addresses = mergedDomainAddresses(existing: [], answer: answer, skip: skip)
                        entries[index].refreshAt = Date().addingTimeInterval(TimeInterval(answer.ttl))
                    } else {
                        entries[index].refreshAt = Date().addingTimeInterval(30)
                    }
                }
            }
        }

        if Task.isCancelled { return [] }
        progress(FairleadL10n.text("Adding the list"))
        guard var current = tunnels.first(where: { $0.id == id }) else { return failures }
        current.routeLists.append(RouteList(name: listName, entries: entries))
        replace(current)
        return failures
    }

/*
===============
removeRouteList
===============
*/
    func removeRouteList(_ listID: UUID, tunnel id: UUID) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        tunnel.routeLists.removeAll { $0.id == listID }
        replace(tunnel)
    }

/*
===============
setRouteList
===============
*/
    func setRouteList(_ listID: UUID, tunnel id: UUID, enabled: Bool) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        guard let index = tunnel.routeLists.firstIndex(where: { $0.id == listID }) else { return }
        tunnel.routeLists[index].enabled = enabled
        replace(tunnel)
    }

/*
===============
updateRouteList
Replace one list's name and entries. Keeps resolved addresses for unchanged domains.
===============
*/
    func updateRouteList(
        _ listID: UUID,
        tunnel id: UUID,
        name: String,
        text: String,
        progress: @escaping (String) -> Void
    ) async -> [(line: String, message: String)] {
        let listName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let tunnel = tunnels.first(where: { $0.id == id }),
              let existing = tunnel.routeLists.first(where: { $0.id == listID }),
              !listName.isEmpty else { return [] }

        let keptAddresses = Dictionary(uniqueKeysWithValues:
            existing.entries
                .filter { $0.kind == .domain }
                .map { ($0.value.lowercased(), ($0.addresses, $0.refreshAt)) }
        )
        var failures: [(line: String, message: String)] = []
        var entries: [RouteEntry] = []
        var seen = Set(
            tunnel.routeLists
                .filter { $0.id != listID }
                .flatMap { list in list.entries.map { "\($0.kind.rawValue) \($0.value)" } }
        )
        var reportedCap = false
        let cap = 4096
        for rawLine in text.split(whereSeparator: \.isNewline) {
            if Task.isCancelled { return [] }
            let line = rawLine.trimmingCharacters(in: .whitespaces)
            if line.isEmpty { continue }
            if entries.count >= cap {
                if !reportedCap {
                    failures.append((line, FairleadL10n.text("A list can hold 4096 entries.")))
                    reportedCap = true
                } else {
                    failures.append((line, ""))
                }
                continue
            }
            if line.contains(where: { $0.isWhitespace }) {
                failures.append((line, FairleadL10n.text("Enter an IP, a network, or a domain.")))
                continue
            }
            if let cidr = CIDR.parse(line) {
                if cidr.prefix == 0 {
                    failures.append((line, FairleadL10n.text("That network is too broad and would disable the whole tunnel.")))
                } else if !seen.insert("ip \(cidr.string)").inserted {
                    failures.append((line, FairleadL10n.text("That address is already listed.")))
                } else {
                    entries.append(RouteEntry(kind: .ip, value: cidr.string))
                }
            } else if line.contains("*") {
                failures.append((line, FairleadL10n.text("Wildcards such as *.example.com are not supported.")))
            } else if let domain = DomainName.normalize(line) {
                if !seen.insert("domain \(domain)").inserted {
                    failures.append((line, FairleadL10n.text("That domain is already listed.")))
                } else if let kept = keptAddresses[domain] {
                    entries.append(RouteEntry(kind: .domain, value: domain, addresses: kept.0, refreshAt: kept.1))
                } else {
                    entries.append(RouteEntry(kind: .domain, value: domain))
                }
            } else {
                failures.append((line, FairleadL10n.text("Enter an IP, a network, or a domain.")))
            }
        }

        if entries.isEmpty {
            if failures.isEmpty {
                failures.append(("", FairleadL10n.text("Enter an IP, a network, or a domain.")))
            }
            return failures
        }

        let domainIndexes = entries.indices.filter {
            entries[$0].kind == .domain && entries[$0].addresses.isEmpty
        }
        if domainIndexes.isEmpty {
            progress(FairleadL10n.text("Saving the list"))
            await Task.yield()
        } else {
            progress(FairleadL10n.format("Resolving %d of %d", 1, domainIndexes.count))
            await Task.yield()
            if Task.isCancelled { return [] }
            await rememberResolvers()
            let target = domainLookupTarget(id)
            let skip = Set(live(id).endpointIPs)
            if !target.servers.isEmpty {
                for (offset, index) in domainIndexes.enumerated() {
                    if Task.isCancelled { return [] }
                    progress(FairleadL10n.format("Resolving %d of %d", offset + 1, domainIndexes.count))
                    let host = entries[index].value
                    let answer = await Task.detached(priority: .utility) {
                        DomainLookup.resolve(name: host, servers: target.servers, interface: target.interface)
                    }.value
                    if Task.isCancelled { return [] }
                    if let answer {
                        entries[index].addresses = mergedDomainAddresses(existing: [], answer: answer, skip: skip)
                        entries[index].refreshAt = Date().addingTimeInterval(TimeInterval(answer.ttl))
                    } else {
                        entries[index].refreshAt = Date().addingTimeInterval(30)
                    }
                }
            }
        }

        if Task.isCancelled { return [] }
        progress(FairleadL10n.text("Saving the list"))
        guard var current = tunnels.first(where: { $0.id == id }),
              let index = current.routeLists.firstIndex(where: { $0.id == listID }) else { return failures }
        current.routeLists[index].name = listName
        current.routeLists[index].entries = entries
        replace(current)
        return failures
    }

/*
===============
addApplications
===============
*/
    func addApplications(to id: UUID) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [.application]
        panel.directoryURL = URL(fileURLWithPath: "/Applications")
        panel.prompt = FairleadL10n.text("Add")
        guard panel.runModal() == .OK else { return }
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        for url in panel.urls {
            let path = url.path
            if tunnel.appRules.contains(where: { $0.path == path }) { continue }
            let bundle = Bundle(url: url)
            let name = (bundle?.object(forInfoDictionaryKey: "CFBundleDisplayName") as? String)
                ?? (bundle?.object(forInfoDictionaryKey: "CFBundleName") as? String)
                ?? url.deletingPathExtension().lastPathComponent
            let identifier = bundle?.bundleIdentifier ?? ""
            tunnel.appRules.append(AppRule(path: path, bundleIdentifier: identifier, name: name))
        }

        replace(tunnel)
    }

/*
===============
removeApp
===============
*/
    func removeApp(_ ruleID: UUID, tunnel id: UUID) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        tunnel.appRules.removeAll { $0.id == ruleID }
        replace(tunnel)
    }

/*
===============
setApp
===============
*/
    func setApp(_ ruleID: UUID, tunnel id: UUID, enabled: Bool? = nil, learn: Bool? = nil) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        guard let index = tunnel.appRules.firstIndex(where: { $0.id == ruleID }) else { return }
        if let enabled { tunnel.appRules[index].enabled = enabled }
        if let learn { tunnel.appRules[index].learn = learn }
        replace(tunnel)
    }

/*
===============
forget
===============
*/
    func forget(_ address: String, app ruleID: UUID, tunnel id: UUID) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        guard let index = tunnel.appRules.firstIndex(where: { $0.id == ruleID }) else { return }
        tunnel.appRules[index].learned.removeAll { $0 == address }
        replace(tunnel)
    }

/*
===============
setExcludeLAN
===============
*/
    func setExcludeLAN(_ id: UUID, _ enabled: Bool) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        tunnel.excludeLAN = enabled
        replace(tunnel)
    }

/*
===============
setRoutePolicy
===============
*/
    func setRoutePolicy(_ id: UUID, _ policy: RoutePolicy) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        tunnel.routePolicy = policy
        replace(tunnel)
    }

/*
===============
installHelper
Settings button: install or restart the helper, allowing the password prompt.
===============
*/
    func installHelper() async {
        banner = nil
        await ensureHelper(interactive: true)
        if helperOnline {
            banner = nil
        } else if banner == nil {
            banner = FairleadL10n.text("The helper is installed, but the socket is not responding yet.")
        }
    }

/*
===============
openConfigFolder
===============
*/
    func openConfigFolder() {
        NSWorkspace.shared.open(store.folder)
    }

/*
===============
menuTitle
===============
*/
    func menuTitle(_ tunnel: Tunnel) -> String {
        let mark = live(tunnel.id).phase == .connected ? "● " : ""
        return mark + tunnel.name
    }

/*
===============
openMainWindow
===============
*/
    func openMainWindow() {
        requestOpenMainWindow()
    }

    var anyConnected: Bool {
        tunnels.contains { lives[$0.id]?.phase == .connected }
    }

/*
===============
replace
===============
*/
    private func replace(_ tunnel: Tunnel) {
        guard let index = tunnels.firstIndex(where: { $0.id == tunnel.id }) else { return }
        tunnels[index] = tunnel
        persist()
        if isUp(tunnel.id) {
            Task { await pushRoutes(for: tunnel) }
        }
    }

/*
===============
pushRoutes
===============
*/
    private func pushRoutes(for tunnel: Tunnel) async {
        routesBusy += 1
        defer { routesBusy = max(0, routesBusy - 1) }
        // The shared planner caps applied routes; sending thousands only slows the helper.
        let cidrs = Array(tunnel.directCIDRs().prefix(128))
        let response = await rpc(
            RPCRequest(
                op: "bypass",
                name: tunnel.slug,
                cidrs: cidrs,
                excludeLAN: tunnel.excludeLAN,
                policy: tunnel.routePolicy.rawValue
            ),
            timeout: 120
        )
        guard let response else { return }
        if let managed = response.managed?.first(where: { $0.name == tunnel.slug }) {
            // Keep Connecting until a real handshake — bypass must not claim Connected.
            let phase = live(tunnel.id).phase
            apply(managed, to: tunnel.id, phase: phase == .idle ? .connecting : phase)
            pushedRoutes[tunnel.slug] = managed.bypass
            promoteIfHandshaken(tunnel.id)
        }

        if let warnings = response.warnings {
            mutate(tunnel.id) { $0.warnings = warnings.map(FairleadL10n.display) }
        }

        noteRouteFailure(response.error, ok: response.ok, tunnel: tunnel)
    }

/*
===============
learnDestinations
===============
*/
    private func learnDestinations() async {
        let snapshot = tunnels
        guard snapshot.contains(where: { $0.appRules.contains(where: \.enabled) }) else { return }
        var skip = Set<String>()
        for tunnel in snapshot {
            skip.formUnion(live(tunnel.id).endpointIPs)
        }

        let rules = snapshot.flatMap(\.appRules)
        let found = await Task.detached(priority: .utility) {
            AppTraffic.scan(rules: rules, skip: skip)
        }.value
        guard !found.isEmpty else { return }

        var changed: [Tunnel] = []
        for index in tunnels.indices {
            var tunnel = tunnels[index]
            var tunnelChanged = false
            for ruleIndex in tunnel.appRules.indices {
                guard tunnel.appRules[ruleIndex].enabled else { continue }
                guard let additions = found[tunnel.appRules[ruleIndex].id] else { continue }
                var learned = tunnel.appRules[ruleIndex].learned
                for ip in additions where !learned.contains(ip) && learned.count < 400 {
                    if let parsed = IPAddress.parse(ip) {
                        learned.append(parsed.string)
                    }
                }

                if learned != tunnel.appRules[ruleIndex].learned {
                    tunnel.appRules[ruleIndex].learned = learned
                    tunnelChanged = true
                }
            }

            if tunnelChanged {
                tunnels[index] = tunnel
                changed.append(tunnel)
            }
        }

        guard !changed.isEmpty else { return }
        persist(immediate: false)
        for tunnel in changed where isUp(tunnel.id) {
            await pushRoutes(for: tunnel)
        }
    }

    private struct DomainJob: Sendable {
        var id: UUID
        var name: String
        var servers: [String]
        var interface: String?
    }

/*
===============
domainLookupTarget
DNS servers for one tunnel. A connected tunnel stays off its own utun.
===============
*/
    private func domainLookupTarget(_ id: UUID) -> (servers: [String], interface: String?) {
        let state = live(id)
        var servers = state.resolvers
        var iface: String?
        if state.phase == .connected || state.phase == .connecting {
            if servers.isEmpty, let gateway = state.gateway, let ip = IPAddress.parse(gateway) {
                servers = [ip.string]
            }

            iface = state.gatewayIface ?? state.resolverIface
            if let name = iface, name.hasPrefix("utun") { iface = state.resolverIface }
            if let name = iface, name.hasPrefix("utun") { iface = nil }
        }

        return (servers, iface)
    }

/*
===============
refreshDomains
===============
*/
    private func refreshDomains() async {
        let now = Date()
        let due = tunnels.contains { tunnel in
            tunnel.routeLists.contains { list in
                list.enabled && list.entries.contains { entry in
                    entry.kind == .domain && (entry.refreshAt == nil || (entry.refreshAt ?? now) <= now)
                }
            }
        }

        guard due else { return }
        await rememberResolvers()
        var jobs: [DomainJob] = []
        for tunnel in tunnels {
            let target = domainLookupTarget(tunnel.id)
            guard !target.servers.isEmpty else { continue }
            for list in tunnel.routeLists where list.enabled {
                for entry in list.entries where entry.kind == .domain {
                    let ready = entry.refreshAt == nil || (entry.refreshAt ?? now) <= now
                    guard ready else { continue }
                    jobs.append(DomainJob(id: entry.id, name: entry.value, servers: target.servers, interface: target.interface))
                }
            }
        }

        guard !jobs.isEmpty else { return }
        let answers = await Task.detached(priority: .utility) {
            var map: [UUID: DomainLookup.Answer] = [:]
            for job in jobs {
                if let answer = DomainLookup.resolve(name: job.name, servers: job.servers, interface: job.interface) {
                    map[job.id] = answer
                }
            }

            return map
        }.value

        var changed = false
        var push: [Tunnel] = []
        let asked = Set(jobs.map(\.id))
        for index in tunnels.indices {
            var tunnel = tunnels[index]
            var tunnelChanged = false
            var addressesChanged = false
            let skip = Set(live(tunnel.id).endpointIPs)
            for listIndex in tunnel.routeLists.indices where tunnel.routeLists[listIndex].enabled {
                for entryIndex in tunnel.routeLists[listIndex].entries.indices {
                    let entryID = tunnel.routeLists[listIndex].entries[entryIndex].id
                    guard asked.contains(entryID) else { continue }
                    if let answer = answers[entryID] {
                        let next = mergedDomainAddresses(
                            existing: tunnel.routeLists[listIndex].entries[entryIndex].addresses,
                            answer: answer,
                            skip: skip
                        )
                        if next != tunnel.routeLists[listIndex].entries[entryIndex].addresses {
                            tunnel.routeLists[listIndex].entries[entryIndex].addresses = next
                            addressesChanged = true
                        }

                        tunnel.routeLists[listIndex].entries[entryIndex].refreshAt = now.addingTimeInterval(TimeInterval(answer.ttl))
                    } else {
                        tunnel.routeLists[listIndex].entries[entryIndex].refreshAt = now.addingTimeInterval(30)
                    }

                    tunnelChanged = true
                }
            }

            if tunnelChanged {
                tunnels[index] = tunnel
                changed = true
            }

            if addressesChanged {
                push.append(tunnel)
            }
        }

        if changed { persist(immediate: false) }
        for tunnel in push where isUp(tunnel.id) {
            await pushRoutes(for: tunnel)
        }
    }

/*
===============
mergedDomainAddresses
===============
*/
    private func mergedDomainAddresses(existing: [String], answer: DomainLookup.Answer, skip: Set<String>) -> [String] {
        var v4 = existing.filter { IPAddress.parse($0)?.family == .v4 }
        var v6 = existing.filter { IPAddress.parse($0)?.family == .v6 }
        if let fresh = answer.v4 { v4 = fresh }
        if let fresh = answer.v6 { v6 = fresh }
        var seen = Set<String>()
        return (v4 + v6).filter { address in
            guard !skip.contains(address), seen.insert(address).inserted else { return false }
            return true
        }.sorted()
    }

/*
===============
rememberResolvers
===============
*/
    private func rememberResolvers() async {
        if tunnels.contains(where: { isUp($0.id) }) { return }
        let now = Date()
        let snap: ScutilDNS.Snapshot
        if let resolverCache, now.timeIntervalSince(resolverCache.at) < 5 {
            snap = resolverCache.snap
        } else {
            snap = await Task.detached(priority: .utility) {
                SystemDNS.current()
            }.value
            resolverCache = (now, snap)
        }

        guard !snap.servers.isEmpty else { return }
        for tunnel in tunnels where live(tunnel.id).phase == .idle {
            mutate(tunnel.id) { live in
                live.resolvers = snap.servers
                live.resolverIface = snap.interface
            }
        }
    }

/*
===============
refresh
===============
*/
    private func refresh() async {
        now = Date()
        // Skip only while a control/bypass RPC holds the helper. Waiting for a
        // handshake after up must still poll status.
        if routesBusy > 0 || controlBusy > 0 {
            return
        }

        // When the helper is down, fail fast so start and quit are not blocked.
        let response = await rpc(RPCRequest(op: "status"), timeout: helperOnline ? 3 : 0.2)
        guard let response, response.ok else {
            helperOnline = false
            if tools.amneziaWG == nil {
                tools = ToolLocator.locate()
            }

            return
        }

        markHelperOnline()
        if let report = response.tools {
            tools = report
        }

        let managed = response.managed ?? []
        let devices = response.devices ?? []
        for tunnel in tunnels {
            let phase = live(tunnel.id).phase
            if phase == .disconnecting {
                continue
            }

            if let item = managed.first(where: { $0.name == tunnel.slug }) {
                absorb(devices, interface: item.realInterface, id: tunnel.id)
                let next: Phase = live(tunnel.id).handshake != nil ? .connected : .connecting
                apply(item, to: tunnel.id, phase: next)
            } else if phase == .connected || phase == .connecting {
                mutate(tunnel.id) {
                    $0.phase = .idle
                    $0.rateDown = 0
                    $0.rateUp = 0
                    $0.handshake = nil
                    $0.realInterface = nil
                }
            }
        }
    }

/*
===============
promoteIfHandshaken
Connected only after WireGuard has a real handshake — not merely an up interface.
===============
*/
    private func promoteIfHandshaken(_ id: UUID) {
        guard live(id).phase == .connecting, live(id).handshake != nil else { return }
        mutate(id) { $0.phase = .connected }
    }

/*
===============
absorb
===============
*/
    private func absorb(_ devices: [WGDeviceStatus], interface: String, id: UUID) {
        guard let device = devices.first(where: { $0.interface == interface }) else { return }
        let peer = device.peers.max { $0.handshake < $1.handshake }
        mutate(id) { live in
            let rx = peer.map { $0.rx } ?? live.rx
            let tx = peer.map { $0.tx } ?? live.tx
            let now = Date()
            if let previous = live.sampleDate {
                let delta = now.timeIntervalSince(previous)
                if delta > 0.2, rx >= live.sampleRx, tx >= live.sampleTx {
                    live.rateDown = Double(rx - live.sampleRx) / delta
                    live.rateUp = Double(tx - live.sampleTx) / delta
                }
            }

            live.sampleDate = now
            live.sampleRx = rx
            live.sampleTx = tx
            live.rx = rx
            live.tx = tx
            if let handshake = peer?.handshake, handshake > 0 {
                live.handshake = Date(timeIntervalSince1970: TimeInterval(handshake))
            } else {
                live.handshake = nil
            }

            if let endpoint = peer?.endpoint, endpoint != "(none)" {
                live.endpoint = endpoint
            }
        }
    }

/*
===============
apply
===============
*/
    private func apply(_ item: ManagedTunnel, to id: UUID, phase: Phase) {
        mutate(id) { live in
            live.phase = phase
            live.realInterface = item.realInterface
            live.gateway = item.gatewayV4
            live.gatewayIface = item.gatewayIface
            live.lanCIDRs = item.lanCIDRs
            live.endpointIPs = item.endpointIPs
            live.appliedRoutes = item.bypass
        }
    }

/*
===============
mutate
===============
*/
    private func mutate(_ id: UUID, _ body: (inout LiveTunnel) -> Void) {
        var live = lives[id] ?? LiveTunnel()
        body(&live)
        lives[id] = live
    }

/*
===============
isUp
===============
*/
    private func isUp(_ id: UUID) -> Bool {
        let phase = live(id).phase
        return phase == .connected || phase == .connecting
    }

/*
===============
rpc
===============
*/
    private func rpc(_ request: RPCRequest, timeout: TimeInterval) async -> RPCResponse? {
        await Task.detached(priority: .userInitiated) {
            try? RPCClient.call(request, timeout: timeout)
        }.value
    }

/*
===============
persist
===============
*/
    private func persist(immediate: Bool = true) {
        if immediate {
            saveTask?.cancel()
            store.save(tunnels)
            return
        }

        saveTask?.cancel()
        saveTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 500_000_000)
            guard !Task.isCancelled, let self else { return }
            self.store.save(self.tunnels)
        }
    }

/*
===============
sanitized
===============
*/
    private func sanitized(_ name: String) -> String {
        let cleaned = name.replacingOccurrences(of: "/", with: "-").trimmingCharacters(in: .whitespaces)
        return cleaned.isEmpty ? "tunnel" : cleaned
    }

/*
===============
exportStem
===============
*/
    private func exportStem(_ name: String) -> String {
        var stem = sanitized(name)
        if stem.lowercased().hasSuffix(".conf") {
            stem = String(stem.dropLast(5))
        }

        let cleaned = stem.trimmingCharacters(in: .whitespaces)
        return cleaned.isEmpty ? "tunnel" : cleaned
    }

/*
===============
confFileURL
===============
*/
    private func confFileURL(_ url: URL) -> URL {
        if url.pathExtension.lowercased() == "conf" {
            let stem = url.deletingPathExtension()
            if stem.pathExtension.lowercased() == "conf" {
                return stem
            }

            return url
        }

        return url.appendingPathExtension("conf")
    }
}
