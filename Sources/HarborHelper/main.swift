import Darwin
import Foundation
import HarborCore

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

private let userspaceShimPath = "/usr/local/libexec/harbor-wg-go"
private let stateLock = NSLock()
private var state = HelperState(tunnels: [:])

private func fail(_ text: String) -> Never {
    fputs("harbor-helper: \(text)\n", stderr)
    exit(1)
}

private func refreshHelperLanguage() {
    let bundleURL = URL(fileURLWithPath: "/usr/local/libexec/HarborLocalizations.bundle")
    if let bundle = Bundle(url: bundleURL) {
        HarborL10n.bundle = bundle
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
    HarborL10n.languageOverride = first
}

private func username() -> String {
    let args = CommandLine.arguments
    if let index = args.firstIndex(of: "--user"), index + 1 < args.count {
        return args[index + 1]
    }
    fail(HarborL10n.text("--user is required."))
}

private func run(_ arguments: [String]) -> CommandResult {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: arguments[0])
    process.arguments = Array(arguments.dropFirst())
    var environment = ProcessInfo.processInfo.environment
    environment["PATH"] = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
    if arguments[0].contains("wg-quick") {
        environment["WG_QUICK_USERSPACE_IMPLEMENTATION"] = userspaceShimPath
    }
    process.environment = environment
    let stdout = Pipe()
    let stderr = Pipe()
    process.standardOutput = stdout
    process.standardError = stderr
    do {
        try process.run()
    } catch {
        return CommandResult(code: 127, out: "", err: error.localizedDescription)
    }
    process.waitUntilExit()
    let out = String(data: stdout.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
    let err = String(data: stderr.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
    return CommandResult(code: process.terminationStatus, out: out, err: err)
}

private func loadState() {
    guard let data = FileManager.default.contents(atPath: HarborPaths.state),
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
    let url = URL(fileURLWithPath: HarborPaths.state)
    try? data.write(to: url, options: .atomic)
    chmod(HarborPaths.state, 0o600)
}

private func validSlug(_ name: String) -> Bool {
    guard name.count == 11, name.hasPrefix("hbr") else { return false }
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

private func interfaceExists(_ name: String) -> Bool {
    guard !name.isEmpty else { return false }
    let result = run(["/sbin/ifconfig", name])
    return result.code == 0 && result.out.contains("flags=")
}

private func endpointIPs(realInterface: String) -> [String] {
    guard let wg = ToolLocator.locate().wg else { return [] }
    let dump = WGDump.parse(run([wg, "show", "all", "dump"]).out)
    guard let device = dump.first(where: { $0.interface == realInterface }) else { return [] }
    var ips: [String] = []
    for peer in device.peers {
        guard let host = EndpointParse.host(from: peer.endpoint), let ip = IPAddress.parse(host) else { continue }
        ips.append(ip.string)
    }
    return RouteText.unique(ips)
}

private func ensureUserspaceShim() {
    let script = """
    #!/bin/bash
    set -euo pipefail
    GO=""
    for candidate in /opt/homebrew/bin/wireguard-go /usr/local/bin/wireguard-go; do
      if [[ -x "$candidate" ]]; then
        GO="$candidate"
        break
      fi
    done
    if [[ -z "$GO" ]]; then
      echo "wireguard-go not found" >&2
      exit 1
    fi
    namefile="${WG_TUN_NAME_FILE:-}"
    log=/var/log/harbor-wireguard-go.log
    pidfile=/var/run/harbor/wireguard-go.pid
    mkdir -p /var/run/harbor
    export LOG_LEVEL=error
    /usr/bin/python3 - "$GO" "${1:-utun}" "$log" "$pidfile" << 'PY'
    import os, sys
    go, iface, log, pidfile = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    first = os.fork()
    if first > 0:
        os.waitpid(first, 0)
        os._exit(0)
    os.setsid()
    child = os.fork()
    if child > 0:
        with open(pidfile, "w") as handle:
            handle.write(str(child))
        os._exit(0)
    fd = os.open(log, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
    os.dup2(fd, 1)
    os.dup2(fd, 2)
    null = os.open("/dev/null", os.O_RDONLY)
    os.dup2(null, 0)
    os.execv(go, [go, "-f", iface])
    PY
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40; do
      if [[ -n "$namefile" && -f "$namefile" ]]; then
        iface="$(tr -d '[:space:]' < "$namefile")"
        if [[ -n "$iface" && -S "/var/run/wireguard/${iface}.sock" ]]; then
          exit 0
        fi
      fi
      sleep 0.05
    done
    if [[ -f "$pidfile" ]]; then
      kill "$(cat "$pidfile")" 2>/dev/null || true
    fi
    echo "wireguard-go did not create a socket" >&2
    tail -n 30 "$log" >&2 || true
    exit 1
    """
    try? script.write(toFile: userspaceShimPath, atomically: true, encoding: .utf8)
    chmod(userspaceShimPath, 0o755)
}

private func waitForInterface(_ slug: String) -> String? {
    for _ in 0..<20 {
        if let name = realInterfaceName(slug: slug), interfaceExists(name) {
            return name
        }
        usleep(50_000)
    }
    return realInterfaceName(slug: slug)
}

private func realInterfaceName(slug: String) -> String? {
    let path = "\(HarborPaths.wireguardRun)/\(slug).name"
    guard let text = try? String(contentsOfFile: path, encoding: .utf8) else { return nil }
    let name = text.trimmingCharacters(in: .whitespacesAndNewlines)
    return name.isEmpty ? nil : name
}

private func routeSnapshot(_ cidr: CIDR) -> RouteSnapshot {
    let family = cidr.family == .v4 ? "inet" : "inet6"
    return RouteText.snapshot(from: run(["/sbin/route", "-n", "get", "-\(family)", cidr.string]).out)
}

private func addOwnedRoute(_ cidr: CIDR, runtime: TunnelRuntime) -> String? {
    let gateway = cidr.family == .v4 ? runtime.gatewayV4 : runtime.gatewayV6
    guard let gateway else {
        return HarborL10n.format("No gateway for %@.", cidr.string)
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
    return detail.isEmpty ? HarborL10n.format("Could not add route %@.", cidr.string) : detail
}

private func deleteOwnedRoute(_ cidrString: String) {
    guard let cidr = CIDR.parse(cidrString) else { return }
    guard RouteText.isExact(cidr, snapshot: routeSnapshot(cidr)) else { return }
    let family = cidr.family == .v4 ? "inet" : "inet6"
    _ = run(["/sbin/route", "-q", "-n", "delete", "-\(family)", cidr.string])
}

private func applyBypass(slug: String, cidrs: [String], excludeLAN: Bool) -> (warnings: [String], error: String?) {
    guard var runtime = state.tunnels[slug] else {
        return ([], HarborL10n.text("The tunnel is not running."))
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
    guard config.utf8.count <= 256_000 else { throw HelperError.message(HarborL10n.text("The config is too large.")) }
    guard !config.contains("\0") else { throw HelperError.message(HarborL10n.text("The config is damaged.")) }
    var document = WGDocument.parse(config)
    let issues = ConfigCheck.issues(in: document)
    if let first = issues.first {
        throw HelperError.message(first.message)
    }
    // Hooks run as root inside wg-quick. Harbor never executes them.
    let hooks: Set<String> = ["preup", "postup", "predown", "postdown", "saveconfig"]
    for index in document.sections.indices {
        document.sections[index].entries.removeAll { entry in
            guard let key = entry.key?.lowercased() else { return false }
            return hooks.contains(key)
        }
    }
    let path = "\(HarborPaths.directory)/\(slug).conf"
    do {
        try document.render().write(toFile: path, atomically: true, encoding: .utf8)
    } catch {
        throw HelperError.message(HarborL10n.text("Could not write the config."))
    }
    chmod(path, 0o600)
    return path
}

private func bringDown(slug: String) -> String? {
    let tools = ToolLocator.locate()
    guard let quick = tools.wgQuick else {
        return HarborL10n.text("wg-quick was not found. Install wireguard-tools.")
    }
    let path = state.tunnels[slug]?.configPath ?? "\(HarborPaths.directory)/\(slug).conf"
    if FileManager.default.fileExists(atPath: path) || interfaceExists(state.tunnels[slug]?.realInterface ?? "") {
        let result = run([quick, "down", path])
        if result.code != 0 && !result.blob.localizedCaseInsensitiveContains("is not a wireguard interface") {
            let real = state.tunnels[slug]?.realInterface
            if let real, interfaceExists(real) {
                return tail(result.blob)
            }
        }
    }
    if let owned = state.tunnels[slug]?.ownedRoutes {
        for route in owned { deleteOwnedRoute(route) }
    }
    state.tunnels[slug] = nil
    saveState()
    try? FileManager.default.removeItem(atPath: path)
    let nameFile = "\(HarborPaths.wireguardRun)/\(slug).name"
    try? FileManager.default.removeItem(atPath: nameFile)
    return nil
}

private func bringUp(slug: String, config: String, cidrs: [String], excludeLAN: Bool) -> RPCResponse {
    let tools = ToolLocator.locate()
    guard tools.ready, let quick = tools.wgQuick else {
        return RPCResponse(id: "", ok: false, error: HarborL10n.text("wg, wg-quick, and wireguard-go are required. Install the wireguard-tools formula."), tools: tools)
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
    ensureUserspaceShim()
    let result = run([quick, "up", path])
    fputs("harbor-helper: wg-quick up \(result.code)\n\(result.blob)\n", stderr)
    if result.code != 0 {
        _ = run([quick, "down", path])
        try? FileManager.default.removeItem(atPath: path)
        return RPCResponse(id: "", ok: false, error: tail(result.blob), tools: tools)
    }
    guard let real = waitForInterface(slug), interfaceExists(real) else {
        let detail = tail(result.blob)
        _ = run([quick, "down", path])
        let message = HarborL10n.text("The tunnel came up, but the interface was not found.")
        let error = detail.isEmpty ? message : message + "\n" + detail
        return RPCResponse(id: "", ok: false, error: error, tools: tools)
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
            error: HarborL10n.format("The tunnel is up, but some direct routes were not applied.\n%@", error),
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
    guard let wg = ToolLocator.locate().wg else { return [] }
    return WGDump.parse(run([wg, "show", "all", "dump"]).out)
}

private func tail(_ text: String) -> String {
    let lines = text.split(whereSeparator: \.isNewline).map(String.init).filter { line in
        !line.localizedCaseInsensitiveContains("privatekey")
    }
    let kept = lines.suffix(12)
    let joined = kept.joined(separator: "\n").trimmingCharacters(in: .whitespacesAndNewlines)
    return joined.isEmpty ? HarborL10n.text("wg-quick failed.") : joined
}

private func reconcile() {
    loadState()
    let alive = state.tunnels.filter { interfaceExists($0.value.realInterface) }
    let dead = state.tunnels.filter { !interfaceExists($0.value.realInterface) }
    for (_, runtime) in dead {
        for route in runtime.ownedRoutes { deleteOwnedRoute(route) }
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
            return RPCResponse(id: request.id, ok: false, error: HarborL10n.text("Invalid tunnel name."))
        }
        guard let config = request.config, !config.isEmpty else {
            return RPCResponse(id: request.id, ok: false, error: HarborL10n.text("The config is empty."))
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
            return RPCResponse(id: request.id, ok: false, error: HarborL10n.text("Invalid tunnel name."))
        }
        if let error = bringDown(slug: name) {
            return RPCResponse(id: request.id, ok: false, error: error)
        }
        return snapshotResponse(id: request.id)
    case "bypass":
        guard let name = request.name, validSlug(name) else {
            return RPCResponse(id: request.id, ok: false, error: HarborL10n.text("Invalid tunnel name."))
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
        return RPCResponse(id: request.id, ok: false, error: HarborL10n.text("Unknown command."))
    }
}

private func serve(fd: Int32) {
    while true {
        let client = accept(fd, nil, nil)
        if client < 0 {
            if errno == EINTR { continue }
            continue
        }
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
            fputs("harbor-helper: \(error)\n", stderr)
        }
    }
}

signal(SIGHUP, SIG_IGN)
setbuf(stdout, nil)
setbuf(stderr, nil)
let user = username()
refreshHelperLanguage()
guard geteuid() == 0 else { fail(HarborL10n.text("Run the helper with administrator privileges.")) }
guard let account = getpwnam(user) else { fail(HarborL10n.format("User %@ was not found.", user)) }
let uid = account.pointee.pw_uid
let gid = account.pointee.pw_gid

try? FileManager.default.createDirectory(atPath: HarborPaths.directory, withIntermediateDirectories: true)
chmod(HarborPaths.directory, 0o755)
reconcile()

let fd: Int32
do {
    fd = try SocketFrame.bind(path: HarborPaths.socket)
} catch {
    fail(String(describing: error))
}
chown(HarborPaths.socket, uid, gid)
chmod(HarborPaths.socket, 0o600)
try? "\(getpid())\n".write(toFile: "/var/run/harbor/helper.pid", atomically: true, encoding: .utf8)
fputs("harbor-helper: listening for \(user)\n", stderr)
serve(fd: fd)
