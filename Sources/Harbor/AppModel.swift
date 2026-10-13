import AppKit
import Foundation
import HarborCore
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
    private var helperUpdateTask: Task<Void, Never>?
    private var resolverCache: (at: Date, snap: ScutilDNS.Snapshot)?
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

    init() {
        tunnels = store.load()
        settings = settingsStore.load()
        if let id = settings.defaultTunnelID, tunnels.contains(where: { $0.id == id }) {
            selection = id
        } else {
            selection = tunnels.first?.id
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
            await self?.ensureCurrentHelper()
            await self?.connectDefaultIfNeeded()
        }
    }

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

    func syncLoginItem() {
        let approval = HarborL10n.text("macOS still needs approval. Open System Settings → General → Login Items and allow Harbor.")
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

    func setDefaultTunnel(_ id: UUID?) {
        let resolved = id.flatMap { value in tunnels.contains { $0.id == value } ? value : nil }
        settings.defaultTunnelID = resolved
        settingsStore.save(settings)
    }

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

    private func connectDefaultIfNeeded() async {
        guard !didAutoConnect else { return }
        didAutoConnect = true
        guard let id = settings.defaultTunnelID,
              let tunnel = tunnels.first(where: { $0.id == id }) else { return }
        selection = id
        guard live(id).phase == .idle || live(id).phase == .disconnecting else { return }
        await connect(tunnel)
    }

    /// Replaces the launchd helper when it is older than the copy shipped in this app.
    func ensureCurrentHelper() async {
        if let helperUpdateTask {
            await helperUpdateTask.value
            return
        }
        guard HelperInstaller.needsUpdate() else {
            helperStale = false
            return
        }
        helperStale = true
        let task = Task { @MainActor in
            self.banner = HarborL10n.text("The running helper is an older copy. Harbor will replace it.")
            await self.installHelper()
            self.helperStale = HelperInstaller.needsUpdate()
        }
        helperUpdateTask = task
        await task.value
    }

    func live(_ id: UUID) -> LiveTunnel {
        lives[id] ?? LiveTunnel()
    }

    func createTunnel() {
        let number = tunnels.count + 1
        let tunnel = Tunnel.blank(name: HarborL10n.format("Tunnel %d", number))
        tunnels.append(tunnel)
        selection = tunnel.id
        editing = tunnel
        persist()
    }

    func importFiles(_ urls: [URL]) {
        for url in urls {
            guard let text = try? String(contentsOf: url, encoding: .utf8), !text.isEmpty else { continue }
            let name = url.deletingPathExtension().lastPathComponent
            var tunnel = Tunnel(name: name, configText: text)
            if tunnels.contains(where: { $0.name == tunnel.name }) {
                tunnel.name = "\(name) \(tunnels.count + 1)"
            }
            tunnels.append(tunnel)
            selection = tunnel.id
        }
        persist()
    }

    func importPanel() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [UTType(filenameExtension: "conf") ?? .data]
        panel.prompt = HarborL10n.text("Import")
        guard panel.runModal() == .OK else { return }
        importFiles(panel.urls)
    }

    func export(_ tunnel: Tunnel) {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = sanitized(tunnel.name) + ".conf"
        panel.allowedContentTypes = [UTType(filenameExtension: "conf") ?? .plainText]
        panel.prompt = HarborL10n.text("Export")
        guard panel.runModal() == .OK, let url = panel.url else { return }
        try? tunnel.configText.write(to: url, atomically: true, encoding: .utf8)
    }

    func update(_ tunnel: Tunnel) {
        guard let index = tunnels.firstIndex(where: { $0.id == tunnel.id }) else { return }
        tunnels[index] = tunnel
        persist()
        if live(tunnel.id).phase == .connected {
            Task { await pushRoutes(for: tunnel) }
        }
    }

    func delete(_ tunnel: Tunnel) {
        Task {
            if live(tunnel.id).phase == .connected || live(tunnel.id).phase == .connecting {
                await disconnect(tunnel)
            }
            tunnels.removeAll { $0.id == tunnel.id }
            lives[tunnel.id] = nil
            if settings.defaultTunnelID == tunnel.id {
                setDefaultTunnel(nil)
            }
            if selection == tunnel.id {
                selection = tunnels.first?.id
            }
            persist()
        }
    }

    func toggle(_ id: UUID) {
        guard let tunnel = tunnels.first(where: { $0.id == id }) else { return }
        switch live(id).phase {
        case .connected, .connecting:
            Task { await disconnect(tunnel) }
        case .idle, .disconnecting:
            Task { await connect(tunnel) }
        }
    }

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
        await ensureCurrentHelper()
        if HelperInstaller.needsUpdate() {
            if banner == nil {
                banner = HarborL10n.text("The running helper is an older copy. Harbor will replace it.")
            }
            showSettings = true
            return
        }
        await rememberResolvers()
        mutate(tunnel.id) { $0.phase = .connecting }
        banner = nil
        let response = await rpc(
            RPCRequest(
                op: "up",
                name: tunnel.slug,
                config: tunnel.configText,
                cidrs: tunnel.directCIDRs(),
                excludeLAN: tunnel.excludeLAN
            ),
            timeout: 90
        )
        guard let response else {
            mutate(tunnel.id) { $0.phase = .idle }
            helperOnline = false
            banner = HarborL10n.text("The helper did not respond. Start it and try again.")
            showSettings = true
            return
        }
        if let managed = response.managed?.first(where: { $0.name == tunnel.slug }) {
            apply(managed, to: tunnel.id, phase: .connected)
            pushedRoutes[tunnel.slug] = managed.bypass
        } else {
            mutate(tunnel.id) { $0.phase = .idle }
        }
        if let warnings = response.warnings, !warnings.isEmpty {
            mutate(tunnel.id) { $0.warnings = warnings.map(HarborL10n.display) }
        }
        noteRouteFailure(response.error, ok: response.ok, tunnel: tunnel)
        await refresh()
    }

    /// Brings every helper-managed tunnel down before the process exits.
    func shutdownTunnels() {
        guard let status = try? RPCClient.call(RPCRequest(op: "status"), timeout: 8), status.ok else { return }
        for tunnel in status.managed ?? [] {
            _ = try? RPCClient.call(RPCRequest(op: "down", name: tunnel.name), timeout: 45)
        }
    }

    private func noteRouteFailure(_ error: String?, ok: Bool, tunnel: Tunnel) {
        guard let error, !ok else { return }
        if tunnel.directCIDRs().isEmpty,
           error.contains("direct routes were not applied") || error.contains("прямых маршрутов") {
            return
        }
        banner = HarborL10n.display(error)
    }

    func disconnect(_ tunnel: Tunnel) async {
        mutate(tunnel.id) { $0.phase = .disconnecting }
        let response = await rpc(RPCRequest(op: "down", name: tunnel.slug), timeout: 60)
        mutate(tunnel.id) { live in
            live.phase = .idle
            live.rateDown = 0
            live.rateUp = 0
            live.realInterface = nil
        }
        pushedRoutes[tunnel.slug] = nil
        if response == nil {
            helperOnline = false
            banner = HarborL10n.text("Could not reach the helper to disconnect.")
        } else if let error = response?.error, response?.ok == false {
            banner = HarborL10n.display(error)
        }
        await refresh()
    }

    func addIP(to id: UUID, raw: String, note: String) -> String? {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return nil }
        guard let cidr = CIDR.parse(raw) else { return HarborL10n.text("Enter an IP or network, for example 203.0.113.10 or 10.1.0.0/16.") }
        if cidr.prefix == 0 { return HarborL10n.text("That network is too broad and would disable the whole tunnel.") }
        if tunnel.ipRules.contains(where: { $0.cidr == cidr.string }) { return HarborL10n.text("That address is already listed.") }
        tunnel.ipRules.append(IPRule(cidr: cidr.string, note: note))
        replace(tunnel)
        return nil
    }

    func removeIP(_ ruleID: UUID, tunnel id: UUID) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        tunnel.ipRules.removeAll { $0.id == ruleID }
        replace(tunnel)
    }

    func setIP(_ ruleID: UUID, tunnel id: UUID, enabled: Bool) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        guard let index = tunnel.ipRules.firstIndex(where: { $0.id == ruleID }) else { return }
        tunnel.ipRules[index].enabled = enabled
        replace(tunnel)
    }

    func addApplications(to id: UUID) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [.application]
        panel.directoryURL = URL(fileURLWithPath: "/Applications")
        panel.prompt = HarborL10n.text("Add")
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

    func removeApp(_ ruleID: UUID, tunnel id: UUID) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        tunnel.appRules.removeAll { $0.id == ruleID }
        replace(tunnel)
    }

    func setApp(_ ruleID: UUID, tunnel id: UUID, enabled: Bool? = nil, learn: Bool? = nil) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        guard let index = tunnel.appRules.firstIndex(where: { $0.id == ruleID }) else { return }
        if let enabled { tunnel.appRules[index].enabled = enabled }
        if let learn { tunnel.appRules[index].learn = learn }
        replace(tunnel)
    }

    func forget(_ address: String, app ruleID: UUID, tunnel id: UUID) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        guard let index = tunnel.appRules.firstIndex(where: { $0.id == ruleID }) else { return }
        tunnel.appRules[index].learned.removeAll { $0 == address }
        replace(tunnel)
    }

    func addDomain(to id: UUID, raw: String, note: String) -> String? {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.contains("*") {
            return HarborL10n.text("Wildcards such as *.example.com are not supported.")
        }
        guard let name = DomainName.normalize(trimmed) else {
            return HarborL10n.text("Enter a domain, for example example.com.")
        }
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return nil }
        if tunnel.domainRules.contains(where: { $0.name == name }) {
            return HarborL10n.text("That domain is already listed.")
        }
        tunnel.domainRules.append(DomainRule(name: name, note: note.trimmingCharacters(in: .whitespacesAndNewlines)))
        replace(tunnel)
        Task { await refreshDomains() }
        return nil
    }

    func removeDomain(_ ruleID: UUID, tunnel id: UUID) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        tunnel.domainRules.removeAll { $0.id == ruleID }
        replace(tunnel)
    }

    func setDomain(_ ruleID: UUID, tunnel id: UUID, enabled: Bool) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        guard let index = tunnel.domainRules.firstIndex(where: { $0.id == ruleID }) else { return }
        tunnel.domainRules[index].enabled = enabled
        replace(tunnel)
    }

    func setExcludeLAN(_ id: UUID, _ enabled: Bool) {
        guard var tunnel = tunnels.first(where: { $0.id == id }) else { return }
        tunnel.excludeLAN = enabled
        replace(tunnel)
    }

    func installHelper() async {
        banner = nil
        if let error = await HelperInstaller.install() {
            banner = error
            return
        }
        await refresh()
        if helperOnline {
            banner = nil
        } else {
            banner = HarborL10n.text("The helper is installed, but the socket is not responding yet.")
        }
    }

    func openConfigFolder() {
        NSWorkspace.shared.open(store.folder)
    }

    func menuTitle(_ tunnel: Tunnel) -> String {
        let mark = live(tunnel.id).phase == .connected ? "● " : ""
        return mark + tunnel.name
    }

    func openMainWindow() {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        NSApp.windows.first { $0.canBecomeMain }?.makeKeyAndOrderFront(nil)
    }

    var anyConnected: Bool {
        tunnels.contains { lives[$0.id]?.phase == .connected }
    }

    private func replace(_ tunnel: Tunnel) {
        guard let index = tunnels.firstIndex(where: { $0.id == tunnel.id }) else { return }
        tunnels[index] = tunnel
        persist()
        if isUp(tunnel.id) {
            Task { await pushRoutes(for: tunnel) }
        }
    }

    private func pushRoutes(for tunnel: Tunnel) async {
        let cidrs = tunnel.directCIDRs()
        let response = await rpc(
            RPCRequest(op: "bypass", name: tunnel.slug, cidrs: cidrs, excludeLAN: tunnel.excludeLAN),
            timeout: 30
        )
        guard let response else { return }
        if let managed = response.managed?.first(where: { $0.name == tunnel.slug }) {
            apply(managed, to: tunnel.id, phase: .connected)
            pushedRoutes[tunnel.slug] = managed.bypass
        }
        if let warnings = response.warnings {
            mutate(tunnel.id) { $0.warnings = warnings.map(HarborL10n.display) }
        }
        noteRouteFailure(response.error, ok: response.ok, tunnel: tunnel)
    }

    private func learnDestinations() async {
        let snapshot = tunnels
        guard snapshot.contains(where: { $0.appRules.contains { $0.enabled && $0.learn } }) else { return }
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
                guard tunnel.appRules[ruleIndex].enabled, tunnel.appRules[ruleIndex].learn else { continue }
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

    private func refreshDomains() async {
        let now = Date()
        let due = tunnels.contains { tunnel in
            tunnel.domainRules.contains { rule in
                rule.enabled && (rule.refreshAt == nil || (rule.refreshAt ?? now) <= now)
            }
        }
        guard due else { return }
        await rememberResolvers()
        var jobs: [DomainJob] = []
        for tunnel in tunnels {
            let state = live(tunnel.id)
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
            guard !servers.isEmpty else { continue }
            for rule in tunnel.domainRules where rule.enabled {
                let ready = rule.refreshAt == nil || (rule.refreshAt ?? now) <= now
                guard ready else { continue }
                jobs.append(DomainJob(id: rule.id, name: rule.name, servers: servers, interface: iface))
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
            for ruleIndex in tunnel.domainRules.indices {
                let ruleID = tunnel.domainRules[ruleIndex].id
                guard asked.contains(ruleID) else { continue }
                if let answer = answers[ruleID] {
                    let next = mergedDomainAddresses(
                        existing: tunnel.domainRules[ruleIndex].addresses,
                        answer: answer,
                        skip: skip
                    )
                    if next != tunnel.domainRules[ruleIndex].addresses {
                        tunnel.domainRules[ruleIndex].addresses = next
                        addressesChanged = true
                    }
                    tunnel.domainRules[ruleIndex].refreshAt = now.addingTimeInterval(TimeInterval(answer.ttl))
                } else {
                    tunnel.domainRules[ruleIndex].refreshAt = now.addingTimeInterval(30)
                }
                tunnelChanged = true
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

    private func refresh() async {
        now = Date()
        let response = await rpc(RPCRequest(op: "status"), timeout: 8)
        guard let response, response.ok else {
            helperOnline = false
            if tools.wireguardGo == nil {
                tools = ToolLocator.locate()
            }
            return
        }
        helperOnline = true
        if let report = response.tools {
            tools = report
        }
        let managed = response.managed ?? []
        let devices = response.devices ?? []
        for tunnel in tunnels {
            if live(tunnel.id).phase == .connecting || live(tunnel.id).phase == .disconnecting {
                if let item = managed.first(where: { $0.name == tunnel.slug }), live(tunnel.id).phase == .connecting {
                    apply(item, to: tunnel.id, phase: .connected)
                    absorb(devices, interface: item.realInterface, id: tunnel.id)
                }
                continue
            }
            if let item = managed.first(where: { $0.name == tunnel.slug }) {
                apply(item, to: tunnel.id, phase: .connected)
                absorb(devices, interface: item.realInterface, id: tunnel.id)
            } else if lives[tunnel.id]?.phase == .connected {
                mutate(tunnel.id) {
                    $0.phase = .idle
                    $0.rateDown = 0
                    $0.rateUp = 0
                }
            }
        }
    }

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

    private func mutate(_ id: UUID, _ body: (inout LiveTunnel) -> Void) {
        var live = lives[id] ?? LiveTunnel()
        body(&live)
        lives[id] = live
    }

    private func isUp(_ id: UUID) -> Bool {
        let phase = live(id).phase
        return phase == .connected || phase == .connecting
    }

    private func rpc(_ request: RPCRequest, timeout: Int) async -> RPCResponse? {
        await Task.detached(priority: .userInitiated) {
            try? RPCClient.call(request, timeout: timeout)
        }.value
    }

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

    private func sanitized(_ name: String) -> String {
        let cleaned = name.replacingOccurrences(of: "/", with: "-").trimmingCharacters(in: .whitespaces)
        return cleaned.isEmpty ? "tunnel" : cleaned
    }
}
