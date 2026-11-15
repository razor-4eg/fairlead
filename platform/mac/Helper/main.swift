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
    var pid: Int32
    var secret: String
    var endpoint: String
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
/// Serializes up / down / bypass / quit so a reload can run while status waits its turn.
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
posix_spawn instead of Foundation.Process for the short helper commands (ps, route, networksetup).
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

struct ListedProcess {
    var pid: Int32
    var ppid: Int32
    var command: String
}

/*
===============
processList
===============
*/
func processList() -> [ListedProcess] {
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
configFile
The sanitized WireGuard text for one tunnel. It stays on disk while the tunnel is up, so a reload can rebuild config.yaml.
===============
*/
private func configFile(_ slug: String) -> String {
    "\(FairleadPaths.directory)/\(slug).conf"
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

    // Hooks would run as root. Fairlead never executes them.
    let hooks: Set<String> = ["preup", "postup", "predown", "postdown", "saveconfig"]
    for index in document.sections.indices {
        document.sections[index].entries.removeAll { entry in
            guard let key = entry.key?.lowercased() else { return false }
            return hooks.contains(key)
        }
    }

    let path = configFile(slug)
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
rules
The route rules from an up or bypass request. Domains and apps go to mihomo by name and path.
===============
*/
private func rules(_ request: RPCRequest) -> MihomoRules {
    let domains = (request.domains ?? []).compactMap { DomainName.normalize($0) }
    let apps = (request.apps ?? []).filter { path in
        path.hasPrefix("/") && !path.contains(",") && !path.unicodeScalars.contains { $0.value < 32 }
    }

    return MihomoRules(
        cidrs: Array((request.cidrs ?? []).prefix(4096)),
        domains: Array(domains.prefix(4096)),
        apps: Array(apps.prefix(64)),
        excludeLAN: request.excludeLAN ?? false,
        selected: (RoutePolicy(rawValue: request.policy ?? "") ?? .exceptions) == .selected
    )
}

/*
===============
bringDown
===============
*/
private func bringDown(slug: String) -> String? {
    mihomoDown()
    state.tunnels[slug] = nil
    saveState()
    try? FileManager.default.removeItem(atPath: configFile(slug))
    fputs("fairlead-helper: tunnel down \(slug)\n", stderr)
    return nil
}

/*
===============
bringUp
There is one mihomo process, so any tunnel that is up comes down first.
===============
*/
private func bringUp(slug: String, config: String, rules: MihomoRules) -> RPCResponse {
    let tools = ToolLocator.locate()
    guard tools.ready else {
        return RPCResponse(id: "", ok: false, error: FairleadL10n.text("This copy of Fairlead is missing the tunnel engine."), tools: tools)
    }

    for other in Array(state.tunnels.keys) {
        _ = bringDown(slug: other)
    }

    let path: String
    do {
        path = try writeConfig(slug: slug, config: config)
    } catch {
        return RPCResponse(id: "", ok: false, error: error.localizedDescription, tools: tools)
    }

    let wg = (try? String(contentsOfFile: path, encoding: .utf8)) ?? config
    let raised = mihomoUp(wg: wg, rules: rules)
    if let error = raised.error {
        mihomoDown()
        try? FileManager.default.removeItem(atPath: path)
        return RPCResponse(id: "", ok: false, error: error, tools: tools)
    }

    let runtime = TunnelRuntime(pid: raised.pid, secret: raised.secret, endpoint: raised.endpoint)
    state.tunnels[slug] = runtime
    saveState()
    return RPCResponse(
        id: "",
        ok: true,
        tools: tools,
        managed: [managed(slug, runtime)],
        devices: currentDevices(probe: false)
    )
}

/*
===============
applyBypass
Rewrite config.yaml with the new rules and reload mihomo.
===============
*/
private func applyBypass(slug: String, rules: MihomoRules) -> String? {
    guard state.tunnels[slug] != nil,
          let wg = try? String(contentsOfFile: configFile(slug), encoding: .utf8) else {
        return FairleadL10n.text("The tunnel is not running.")
    }

    if let error = mihomoReload(wg: wg, rules: rules) {
        if !mihomoRunning() { _ = bringDown(slug: slug) }
        return error
    }

    // A restart inside the reload gives mihomo a new pid.
    state.tunnels[slug]?.pid = mihomoCurrentPID()
    saveState()
    return nil
}

/*
===============
managed
===============
*/
private func managed(_ slug: String, _ runtime: TunnelRuntime) -> ManagedTunnel {
    _ = runtime
    let wg = (try? String(contentsOfFile: configFile(slug), encoding: .utf8)) ?? ""
    return ManagedTunnel(
        name: slug,
        realInterface: mihomoProtocolLabel(wg),
        gatewayV4: mihomoPeerHost(),
        lanCIDRs: mihomoLANRanges
    )
}

/*
===============
currentDevices
===============
*/
private func currentDevices(probe: Bool = true) -> [WGDeviceStatus] {
    guard !state.tunnels.isEmpty, let device = mihomoStatus(probe: probe) else { return [] }
    return [device]
}

/*
===============
reapDead
A tunnel whose mihomo is gone is dropped, so the app sees it as off.
===============
*/
private func reapDead() {
    guard !state.tunnels.isEmpty, !mihomoRunning() else { return }
    for slug in Array(state.tunnels.keys) {
        _ = bringDown(slug: slug)
    }
}

/*
===============
reconcile
On helper start: clear the old userspace tunnel, take over a mihomo that is still running, end any other.
===============
*/
private func reconcile() {
    cleanupLegacyTunnels()
    loadState()
    var kept: [String: TunnelRuntime] = [:]
    for (slug, runtime) in state.tunnels.sorted(by: { $0.key < $1.key }) where kept.isEmpty {
        let hasConfig = FileManager.default.fileExists(atPath: configFile(slug))
        if hasConfig, mihomoAdopt(pid: runtime.pid, secret: runtime.secret, endpoint: runtime.endpoint) {
            kept[slug] = runtime
        }
    }

    state.tunnels = kept
    mihomoReapOrphans()
    if let files = try? FileManager.default.contentsOfDirectory(atPath: FairleadPaths.directory) {
        for file in files where file.hasSuffix(".conf") && !kept.keys.contains(String(file.dropLast(5))) {
            try? FileManager.default.removeItem(atPath: "\(FairleadPaths.directory)/\(file)")
        }
    }

    saveState()
}

/*
===============
snapshotResponse
===============
*/
private func snapshotResponse(id: String) -> RPCResponse {
    reapDead()
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
        return snapshotResponse(id: request.id)
    case "up":
        guard let name = request.name, validSlug(name) else {
            return RPCResponse(id: request.id, ok: false, error: FairleadL10n.text("Invalid tunnel name."))
        }

        guard let config = request.config, !config.isEmpty else {
            return RPCResponse(id: request.id, ok: false, error: FairleadL10n.text("The config is empty."))
        }

        var response = bringUp(slug: name, config: config, rules: rules(request))
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

        let error = applyBypass(slug: name, rules: rules(request))
        let tunnels = state.tunnels.keys.sorted().compactMap { slug -> ManagedTunnel? in
            guard let runtime = state.tunnels[slug] else { return nil }
            return managed(slug, runtime)
        }
        var response = RPCResponse(
            id: request.id,
            ok: error == nil,
            error: error,
            tools: ToolLocator.locate(),
            managed: tunnels,
            devices: currentDevices(probe: false)
        )
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
up / down / bypass / quit must not interleave; status stays responsive between them.
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
