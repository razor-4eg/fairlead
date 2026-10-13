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
private var state = HelperState(tunnels: [:])

private func fail(_ text: String) -> Never {
    fputs("fairlead-helper: \(text)\n", stderr)
    exit(1)
}

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

private func username() -> String {
    let args = CommandLine.arguments
    if let index = args.firstIndex(of: "--user"), index + 1 < args.count {
        return args[index + 1]
    }
    fail(FairleadL10n.text("--user is required."))
}

func run(_ arguments: [String]) -> CommandResult {
    autoreleasepool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: arguments[0])
        process.arguments = Array(arguments.dropFirst())
        var environment = ProcessInfo.processInfo.environment
        environment["PATH"] = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
        process.environment = environment
        process.standardInput = FileHandle.nullDevice
        let stdout = Pipe()
        let stderr = Pipe()
        process.standardOutput = stdout
        process.standardError = stderr
        do {
            try process.run()
        } catch {
            return CommandResult(code: 127, out: "", err: error.localizedDescription)
        }
        // Read both pipes while the child runs. Waiting first deadlocks when
        // the output is larger than the pipe buffer.
        let outHandle = stdout.fileHandleForReading
        let errHandle = stderr.fileHandleForReading
        let group = DispatchGroup()
        var outData = Data()
        var errData = Data()
        group.enter()
        DispatchQueue.global(qos: .userInitiated).async {
            outData = outHandle.readDataToEndOfFile()
            group.leave()
        }
        group.enter()
        DispatchQueue.global(qos: .userInitiated).async {
            errData = errHandle.readDataToEndOfFile()
            group.leave()
        }
        process.waitUntilExit()
        group.wait()
        try? outHandle.close()
        try? errHandle.close()
        let out = String(data: outData, encoding: .utf8) ?? ""
        let err = String(data: errData, encoding: .utf8) ?? ""
        return CommandResult(code: process.terminationStatus, out: out, err: err)
    }
}

private func loadState() {
    guard let data = FileManager.default.contents(atPath: FairleadPaths.state),
          let decoded = try? JSONDecoder().decode(HelperState.self, from: data) else {
        state = HelperState(tunnels: [:])
        return
    }
    state = decoded
}

private func saveState() {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    guard let data = try? encoder.encode(state) else { return }
    let url = URL(fileURLWithPath: FairleadPaths.state)
    try? data.write(to: url, options: .atomic)
    chmod(FairleadPaths.state, 0o600)
}

private func validSlug(_ name: String) -> Bool {
    guard name.count == 11, name.hasPrefix("fld") || name.hasPrefix("hbr") else { return false }
    return name.dropFirst(3).allSatisfy(\.isHexDigit)
}

private func hardwareInterfaces() -> [String] {
    let listed = run(["/sbin/ifconfig", "-l"]).out
    return listed.split(whereSeparator: \.isWhitespace).map(String.init).filter {
        $0.hasPrefix("en") || $0.hasPrefix("bridge")
    }
}

private func scopedGateway(family: String) -> (iface: String, gateway: String)? {
    var best: (iface: String, gateway: String, score: Int)?
    for iface in hardwareInterfaces() {
        let result = run(["/sbin/route", "-n", "get", "-\(family)", "default", "-ifscope", iface])
        let parsed = RouteText.parseRouteGet(result.out)
        guard let gateway = parsed.gateway, parsed.interface == iface else { continue }
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

private func captureNetwork() -> (v4: (String, String)?, v6: (String, String)?, lan: [String]) {
    let v4 = scopedGateway(family: "inet")
    let v6 = scopedGateway(family: "inet6")
    let iface = v4?.0 ?? v6?.0
    let lan = iface.map { RouteText.networks(fromIfconfig: run(["/sbin/ifconfig", $0]).out) } ?? []
    return (v4, v6, lan)
}

func interfaceExists(_ name: String) -> Bool {
    guard !name.isEmpty else { return false }
    let result = run(["/sbin/ifconfig", name])
    return result.code == 0 && result.out.contains("flags=")
}

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

private func isFairleadUserspace(_ command: String) -> Bool {
    if command.contains("fairlead-helper") || command.contains("harbor-helper") { return false }
    if command.contains("FAIRLEAD_MANAGED=1") || command.contains("HARBOR_MANAGED=1") { return true }
    if command.contains("/usr/local/libexec/fairlead-wireguard-go")
        || command.contains("/usr/local/libexec/harbor-wireguard-go")
        || command.contains("/usr/local/libexec/fairlead-wg-go")
        || command.contains("/usr/local/libexec/harbor-wg-go") {
        return true
    }
    if command.contains("WG_TUN_NAME_FILE=/var/run/wireguard/fld")
        || command.contains("WG_TUN_NAME_FILE=/var/run/wireguard/hbr") {
        return true
    }
    if (command.contains("/var/run/fairlead/") || command.contains("/var/run/harbor/"))
        && (command.contains("wg-quick") || command.contains("wireguard-go")) {
        return true
    }
    return false
}

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
       proc.command.contains("wireguard-go") {
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

private func isTunnelBlocker(_ command: String) -> Bool {
    command.contains("networksetup") || (command.contains("route") && command.contains("monitor"))
}

// The route monitor keeps running after wg-quick returns. On the next connect
// its exit trap deletes the new interface, and wg-quick still exits 0.
func stopManagedProcesses() {
    let targets = fairleadProcessTree()
    guard !targets.isEmpty else { return }
    fputs("fairlead-helper: stopping \(targets.count) leftover tunnel processes\n", stderr)
    for proc in targets where isTunnelBlocker(proc.command) {
        kill(proc.pid, SIGKILL)
    }
    for proc in targets where !isTunnelBlocker(proc.command) {
        kill(proc.pid, SIGTERM)
    }
    let deadline = Date().addingTimeInterval(8)
    while Date() < deadline {
        let live = fairleadProcessTree().filter { !isTunnelBlocker($0.command) }
        if live.isEmpty { break }
        usleep(100_000)
    }
    for proc in fairleadProcessTree() where !isTunnelBlocker(proc.command) {
        kill(proc.pid, SIGKILL)
    }
}

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

private func routeSnapshot(_ cidr: CIDR) -> RouteSnapshot {
    let family = cidr.family == .v4 ? "inet" : "inet6"
    return RouteText.snapshot(from: run(["/sbin/route", "-n", "get", "-\(family)", cidr.string]).out)
}

private func addOwnedRoute(_ cidr: CIDR, runtime: TunnelRuntime) -> String? {
    let gateway = cidr.family == .v4 ? runtime.gatewayV4 : runtime.gatewayV6
    guard let gateway else {
        return FairleadL10n.format("No gateway for %@.", cidr.string)
    }
    let family = cidr.family == .v4 ? "inet" : "inet6"
    let result = run(["/sbin/route", "-q", "-n", "add", "-\(family)", cidr.string, gateway])
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

private func deleteOwnedRoute(_ cidrString: String) {
    guard let cidr = CIDR.parse(cidrString) else { return }
    guard RouteText.isExact(cidr, snapshot: routeSnapshot(cidr)) else { return }
    let family = cidr.family == .v4 ? "inet" : "inet6"
    _ = run(["/sbin/route", "-q", "-n", "delete", "-\(family)", cidr.string])
}

private func applyBypass(slug: String, cidrs: [String], excludeLAN: Bool) -> (warnings: [String], error: String?) {
    guard var runtime = state.tunnels[slug] else {
        return ([], FairleadL10n.text("The tunnel is not running."))
    }
    let planned = BypassPlanner.routes(
        BypassInput(
            rules: cidrs,
            learned: [],
            lan: runtime.lanCIDRs,
            excludeLAN: excludeLAN,
            endpoints: runtime.endpointIPs
        )
    )
    let desired = Set(planned.routes)
    let automatic = Set(runtime.lanCIDRs.compactMap { CIDR.parse($0)?.string })
    for existing in runtime.ownedRoutes where !desired.contains(existing) {
        deleteOwnedRoute(existing)
    }
    var owned: [String] = []
    var failures: [String] = []
    for route in planned.routes {
        guard let cidr = CIDR.parse(route) else { continue }
        let existed = RouteText.isExact(cidr, snapshot: routeSnapshot(cidr))
        let weOwned = runtime.ownedRoutes.contains(route)
        if existed && !weOwned {
            continue
        }
        if existed && weOwned {
            owned.append(route)
            continue
        }
        if let error = addOwnedRoute(cidr, runtime: runtime) {
            let nowExact = RouteText.isExact(cidr, snapshot: routeSnapshot(cidr))
            if nowExact {
                owned.append(route)
            } else if automatic.contains(route) {
                continue
            } else {
                failures.append("\(route): \(error)")
            }
        } else {
            owned.append(route)
        }
    }
    runtime.ownedRoutes = owned
    state.tunnels[slug] = runtime
    saveState()
    if failures.isEmpty {
        return (planned.warnings, nil)
    }
    return (planned.warnings, failures.joined(separator: "\n"))
}

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

private func bringDown(slug: String) -> String? {
    let path = state.tunnels[slug]?.configPath ?? "\(FairleadPaths.directory)/\(slug).conf"
    let realBefore = state.tunnels[slug]?.realInterface
    stopManagedProcesses()
    bringUserspaceDown(slug: slug, interfaceName: realBefore)
    if let owned = state.tunnels[slug]?.ownedRoutes {
        for route in owned { deleteOwnedRoute(route) }
    }
    state.tunnels[slug] = nil
    saveState()
    try? FileManager.default.removeItem(atPath: path)
    fputs("fairlead-helper: tunnel down \(slug)\n", stderr)
    return nil
}

private func bringUp(slug: String, config: String, cidrs: [String], excludeLAN: Bool) -> RPCResponse {
    let tools = ToolLocator.locate()
    guard tools.ready else {
        return RPCResponse(id: "", ok: false, error: FairleadL10n.text("wireguard-go is not included in this copy of Fairlead."), tools: tools)
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
        gatewayIface: network.v4?.0 ?? network.v6?.0
    )
    guard let real = raised.interface else {
        bringUserspaceDown(slug: slug, interfaceName: nil)
        try? FileManager.default.removeItem(atPath: path)
        let message = raised.error ?? FairleadL10n.text("wireguard-go did not create an interface.")
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
    let applied = applyBypass(slug: slug, cidrs: cidrs, excludeLAN: excludeLAN)
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

private func currentDevices() -> [WGDeviceStatus] {
    state.tunnels.values.compactMap { userspaceStatus($0.realInterface) }
}

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

private func snapshotResponse(id: String) -> RPCResponse {
    let managed = state.tunnels.map { managed($0.key, $0.value) }.sorted { $0.name < $1.name }
    return RPCResponse(id: id, ok: true, tools: ToolLocator.locate(), managed: managed, devices: currentDevices())
}

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
            excludeLAN: request.excludeLAN ?? false
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
        let applied = applyBypass(slug: name, cidrs: request.cidrs ?? [], excludeLAN: request.excludeLAN ?? false)
        var response = snapshotResponse(id: request.id)
        response.warnings = applied.warnings
        if let error = applied.error {
            response.ok = false
            response.error = error
        }
        return response
    default:
        return RPCResponse(id: request.id, ok: false, error: FairleadL10n.text("Unknown command."))
    }
}

private func serve(fd: Int32) {
    while true {
        let client = accept(fd, nil, nil)
        if client < 0 { continue }
        autoreleasepool {
            SocketFrame.setTimeout(client, seconds: 5)
            defer { close(client) }
            var locked = false
            do {
                let data = try SocketFrame.read(client)
                let request = try RPCFrame.decode(RPCRequest.self, from: data)
                SocketFrame.setTimeout(client, seconds: 90)
                stateLock.lock()
                locked = true
                let response = handle(request)
                stateLock.unlock()
                locked = false
                let payload = try RPCFrame.encode(response)
                try SocketFrame.write(client, payload)
            } catch {
                if locked { stateLock.unlock() }
                fputs("fairlead-helper: \(error)\n", stderr)
            }
        }
    }
}

signal(SIGHUP, SIG_IGN)
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
