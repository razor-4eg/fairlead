//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// main.swift -- Helper process: the control socket and tunnel commands.
//
//===============================================================

import Darwin
import Foundation
import FairleadCore

struct TunnelRuntime: Codable, Equatable {
    var configPath: String
    var realInterface: String
    var gatewayV4: String?
    var gatewayV6: String?
    var gatewayIface: String?
    var lanCIDRs: [String]
    var ownedRoutes: [String]
    var endpointIPs: [String]
}

struct HelperState: Codable {
    var tunnels: [String: TunnelRuntime]
}

enum HelperError: LocalizedError {
    case message(String)
    var errorDescription: String? {
        switch self {
        case .message(let text): return text
        }
    }
}

struct CommandResult {
    var code: Int32
    var out: String
    var err: String
    var blob: String { out + (err.isEmpty ? "" : "\n" + err) }
}

private let stateLock = NSLock()
/// Serializes up / down / bypass / quit so route I/O can drop `stateLock` and still answer status.
private let controlLock = NSLock()
private var state = HelperState(tunnels: [:])
private var shouldQuit = false

/*
===============
fail
===============
*/
private func fail(_ text: String) -> Never {
    fputs("fairlead-helper: \(text)\n", stderr)
    exit(1)
}

/*
===============
refreshHelperLanguage
===============
*/
private func refreshHelperLanguage() {
    let bundleURL = URL(fileURLWithPath: "/usr/local/libexec/FairleadLocalizations.bundle")
    if let bundle = Bundle(url: bundleURL) {
        FairleadL10n.bundle = bundle
    }

    guard let account = getpwnam(user) else { return }
    let home = String(cString: account.pointee.pw_dir)
    let url = URL(fileURLWithPath: home).appendingPathComponent("Library/Preferences/.GlobalPreferences.plist")
    guard let data = try? Data(contentsOf: url),
          let plist = try? PropertyListSerialization.propertyList(from: data, options: [], format: nil) as? [String: Any],
          let languages = plist["AppleLanguages"] as? [String],
          let first = languages.first else {
        return
    }

    FairleadL10n.languageOverride = first
}

/*
===============
username
===============
*/
private func username() -> String {
    let args = CommandLine.arguments
    if let index = args.firstIndex(of: "--user"), index + 1 < args.count {
        return args[index + 1]
    }

    fail(FairleadL10n.text("--user is required."))
}

/*
===============
readPipe
===============
*/
private func readPipe(_ fd: Int32) -> Data {
    var data = Data()
    var buffer = [UInt8](repeating: 0, count: 16_384)
    while true {
        let count = buffer.withUnsafeMutableBytes { raw -> Int in
            guard let base = raw.baseAddress else { return -1 }
            return Darwin.read(fd, base, raw.count)
        }
        if count == 0 { break }
        if count < 0 {
            if errno == EINTR { continue }
            break
        }
        data.append(buffer, count: count)
    }
    return data
}

/*
===============
run
posix_spawn instead of Foundation.Process — connect/disconnect spawn dozens of
route/ifconfig/networksetup children, and Process adds ~100ms each.
===============
*/
func run(_ arguments: [String]) -> CommandResult {
    guard let executable = arguments.first, !executable.isEmpty else {
        return CommandResult(code: 127, out: "", err: "missing executable")
    }

    var outPipe: [Int32] = [0, 0]
    var errPipe: [Int32] = [0, 0]
    guard pipe(&outPipe) == 0 else {
        return CommandResult(code: 127, out: "", err: String(cString: strerror(errno)))
    }
    guard pipe(&errPipe) == 0 else {
        close(outPipe[0]); close(outPipe[1])
        return CommandResult(code: 127, out: "", err: String(cString: strerror(errno)))
    }
    _ = fcntl(outPipe[0], F_SETFD, FD_CLOEXEC)
    _ = fcntl(outPipe[1], F_SETFD, FD_CLOEXEC)
    _ = fcntl(errPipe[0], F_SETFD, FD_CLOEXEC)
    _ = fcntl(errPipe[1], F_SETFD, FD_CLOEXEC)

    var actions: posix_spawn_file_actions_t?
    posix_spawn_file_actions_init(&actions)
    defer { posix_spawn_file_actions_destroy(&actions) }
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0)
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO)
    posix_spawn_file_actions_adddup2(&actions, errPipe[1], STDERR_FILENO)
    posix_spawn_file_actions_addclose(&actions, outPipe[0])
    posix_spawn_file_actions_addclose(&actions, outPipe[1])
    posix_spawn_file_actions_addclose(&actions, errPipe[0])
    posix_spawn_file_actions_addclose(&actions, errPipe[1])

    let argvStorage: [UnsafeMutablePointer<CChar>?] = arguments.map { strdup($0) } + [nil]
    defer { argvStorage.forEach { free($0) } }
    var argv = argvStorage
    var pid: pid_t = 0
    let spawned = argv.withUnsafeMutableBufferPointer { pointer in
        posix_spawn(&pid, executable, &actions, nil, pointer.baseAddress, environ)
    }
    close(outPipe[1])
    close(errPipe[1])
    guard spawned == 0 else {
        close(outPipe[0])
        close(errPipe[0])
        return CommandResult(code: 127, out: "", err: String(cString: strerror(spawned)))
    }

    let group = DispatchGroup()
    var outData = Data()
    var errData = Data()
    group.enter()
    DispatchQueue.global(qos: .userInitiated).async {
        outData = readPipe(outPipe[0])
        close(outPipe[0])
        group.leave()
    }
    group.enter()
    DispatchQueue.global(qos: .userInitiated).async {
        errData = readPipe(errPipe[0])
        close(errPipe[0])
        group.leave()
    }

    var status: Int32 = 0
    while true {
        let waited = waitpid(pid, &status, 0)
        if waited == -1 && errno == EINTR { continue }
        break
    }
    group.wait()

    // wait(2) status macros are not imported into Swift.
    let code: Int32
    if (status & 0o177) == 0 {
        code = (status >> 8) & 0xff
    } else if (status & 0o177) != 0o177 {
        code = 128 + (status & 0o177)
    } else {
        code = status
    }
    let out = String(data: outData, encoding: .utf8) ?? ""
    let err = String(data: errData, encoding: .utf8) ?? ""
    return CommandResult(code: code, out: out, err: err)
}

/*
===============
loadState
===============
*/
private func loadState() {
    guard let data = FileManager.default.contents(atPath: FairleadPaths.state),
          let decoded = try? JSONDecoder().decode(HelperState.self, from: data) else {
        state = HelperState(tunnels: [:])
        return
    }

    state = decoded
}

/*
===============
saveState
===============
*/
private func saveState() {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    guard let data = try? encoder.encode(state) else { return }
    let url = URL(fileURLWithPath: FairleadPaths.state)
    try? data.write(to: url, options: .atomic)
    chmod(FairleadPaths.state, 0o600)
}

/*
===============
validSlug
===============
*/
private func validSlug(_ name: String) -> Bool {
    guard name.count == 11, name.hasPrefix("fld") || name.hasPrefix("hbr") else { return false }
    return name.dropFirst(3).allSatisfy(\.isHexDigit)
}

/*
===============
hardwareInterfaces
===============
*/
private func hardwareInterfaces() -> [String] {
    let listed = run(["/sbin/ifconfig", "-l"]).out
    return listed.split(whereSeparator: \.isWhitespace).map(String.init).filter {
        $0.hasPrefix("en") || $0.hasPrefix("bridge")
    }
}

/*
===============
defaultGateway
Prefer the unscoped default route. Fall back to per-interface probes only when
needed — probing every hardware iface on connect cost a second or more.
===============
*/
private func defaultGateway(family: String) -> (iface: String, gateway: String)? {
    let parsed = RouteText.parseRouteGet(run(["/sbin/route", "-n", "get", "-\(family)", "default"]).out)
    if let gateway = parsed.gateway,
       let iface = parsed.interface,
       !iface.isEmpty,
       IPAddress.parse(gateway) != nil,
       !iface.hasPrefix("utun") {
        return (iface, gateway)
    }

    var best: (iface: String, gateway: String, score: Int)?
    for iface in hardwareInterfaces() {
        let result = run(["/sbin/route", "-n", "get", "-\(family)", "default", "-ifscope", iface])
        let scoped = RouteText.parseRouteGet(result.out)
        guard let gateway = scoped.gateway, scoped.interface == iface else { continue }
        guard IPAddress.parse(gateway) != nil else { continue }
        let info = run(["/sbin/ifconfig", iface]).out
        var score = 0
        if info.contains("status: active") { score += 5 }
        if family == "inet", info.contains("inet ") { score += 3 }
        if family == "inet6", info.contains("inet6 ") { score += 3 }
        if iface == "en0" { score += 1 }
        if best == nil || score > best!.score {
            best = (iface, gateway, score)
        }
    }

    return best.map { ($0.iface, $0.gateway) }
}

/*
===============
captureNetwork
===============
*/
private func captureNetwork() -> (v4: (String, String)?, v6: (String, String)?, lan: [String]) {
    let v4 = defaultGateway(family: "inet")
    let v6 = defaultGateway(family: "inet6")
    let iface = v4?.0 ?? v6?.0
    let lan = iface.map { RouteText.networks(fromIfconfig: run(["/sbin/ifconfig", $0]).out) } ?? []
    return (v4, v6, lan)
}

/*
===============
interfaceExists
===============
*/
func interfaceExists(_ name: String) -> Bool {
    guard !name.isEmpty else { return false }
    return if_nametoindex(name) != 0
}

/*
===============
endpointIPs
===============
*/
private func endpointIPs(realInterface: String) -> [String] {
    guard let device = userspaceStatus(realInterface) else { return [] }
    var ips: [String] = []
    for peer in device.peers {
        guard let host = EndpointParse.host(from: peer.endpoint), let ip = IPAddress.parse(host) else { continue }
        ips.append(ip.string)
    }

    return RouteText.unique(ips)
}

private struct ListedProcess {
    var pid: Int32
    var ppid: Int32
    var command: String
}

/*
===============
processList
===============
*/
private func processList() -> [ListedProcess] {
    let result = run(["/bin/ps", "-axww", "-o", "pid=,ppid=,command="])
    var list: [ListedProcess] = []
    for line in result.out.split(separator: "\n", omittingEmptySubsequences: true) {
        let text = String(line).trimmingCharacters(in: .whitespaces)
        let parts = text.split(separator: " ", maxSplits: 2, omittingEmptySubsequences: true)
        guard parts.count >= 2, let pid = Int32(parts[0]), let ppid = Int32(parts[1]), pid > 1 else { continue }
        let command = parts.count == 3 ? String(parts[2]) : ""
        list.append(ListedProcess(pid: pid, ppid: ppid, command: command))
    }

    return list
}

/*
===============
isFairleadUserspace
===============
*/
private func isFairleadUserspace(_ command: String) -> Bool {
    if command.contains("fairlead-helper") || command.contains("harbor-helper") { return false }
    if command.contains("FAIRLEAD_MANAGED=1") || command.contains("HARBOR_MANAGED=1") { return true }
    if command.contains("/usr/local/libexec/fairlead-wireguard-go")
        || command.contains("/usr/local/libexec/harbor-wireguard-go")
        || command.contains("/usr/local/libexec/fairlead-amneziawg-go")
        || command.contains("/usr/local/libexec/fairlead-wg-go")
        || command.contains("/usr/local/libexec/harbor-wg-go") {
        return true
    }

    if command.contains("WG_TUN_NAME_FILE=/var/run/wireguard/fld")
        || command.contains("WG_TUN_NAME_FILE=/var/run/wireguard/hbr") {
        return true
    }

    if (command.contains("/var/run/fairlead/") || command.contains("/var/run/harbor/"))
        && (command.contains("wg-quick") || command.contains("wireguard-go") || command.contains("amneziawg-go")) {
        return true
    }

    return false
}

/*
===============
fairleadProcessTree
===============
*/
private func fairleadProcessTree() -> [ListedProcess] {
    let list = processList()
    var seeds = Set<Int32>()
    for proc in list where proc.pid != getpid() && isFairleadUserspace(proc.command) {
        seeds.insert(proc.pid)
    }

    if let text = try? String(contentsOfFile: "/var/run/fairlead/wireguard-go.pid", encoding: .utf8),
       let pid = Int32(text.trimmingCharacters(in: .whitespacesAndNewlines)),
       pid != getpid(),
       let proc = list.first(where: { $0.pid == pid }),
       proc.command.contains("wireguard-go") || proc.command.contains("amneziawg-go") {
        seeds.insert(pid)
    }

    var keep = seeds
    var grew = true
    while grew {
        grew = false
        for proc in list where proc.pid != getpid() && !keep.contains(proc.pid) && keep.contains(proc.ppid) {
            keep.insert(proc.pid)
            grew = true
        }
    }

    return list.filter { keep.contains($0.pid) }
}

/*
===============
isTunnelBlocker
===============
*/
private func isTunnelBlocker(_ command: String) -> Bool {
    command.contains("networksetup") || (command.contains("route") && command.contains("monitor"))
}

// The route monitor keeps running after wg-quick returns. On the next connect
// its exit trap deletes the new interface, and wg-quick still exits 0.

/*
===============
pidAlive
===============
*/
private func pidAlive(_ pid: Int32) -> Bool {
    pid > 1 && kill(pid, 0) == 0
}

/*
===============
stopManagedProcesses
===============
*/
func stopManagedProcesses() {
    let targets = fairleadProcessTree()
    guard !targets.isEmpty else { return }
    fputs("fairlead-helper: stopping \(targets.count) leftover tunnel processes\n", stderr)
    for proc in targets where isTunnelBlocker(proc.command) {
        kill(proc.pid, SIGKILL)
    }

    let workers = targets.filter { !isTunnelBlocker($0.command) }
    for proc in workers {
        kill(proc.pid, SIGTERM)
    }

    // amneziawg-go usually exits immediately; don't burn seconds re-parsing `ps`.
    let deadline = Date().addingTimeInterval(0.6)
    while Date() < deadline {
        if workers.allSatisfy({ !pidAlive($0.pid) }) { break }
        usleep(20_000)
    }

    for proc in workers where pidAlive(proc.pid) {
        kill(proc.pid, SIGKILL)
    }
}

/*
===============
removeInterfaceFiles
===============
*/
func removeInterfaceFiles(slug: String) {
    let nameFile = "\(FairleadPaths.wireguardRun)/\(slug).name"
    var iface: String?
    if let text = try? String(contentsOfFile: nameFile, encoding: .utf8) {
        let name = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if !name.isEmpty { iface = name }
    }

    if let iface, iface.allSatisfy({ $0.isLetter || $0.isNumber }) {
        unlink("\(FairleadPaths.wireguardRun)/\(iface).sock")
    }

    unlink(nameFile)
    unlink("/var/run/fairlead/wireguard-go.pid")
}

/*
===============
routeSnapshot
===============
*/
private func routeSnapshot(_ cidr: CIDR) -> RouteSnapshot {
    let family = cidr.family == .v4 ? "inet" : "inet6"
    return RouteText.snapshot(from: run(["/sbin/route", "-n", "get", "-\(family)", cidr.string]).out)
}

/*
===============
addOwnedRoute
===============
*/
private func addOwnedRoute(_ cidr: CIDR, viaTunnel: Bool, runtime: TunnelRuntime) -> String? {
    let family = cidr.family == .v4 ? "inet" : "inet6"
    let result: CommandResult
    if viaTunnel {
        result = run(["/sbin/route", "-q", "-n", "add", "-\(family)", cidr.string, "-interface", runtime.realInterface])
    } else {
        guard let gateway = cidr.family == .v4 ? runtime.gatewayV4 : runtime.gatewayV6 else {
            return FairleadL10n.format("No gateway for %@.", cidr.string)
        }

        result = run(["/sbin/route", "-q", "-n", "add", "-\(family)", cidr.string, gateway])
    }

    let blob = result.blob
    if result.code == 0
        || blob.localizedCaseInsensitiveContains("file exists")
        || blob.localizedCaseInsensitiveContains("exists")
        || blob.localizedCaseInsensitiveContains("существует") {
        return nil
    }

    let detail = result.blob.trimmingCharacters(in: .whitespacesAndNewlines)
    return detail.isEmpty ? FairleadL10n.format("Could not add route %@.", cidr.string) : detail
}

/*
===============
deleteOwnedRoute
===============
*/
private func deleteOwnedRoute(_ cidrString: String) {
    guard let cidr = CIDR.parse(cidrString) else { return }
    let family = cidr.family == .v4 ? "inet" : "inet6"
    // Skip `route get` — these are routes we installed; a missing one is fine.
    _ = run(["/sbin/route", "-q", "-n", "delete", "-\(family)", cidr.string])
}

/*
===============
applyBypass
Caller holds `stateLock`. Route process work drops it so status/ping still answer
while a long list is applied; `controlLock` keeps a second up/down/bypass out.
===============
*/
private func applyBypass(slug: String, cidrs: [String], excludeLAN: Bool, policy: RoutePolicy) -> (warnings: [String], error: String?) {
    guard let runtime = state.tunnels[slug] else {
        return ([], FairleadL10n.text("The tunnel is not running."))
    }

    let configPath = runtime.configPath
    let lanCIDRs = runtime.lanCIDRs
    let endpointIPs = runtime.endpointIPs
    let previousOwned = runtime.ownedRoutes
    let working = runtime

    stateLock.unlock()
    syncInternetCapture(slug: slug, configPath: configPath, capture: policy != .selected)
    let planned = BypassPlanner.routes(
        BypassInput(
            rules: cidrs,
            learned: [],
            lan: lanCIDRs,
            excludeLAN: excludeLAN,
            endpoints: endpointIPs,
            policy: policy
        )
    )
    let tunnelSet = policy == .selected ? Set(planned.routes) : Set<String>()
    let directSet = policy == .selected ? Set(planned.direct) : Set(planned.routes)
    let desired = tunnelSet.union(directSet)
    let automatic = Set(lanCIDRs.compactMap { CIDR.parse($0)?.string })
    for existing in previousOwned where !desired.contains(existing) {
        deleteOwnedRoute(existing)
    }

    var owned: [String] = []
    var failures: [String] = []
    for route in desired.sorted() {
        guard let cidr = CIDR.parse(route) else { continue }
        let wantTunnel = tunnelSet.contains(route)
        let snapshot = routeSnapshot(cidr)
        let exact = RouteText.isExact(cidr, snapshot: snapshot)
        let onTunnel = snapshot.interface == working.realInterface
        let weOwned = previousOwned.contains(route)
        if exact && onTunnel == wantTunnel && (weOwned || !wantTunnel) {
            if weOwned { owned.append(route) }
            continue
        }

        if exact { deleteOwnedRoute(route) }
        // No IPv6 (or IPv4) gateway on this network — skip that family instead of
        // failing the whole connect with "direct routes were not applied".
        if !wantTunnel {
            let hasGateway = (cidr.family == .v4 && working.gatewayV4 != nil)
                || (cidr.family == .v6 && working.gatewayV6 != nil)
            if !hasGateway { continue }
        }

        if let error = addOwnedRoute(cidr, viaTunnel: wantTunnel, runtime: working) {
            let now = routeSnapshot(cidr)
            let nowExact = RouteText.isExact(cidr, snapshot: now)
            let nowOnTunnel = now.interface == working.realInterface
            if nowExact && nowOnTunnel == wantTunnel {
                owned.append(route)
            } else if automatic.contains(route) && !wantTunnel {
                continue
            } else {
                failures.append("\(route): \(error)")
            }
        } else {
            owned.append(route)
        }
    }

    stateLock.lock()
    guard state.tunnels[slug] != nil else {
        return (planned.warnings, FairleadL10n.text("The tunnel is not running."))
    }

    state.tunnels[slug]?.ownedRoutes = owned
    saveState()
    if failures.isEmpty {
        return (planned.warnings, nil)
    }

    return (planned.warnings, failures.joined(separator: "\n"))
}

/*
===============
writeConfig
===============
*/
private func writeConfig(slug: String, config: String) throws -> String {
    guard config.utf8.count <= 256_000 else { throw HelperError.message(FairleadL10n.text("The config is too large.")) }
    guard !config.contains("\0") else { throw HelperError.message(FairleadL10n.text("The config is damaged.")) }
    var document = WGDocument.parse(config)
    let issues = ConfigCheck.issues(in: document)
    if let first = issues.first {
        throw HelperError.message(first.message)
    }

    // Hooks run as root inside wg-quick. Fairlead never executes them.
    let hooks: Set<String> = ["preup", "postup", "predown", "postdown", "saveconfig"]
    for index in document.sections.indices {
        document.sections[index].entries.removeAll { entry in
            guard let key = entry.key?.lowercased() else { return false }
            return hooks.contains(key)
        }
    }

    let path = "\(FairleadPaths.directory)/\(slug).conf"
    do {
        try document.render().write(toFile: path, atomically: true, encoding: .utf8)
    } catch {
        throw HelperError.message(FairleadL10n.text("Could not write the config."))
    }

    chmod(path, 0o600)
    return path
}

/*
===============
bringDown
===============
*/
private func bringDown(slug: String) -> String? {
    let path = state.tunnels[slug]?.configPath ?? "\(FairleadPaths.directory)/\(slug).conf"
    let realBefore = state.tunnels[slug]?.realInterface
    let owned = state.tunnels[slug]?.ownedRoutes ?? []
    // Drop owned routes before tearing the interface so we don't wait on gets.
    for route in owned { deleteOwnedRoute(route) }
    bringUserspaceDown(slug: slug, interfaceName: realBefore)
    state.tunnels[slug] = nil
    saveState()
    try? FileManager.default.removeItem(atPath: path)
    fputs("fairlead-helper: tunnel down \(slug)\n", stderr)
    return nil
}

/*
===============
bringUp
===============
*/
private func bringUp(slug: String, config: String, cidrs: [String], excludeLAN: Bool, policy: RoutePolicy) -> RPCResponse {
    let tools = ToolLocator.locate()
    guard tools.ready else {
        return RPCResponse(id: "", ok: false, error: FairleadL10n.text("amneziawg-go is not included in this copy of Fairlead."), tools: tools)
    }

    if state.tunnels[slug] != nil {
        if let error = bringDown(slug: slug) {
            return RPCResponse(id: "", ok: false, error: error, tools: tools)
        }
    }

    let network = captureNetwork()
    let path: String
    do {
        path = try writeConfig(slug: slug, config: config)
    } catch {
        return RPCResponse(id: "", ok: false, error: error.localizedDescription, tools: tools)
    }

    let document = WGDocument.parse((try? String(contentsOfFile: path, encoding: .utf8)) ?? config)
    stopManagedProcesses()
    removeInterfaceFiles(slug: slug)
    let raised = bringUserspaceUp(
        slug: slug,
        document: document,
        gatewayV4: network.v4?.1,
        gatewayV6: network.v6?.1,
        gatewayIface: network.v4?.0 ?? network.v6?.0,
        captureInternet: policy != .selected
    )
    guard let real = raised.interface else {
        bringUserspaceDown(slug: slug, interfaceName: nil)
        try? FileManager.default.removeItem(atPath: path)
        let message = raised.error ?? FairleadL10n.text("amneziawg-go did not create an interface.")
        return RPCResponse(id: "", ok: false, error: message, tools: tools)
    }

    var runtime = TunnelRuntime(
        configPath: path,
        realInterface: real,
        gatewayV4: network.v4?.1,
        gatewayV6: network.v6?.1,
        gatewayIface: network.v4?.0 ?? network.v6?.0,
        lanCIDRs: network.lan,
        ownedRoutes: [],
        endpointIPs: endpointIPs(realInterface: real)
    )
    state.tunnels[slug] = runtime
    saveState()
    let applied = applyBypass(slug: slug, cidrs: cidrs, excludeLAN: excludeLAN, policy: policy)
    runtime = state.tunnels[slug] ?? runtime
    let devices = currentDevices()
    if let error = applied.error {
        return RPCResponse(
            id: "",
            ok: false,
            error: FairleadL10n.format("The tunnel is up, but some direct routes were not applied.\n%@", error),
            warnings: applied.warnings,
            tools: tools,
            managed: [managed(slug, runtime)],
            devices: devices
        )
    }

    return RPCResponse(
        id: "",
        ok: true,
        warnings: applied.warnings,
        tools: tools,
        managed: [managed(slug, runtime)],
        devices: devices
    )
}

/*
===============
managed
===============
*/
private func managed(_ slug: String, _ runtime: TunnelRuntime) -> ManagedTunnel {
    ManagedTunnel(
        name: slug,
        realInterface: runtime.realInterface,
        gatewayV4: runtime.gatewayV4,
        gatewayV6: runtime.gatewayV6,
        gatewayIface: runtime.gatewayIface,
        lanCIDRs: runtime.lanCIDRs,
        bypass: runtime.ownedRoutes.sorted(),
        endpointIPs: runtime.endpointIPs
    )
}

/*
===============
currentDevices
===============
*/
private func currentDevices() -> [WGDeviceStatus] {
    state.tunnels.values.compactMap { userspaceStatus($0.realInterface) }
}

/*
===============
reconcile
===============
*/
private func reconcile() {
    loadState()
    let alive = state.tunnels.filter { interfaceExists($0.value.realInterface) }
    let dead = state.tunnels.filter { !interfaceExists($0.value.realInterface) }
    for (slug, runtime) in dead {
        for route in runtime.ownedRoutes { deleteOwnedRoute(route) }
        bringUserspaceDown(slug: slug, interfaceName: runtime.realInterface)
    }

    state.tunnels = alive
    for slug in Array(state.tunnels.keys) {
        let interface = state.tunnels[slug]?.realInterface ?? ""
        let ips = endpointIPs(realInterface: interface)
        state.tunnels[slug]?.endpointIPs = ips
    }

    saveState()
}

/*
===============
snapshotResponse
===============
*/
private func snapshotResponse(id: String) -> RPCResponse {
    let tunnels = state.tunnels.map { managed($0.key, $0.value) }.sorted { $0.name < $1.name }
    return RPCResponse(id: id, ok: true, tools: ToolLocator.locate(), managed: tunnels, devices: currentDevices())
}

/*
===============
handle
===============
*/
private func handle(_ request: RPCRequest) -> RPCResponse {
    refreshHelperLanguage()
    switch request.op {
    case "ping":
        return RPCResponse(id: request.id, ok: true, tools: ToolLocator.locate())
    case "tools":
        return RPCResponse(id: request.id, ok: true, tools: ToolLocator.locate())
    case "status":
        var changed = false
        for slug in Array(state.tunnels.keys) {
            guard let real = state.tunnels[slug]?.realInterface else { continue }
            let ips = endpointIPs(realInterface: real)
            if state.tunnels[slug]?.endpointIPs != ips {
                state.tunnels[slug]?.endpointIPs = ips
                changed = true
            }
        }

        if changed { saveState() }
        return snapshotResponse(id: request.id)
    case "up":
        guard let name = request.name, validSlug(name) else {
            return RPCResponse(id: request.id, ok: false, error: FairleadL10n.text("Invalid tunnel name."))
        }

        guard let config = request.config, !config.isEmpty else {
            return RPCResponse(id: request.id, ok: false, error: FairleadL10n.text("The config is empty."))
        }

        var response = bringUp(
            slug: name,
            config: config,
            cidrs: request.cidrs ?? [],
            excludeLAN: request.excludeLAN ?? false,
            policy: RoutePolicy(rawValue: request.policy ?? "") ?? .exceptions
        )
        response.id = request.id
        return response
    case "down":
        guard let name = request.name, validSlug(name) else {
            return RPCResponse(id: request.id, ok: false, error: FairleadL10n.text("Invalid tunnel name."))
        }

        if let error = bringDown(slug: name) {
            return RPCResponse(id: request.id, ok: false, error: error)
        }

        return snapshotResponse(id: request.id)
    case "bypass":
        guard let name = request.name, validSlug(name) else {
            return RPCResponse(id: request.id, ok: false, error: FairleadL10n.text("Invalid tunnel name."))
        }

        let applied = applyBypass(
            slug: name,
            cidrs: request.cidrs ?? [],
            excludeLAN: request.excludeLAN ?? false,
            policy: RoutePolicy(rawValue: request.policy ?? "") ?? .exceptions
        )
        var response = snapshotResponse(id: request.id)
        response.warnings = applied.warnings
        if let error = applied.error {
            response.ok = false
            response.error = error
        }

        return response
    case "quit":
        for slug in Array(state.tunnels.keys) {
            _ = bringDown(slug: slug)
        }
        shouldQuit = true
        return RPCResponse(id: request.id, ok: true, tools: ToolLocator.locate())
    default:
        return RPCResponse(id: request.id, ok: false, error: FairleadL10n.text("Unknown command."))
    }
}

/*
===============
isHarborHelper
===============
*/
private func isHarborHelper(_ command: String) -> Bool {
    command.contains("/harbor-helper")
        || (command as NSString).lastPathComponent.hasPrefix("harbor-helper")
}

/*
===============
reapHarborHelpers
Clear leftover Harbor helpers. Fairlead itself is a single launchd job — do not
SIGKILL peer fairlead-helper processes or KeepAlive restarts fight each other.
===============
*/
private func reapHarborHelpers() {
    let selfPid = getpid()
    let peers = processList().filter { $0.pid != selfPid && isHarborHelper($0.command) }
    guard !peers.isEmpty else { return }
    fputs("fairlead-helper: stopping \(peers.count) harbor-helper process(es)\n", stderr)
    for proc in peers {
        kill(proc.pid, SIGTERM)
    }
    usleep(200_000)
    for proc in processList() where proc.pid != selfPid && isHarborHelper(proc.command) {
        kill(proc.pid, SIGKILL)
    }
}

/*
===============
isControlOp
up / down / bypass / quit must not interleave; status stays responsive during bypass.
===============
*/
private func isControlOp(_ op: String) -> Bool {
    op == "up" || op == "down" || op == "bypass" || op == "quit"
}

/*
===============
serve
===============
*/
private func serve(fd: Int32) {
    while !shouldQuit {
        var pollFD = pollfd(fd: fd, events: Int16(POLLIN), revents: 0)
        let ready = poll(&pollFD, 1, 250)
        if ready < 0 {
            if errno == EINTR { continue }
            // A transient poll error must not take the daemon down — KeepAlive
            // would restart us and tear the tunnel for a moment.
            let code = errno
            fputs("fairlead-helper: poll failed: \(String(cString: strerror(code))) (\(code))\n", stderr)
            if code == EBADF { break }
            usleep(50_000)
            continue
        }
        if ready == 0 { continue }
        let client = accept(fd, nil, nil)
        if client < 0 { continue }
        SocketFrame.setNoSigPipe(client)
        SocketFrame.setCloseOnExec(client)
        autoreleasepool {
            SocketFrame.setTimeout(client, seconds: 5)
            defer { close(client) }
            var stateLocked = false
            var controlLocked = false
            do {
                let data = try SocketFrame.read(client)
                let request = try RPCFrame.decode(RPCRequest.self, from: data)
                SocketFrame.setTimeout(client, seconds: 120)
                if isControlOp(request.op) {
                    controlLock.lock()
                    controlLocked = true
                }
                stateLock.lock()
                stateLocked = true
                let response = handle(request)
                stateLock.unlock()
                stateLocked = false
                if controlLocked {
                    controlLock.unlock()
                    controlLocked = false
                }
                let payload = try RPCFrame.encode(response)
                try SocketFrame.write(client, payload)
            } catch let error as SocketFrameError {
                if stateLocked { stateLock.unlock() }
                if controlLocked { controlLock.unlock() }
                // Timed-out UI clients are normal while up/down holds the lock.
                if case .disconnected = error { return }
                fputs("fairlead-helper: \(error)\n", stderr)
            } catch {
                if stateLocked { stateLock.unlock() }
                if controlLocked { controlLock.unlock() }
                fputs("fairlead-helper: \(error)\n", stderr)
            }
        }
    }
    close(fd)
    unlink(FairleadPaths.socket)
    try? FileManager.default.removeItem(atPath: "/var/run/fairlead/helper.pid")
    fputs("fairlead-helper: quit\n", stderr)
}

signal(SIGHUP, SIG_IGN)
// A client that times out closes the socket. Writing the reply must not kill the helper.
signal(SIGPIPE, SIG_IGN)
setbuf(stdout, nil)
setbuf(stderr, nil)
let user = username()
refreshHelperLanguage()
guard geteuid() == 0 else { fail(FairleadL10n.text("Run the helper with administrator privileges.")) }
guard let account = getpwnam(user) else { fail(FairleadL10n.format("User %@ was not found.", user)) }
let uid = account.pointee.pw_uid
let gid = account.pointee.pw_gid

try? FileManager.default.createDirectory(atPath: FairleadPaths.directory, withIntermediateDirectories: true)
chmod(FairleadPaths.directory, 0o755)
reapHarborHelpers()
reconcile()

let fd: Int32
do {
    fd = try SocketFrame.bind(path: FairleadPaths.socket)
} catch {
    fail(String(describing: error))
}

chown(FairleadPaths.socket, uid, gid)
chmod(FairleadPaths.socket, 0o600)
try? "\(getpid())\n".write(toFile: "/var/run/fairlead/helper.pid", atomically: true, encoding: .utf8)
fputs("fairlead-helper: listening for \(user)\n", stderr)
serve(fd: fd)
