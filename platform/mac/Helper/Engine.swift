//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// Engine.swift -- One mihomo process. mihomo owns the TUN, routes, DNS, WireGuard, and process rules.
//
//===============================================================

import Darwin
import Foundation
import FairleadCore

let mihomoInterface = "mihomo"

/// The private ranges the Exclude LAN switch keeps off the tunnel. Same list as the shared config builder.
let mihomoLANRanges = ["10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "169.254.0.0/16", "fc00::/7", "fe80::/10"]

struct MihomoRules {
    var cidrs: [String]
    var domains: [String]
    var apps: [String]
    var excludeLAN: Bool
    var selected: Bool
}

struct MihomoUp {
    var pid: Int32 = 0
    var secret = ""
    var endpoint = ""
    var error: String?
}

private final class ApiBox: @unchecked Sendable {
    var ok = false
    var status = 0
    var data = Data()
}

private let apiSession: URLSession = {
    let config = URLSessionConfiguration.ephemeral
    config.connectionProxyDictionary = [:]
    config.requestCachePolicy = .reloadIgnoringLocalCacheData
    return URLSession(configuration: config)
}()

private var mihomoProcess: Process?
private var mihomoPID: Int32 = 0
private var mihomoSecret = ""
private var mihomoEndpoint = ""
private var mihomoEndpointIP = ""
private var lastProbeAt: Date?
private var lastOkAt: Date?
private var lastPingAt: Date?
private var lastDelayMs = 0
private var lastPingMs = 0
private var probeFails = 0
private var recovered = false

private let probePath = "/proxies/\(MihomoConfig.proxyName)/delay?url=https%3A%2F%2Fwww.gstatic.com%2Fgenerate_204&timeout=1500&expected=204"
private let probeEvery: TimeInterval = 15
private let probeRetry: TimeInterval = 1
private let probeStale: TimeInterval = 45
private let pingEvery: TimeInterval = 5
private let probeFailRestart = 6

private var configPath: String { FairleadPaths.mihomoDirectory + "/config.yaml" }
private var logPath: String { FairleadPaths.mihomoDirectory + "/mihomo.log" }

/*
===============
makeSecret
Random hex for the controller. It lives in config.yaml and state.json, both root only.
===============
*/
private func makeSecret() -> String {
    var bytes = [UInt8](repeating: 0, count: 16)
    arc4random_buf(&bytes, bytes.count)
    return bytes.map { String(format: "%02x", $0) }.joined()
}

/*
===============
mihomoAPI
One controller request. Nil when nothing answered.
===============
*/
private func mihomoAPI(_ method: String, _ path: String, secret: String, body: String? = nil, timeout: TimeInterval = 2) -> (status: Int, data: Data)? {
    guard let url = URL(string: MihomoConfig.controllerURL + path) else { return nil }
    var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: timeout)
    request.httpMethod = method
    request.setValue("Bearer \(secret)", forHTTPHeaderField: "Authorization")
    if let body {
        request.httpBody = Data(body.utf8)
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
    }

    let box = ApiBox()
    let done = DispatchSemaphore(value: 0)
    let task = apiSession.dataTask(with: request) { data, response, error in
        if error == nil, let http = response as? HTTPURLResponse {
            box.ok = true
            box.status = http.statusCode
            box.data = data ?? Data()
        }

        done.signal()
    }

    task.resume()
    if done.wait(timeout: .now() + timeout + 1) == .timedOut {
        task.cancel()
        return nil
    }

    return box.ok ? (box.status, box.data) : nil
}

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
pidIsMihomo
===============
*/
private func pidIsMihomo(_ pid: Int32) -> Bool {
    guard pidAlive(pid) else { return false }
    let path = run(["/bin/ps", "-p", "\(pid)", "-o", "comm="]).out.trimmingCharacters(in: .whitespacesAndNewlines)
    return (path as NSString).lastPathComponent.contains("mihomo")
}

/*
===============
mihomoRunning
===============
*/
func mihomoRunning() -> Bool {
    if let process = mihomoProcess { return process.isRunning }
    return pidAlive(mihomoPID)
}

/*
===============
mihomoCurrentPID
===============
*/
func mihomoCurrentPID() -> Int32 {
    mihomoPID
}

/*
===============
logTail
The last non-empty line of mihomo.log, for an error message.
===============
*/
private func logTail() -> String {
    guard let data = FileManager.default.contents(atPath: logPath) else { return "" }
    let text = String(decoding: data.suffix(4096), as: UTF8.self)
    return text.split(separator: "\n").last.map { $0.trimmingCharacters(in: .whitespacesAndNewlines) } ?? ""
}

/*
===============
writeYAML
Build config.yaml from the WireGuard text and the rules. Returns an error message on failure.
===============
*/
private func writeYAML(wg: String, rules: MihomoRules, secret: String) -> String? {
    let built = MihomoConfig.build(
        wg: wg,
        selected: rules.selected,
        excludeLAN: rules.excludeLAN,
        cidrs: rules.cidrs,
        domains: rules.domains,
        apps: rules.apps,
        secret: secret
    )
    guard let yaml = built.yaml else { return built.error }
    do {
        try yaml.write(toFile: configPath, atomically: true, encoding: .utf8)
    } catch {
        return FairleadL10n.text("Could not write the config.")
    }

    chmod(configPath, 0o600)
    return nil
}

/*
===============
spawn
mihomo -d workdir -f config.yaml. Output goes to mihomo.log.
===============
*/
private func spawn(binary: String) -> String? {
    let failure = FairleadL10n.text("mihomo could not be started.")
    unlink(logPath)
    guard FileManager.default.createFile(atPath: logPath, contents: nil, attributes: [.posixPermissions: 0o600]),
          let log = FileHandle(forWritingAtPath: logPath) else {
        return failure
    }

    let process = Process()
    process.executableURL = URL(fileURLWithPath: binary)
    process.arguments = ["-d", FairleadPaths.mihomoDirectory, "-f", configPath]
    process.currentDirectoryURL = URL(fileURLWithPath: FairleadPaths.mihomoDirectory)
    process.environment = ["PATH": "/usr/bin:/bin:/usr/sbin:/sbin"]
    process.standardInput = FileHandle.nullDevice
    process.standardOutput = log
    process.standardError = log
    do {
        try process.run()
    } catch {
        try? log.close()
        return error.localizedDescription
    }

    // The child holds its own copy of the descriptor.
    try? log.close()
    mihomoProcess = process
    mihomoPID = process.processIdentifier
    try? "\(mihomoPID)\n".write(toFile: FairleadPaths.mihomoPID, atomically: true, encoding: .utf8)
    chmod(FairleadPaths.mihomoPID, 0o600)
    return nil
}

/*
===============
waitReady
Wait for the controller, then give the TUN a moment to fail loudly. Returns an error message on failure.
===============
*/
private func waitReady() -> String? {
    var answered = false
    for _ in 0..<60 {
        if !mihomoRunning() {
            return FairleadL10n.format("mihomo exited. %@", logTail())
        }

        if let reply = mihomoAPI("GET", "/version", secret: mihomoSecret, timeout: 1), reply.status == 200 {
            answered = true
            break
        }

        usleep(250_000)
    }

    guard answered else {
        return FairleadL10n.format("mihomo did not answer on %@.", MihomoConfig.controller)
    }

    /* Brief pause so a TUN that fails immediately shows up in the log. */
    usleep(400_000)
    if !mihomoRunning() {
        return FairleadL10n.format("mihomo exited. %@", logTail())
    }

    let log = (try? String(contentsOfFile: logPath, encoding: .utf8)) ?? ""
    if log.contains("TUN listening error") || log.contains("Start TUN") {
        return FairleadL10n.format("mihomo could not start the TUN adapter. %@", logTail())
    }

    return nil
}

/*
===============
controllerUp
True when something still answers on the external-controller port.
===============
*/
private func controllerUp() -> Bool {
    guard let url = URL(string: MihomoConfig.controllerURL + "/version") else { return false }
    var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 0.2)
    request.httpMethod = "GET"
    let box = ApiBox()
    let lock = DispatchSemaphore(value: 0)
    let task = apiSession.dataTask(with: request) { _, response, _ in
        if let http = response as? HTTPURLResponse {
            box.ok = true
            box.status = http.statusCode
        }
        lock.signal()
    }
    task.resume()
    _ = lock.wait(timeout: .now() + 0.3)
    task.cancel()
    return box.ok
}

/*
===============
waitControllerGone
After a kill, wait until the old controller releases the port so the next spawn can bind.
===============
*/
private func waitControllerGone(maxSeconds: TimeInterval = 3) {
    let deadline = Date().addingTimeInterval(maxSeconds)
    while Date() < deadline {
        if !controllerUp() { return }
        usleep(100_000)
    }
}

/*
===============
stopProcess
Turn the TUN off through the controller so mihomo drops the adapter and routes, then end the process.
===============
*/
private func stopProcess() {
    let pid = mihomoPID
    if mihomoRunning() {
        _ = mihomoAPI("PATCH", "/configs", secret: mihomoSecret, body: "{\"tun\":{\"enable\":false}}", timeout: 1)
        let quiet = Date().addingTimeInterval(0.5)
        while mihomoRunning(), Date() < quiet {
            usleep(20_000)
        }
    }

    if mihomoRunning() {
        if let process = mihomoProcess {
            process.terminate()
        } else {
            kill(pid, SIGTERM)
        }

        let deadline = Date().addingTimeInterval(4)
        while mihomoRunning(), Date() < deadline {
            usleep(20_000)
        }
    }

    if mihomoRunning() {
        kill(pid, SIGKILL)
        if let process = mihomoProcess {
            process.waitUntilExit()
        } else {
            usleep(200_000)
        }
    }

    mihomoProcess = nil
    mihomoPID = 0
    lastProbeAt = nil
    lastOkAt = nil
    lastPingAt = nil
    lastDelayMs = 0
    lastPingMs = 0
    mihomoEndpointIP = ""
    unlink(FairleadPaths.mihomoPID)
    waitControllerGone()
}

/*
===============
mihomoProtocolLabel
===============
*/
func mihomoProtocolLabel(_ wg: String) -> String {
    Amnezia.isAmnezia(WGDocument.parse(wg)) ? "AmneziaWG" : "WireGuard"
}

/*
===============
mihomoPeerHost
Resolved endpoint IP when known, otherwise the host from Endpoint.
===============
*/
func mihomoPeerHost() -> String? {
    if !mihomoEndpointIP.isEmpty { return mihomoEndpointIP }
    return endpointHost(mihomoEndpoint)
}

/*
===============
peerEndpoint
The first peer's Endpoint, for the status line.
===============
*/
private func peerEndpoint(_ wg: String) -> String {
    let document = WGDocument.parse(wg)
    guard let peer = document.peerIndexes.first else { return "" }
    return document.peerEndpoint(peer)
}

/*
===============
mihomoUp
Write config.yaml, start mihomo, and wait until the controller and TUN are up.
===============
*/
func mihomoUp(wg: String, rules: MihomoRules) -> MihomoUp {
    stopProcess()
    guard let binary = ToolLocator.locate().mihomo else {
        return MihomoUp(error: FairleadL10n.text("mihomo is not included in this copy of Fairlead."))
    }

    try? FileManager.default.createDirectory(atPath: FairleadPaths.mihomoDirectory, withIntermediateDirectories: true)
    chmod(FairleadPaths.mihomoDirectory, 0o700)
    let secret = makeSecret()
    if let error = writeYAML(wg: wg, rules: rules, secret: secret) {
        return MihomoUp(error: error)
    }

    mihomoSecret = secret
    mihomoEndpoint = peerEndpoint(wg)
    mihomoEndpointIP = resolveEndpointIP(mihomoEndpoint)
    if let error = spawn(binary: binary) {
        return MihomoUp(error: error)
    }

    if let error = waitReady() {
        stopProcess()
        return MihomoUp(error: error)
    }

    recovered = false
    probeFails = 0
    fputs("fairlead-helper: mihomo \(mihomoPID) up\n", stderr)
    return MihomoUp(pid: mihomoPID, secret: secret, endpoint: mihomoEndpoint, error: nil)
}

/*
===============
mihomoDown
Stop mihomo. It drops the adapter and routes with it.
===============
*/
func mihomoDown() {
    stopProcess()
    mihomoSecret = ""
    mihomoEndpoint = ""
    mihomoEndpointIP = ""
    probeFails = 0
    recovered = false
}

/*
===============
reload
Ask the running mihomo to read config.yaml again.
===============
*/
private func reload() -> Bool {
    guard let body = try? JSONSerialization.data(withJSONObject: ["path": configPath]),
          let text = String(data: body, encoding: .utf8),
          let reply = mihomoAPI("PUT", "/configs?force=true", secret: mihomoSecret, body: text, timeout: 5) else {
        return false
    }

    return reply.status >= 200 && reply.status < 300
}

/*
===============
mihomoReload
Rewrite config.yaml with the new rules and reload mihomo. If the reload is refused, restart it.
===============
*/
func mihomoReload(wg: String, rules: MihomoRules) -> String? {
    guard mihomoPID > 0 else { return FairleadL10n.text("The tunnel is not running.") }
    if let error = writeYAML(wg: wg, rules: rules, secret: mihomoSecret) { return error }
    if mihomoRunning(), reload() { return nil }

    fputs("fairlead-helper: mihomo reload failed, restarting\n", stderr)
    stopProcess()
    guard let binary = ToolLocator.locate().mihomo, spawn(binary: binary) == nil, waitReady() == nil else {
        stopProcess()
        return FairleadL10n.text("mihomo could not be restarted.")
    }

    return nil
}

/*
===============
total
An unsigned number after a JSON key.
===============
*/
private func total(_ text: String, _ key: String) -> UInt64 {
    guard let range = text.range(of: key) else { return 0 }
    return UInt64(text[range.upperBound...].prefix { $0.isNumber }) ?? 0
}

/*
===============
probeDelay
HTTP probe through the WireGuard outbound. 0 when the tunnel does not answer.
===============
*/
private func probeDelay() -> Int {
    guard let reply = mihomoAPI("GET", probePath, secret: mihomoSecret, timeout: 4), reply.status == 200 else {
        return 0
    }

    let text = String(decoding: reply.data, as: UTF8.self)
    guard let range = text.range(of: "\"delay\":") else { return 0 }
    let value = text[range.upperBound...].prefix { $0.isNumber }
    return Int(value) ?? 0
}

/*
===============
endpointHost
host from host:port or [v6]:port.
===============
*/
private func endpointHost(_ endpoint: String) -> String? {
    if endpoint.hasPrefix("["), let end = endpoint.firstIndex(of: "]") {
        let start = endpoint.index(after: endpoint.startIndex)
        guard start < end else { return nil }
        return String(endpoint[start..<end])
    }

    guard let colon = endpoint.lastIndex(of: ":"), colon > endpoint.startIndex else { return nil }
    return String(endpoint[..<colon])
}

/*
===============
resolveEndpointIP
DNS lookup before TUN is up. IPv4 only, for ICMP ping after connect.
===============
*/
private func resolveEndpointIP(_ endpoint: String) -> String {
    guard let host = endpointHost(endpoint), !host.isEmpty else { return "" }
    var hints = addrinfo()
    hints.ai_family = AF_INET
    var result: UnsafeMutablePointer<addrinfo>?
    guard getaddrinfo(host, nil, &hints, &result) == 0, let first = result else { return "" }
    defer { freeaddrinfo(first) }
    var buf = [CChar](repeating: 0, count: Int(NI_MAXHOST))
    guard getnameinfo(first.pointee.ai_addr, first.pointee.ai_addrlen, &buf, socklen_t(buf.count), nil, 0, NI_NUMERICHOST) == 0 else {
        return ""
    }
    return String(cString: buf)
}

/*
===============
pingEndpoint
ICMP echo to the pre-resolved endpoint IP. No DNS after connect.
===============
*/
private func pingEndpoint() -> Int {
    guard !mihomoEndpointIP.isEmpty else { return 0 }
    let process = Process()
    process.executableURL = URL(fileURLWithPath: "/sbin/ping")
    process.arguments = ["-c", "1", "-W", "800", mihomoEndpointIP]
    let pipe = Pipe()
    process.standardOutput = pipe
    process.standardError = FileHandle.nullDevice
    do {
        try process.run()
        process.waitUntilExit()
    } catch {
        return 0
    }

    guard process.terminationStatus == 0 else { return 0 }
    let text = String(decoding: pipe.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
    guard let range = text.range(of: "time=") else { return 0 }
    var num = ""
    for ch in text[range.upperBound...] {
        if ch.isNumber || ch == "." {
            num.append(ch)
        } else if !num.isEmpty {
            break
        }
    }

    guard let ms = Double(num) else { return 0 }
    let rounded = Int(ms.rounded())
    return rounded > 0 ? rounded : 1
}

/*
===============
mihomoStatus
Counters, HTTP probe for tunnel liveness, ICMP ping for the UI (only after the tunnel is up).
Pass probe: false right after up() so Connected still waits on later status ticks.
===============
*/
func mihomoStatus(probe: Bool = true) -> WGDeviceStatus? {
    guard mihomoPID > 0, mihomoRunning() else {
        lastProbeAt = nil
        lastOkAt = nil
        lastDelayMs = 0
        lastPingMs = 0
        probeFails = 0
        return nil
    }

    var rx: UInt64 = 0
    var tx: UInt64 = 0
    if let reply = mihomoAPI("GET", "/connections", secret: mihomoSecret), reply.status == 200 {
        let head = String(decoding: reply.data.prefix(512), as: UTF8.self)
        rx = total(head, "\"downloadTotal\":")
        tx = total(head, "\"uploadTotal\":")
    }

    let now = Date()
    var stage = ""
    if probe {
        let gap = lastOkAt == nil ? probeRetry : probeEvery
        let needProbe = lastProbeAt == nil || now.timeIntervalSince(lastProbeAt!) >= gap
        if lastOkAt == nil {
            stage = FairleadL10n.text(lastProbeAt == nil ? "Opening the tunnel…" : "Checking the tunnel…")
        }
        if needProbe {
            lastProbeAt = now
            stage = FairleadL10n.text("Checking the tunnel…")
            let delay = probeDelay()
            if delay > 0 {
                lastOkAt = now
                lastDelayMs = delay
                probeFails = 0
            } else if lastOkAt == nil {
                probeFails += 1
                if probeFails >= probeFailRestart, !recovered {
                    fputs("fairlead-helper: tunnel not reachable, restarting mihomo\n", stderr)
                    recovered = true
                    probeFails = 0
                    lastProbeAt = nil
                    let binary = ToolLocator.locate().mihomo
                    stopProcess()
                    if let binary, spawn(binary: binary) == nil {
                        stage = FairleadL10n.text("Opening the tunnel…")
                    }
                }
            }
        }
    } else if lastOkAt == nil {
        stage = FairleadL10n.text("Opening the tunnel…")
    }

    var handshake = 0
    var delay = 0
    var ping = 0
    if let ok = lastOkAt, now.timeIntervalSince(ok) <= probeStale {
        handshake = Int(ok.timeIntervalSince1970)
        delay = lastDelayMs
        stage = ""
        let needPing = lastPingAt == nil || now.timeIntervalSince(lastPingAt!) >= pingEvery
        if needPing {
            lastPingAt = now
            let sample = pingEndpoint()
            if sample > 0 {
                lastPingMs = sample
            }
        }
        ping = lastPingMs
    } else {
        lastOkAt = nil
        lastDelayMs = 0
    }

    let peer = WGPeerStatus(
        publicKey: "",
        endpoint: mihomoEndpoint,
        allowedIPs: "",
        handshake: handshake,
        rx: rx,
        tx: tx,
        keepalive: 0,
        delay: delay,
        ping: ping,
        stage: stage
    )
    return WGDeviceStatus(interface: mihomoInterface, publicKey: "", listenPort: 0, peers: [peer])
}

/*
===============
mihomoAdopt
After a helper restart, take over the mihomo that is still running. False when it is gone or does not answer.
===============
*/
func mihomoAdopt(pid: Int32, secret: String, endpoint: String) -> Bool {
    guard pidIsMihomo(pid) else { return false }
    guard let reply = mihomoAPI("GET", "/version", secret: secret, timeout: 1), reply.status == 200 else { return false }
    mihomoProcess = nil
    mihomoPID = pid
    mihomoSecret = secret
    mihomoEndpoint = endpoint
    return true
}

/*
===============
mihomoReapOrphans
End a mihomo a previous helper left behind. Only the recorded pid is touched.
===============
*/
func mihomoReapOrphans() {
    defer { if mihomoPID == 0 { unlink(FairleadPaths.mihomoPID) } }
    guard let text = try? String(contentsOfFile: FairleadPaths.mihomoPID, encoding: .utf8),
          let pid = Int32(text.trimmingCharacters(in: .whitespacesAndNewlines)),
          pid != mihomoPID, pidIsMihomo(pid) else { return }
    fputs("fairlead-helper: ending an orphan mihomo \(pid)\n", stderr)
    kill(pid, SIGTERM)
    let deadline = Date().addingTimeInterval(3)
    while pidAlive(pid), Date() < deadline {
        usleep(20_000)
    }

    if pidAlive(pid) { kill(pid, SIGKILL) }
}
