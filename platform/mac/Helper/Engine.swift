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
    var blockQUIC: Bool
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
/// First IPv4 from Interface Address — used to confirm system routes hit our TUN, not another VPN.
private var mihomoTunnelIPv4 = ""
private var deviceLabel = mihomoInterface
private var lastProbeAt: Date?
private var lastOkAt: Date?
private var lastPingAt: Date?
private var lastDelayMs = 0
private var lastPingMs = 0
private var probeFails = 0
private var recoveries = 0
private var recoverUntil: Date?
/// ICMP to the WireGuard endpoint (excluded from TUN). Off the status RPC path so polls stay cheap.
private var pingTimer: DispatchSourceTimer?
private let pingQueue = DispatchQueue(label: "fairlead.endpoint-ping")

private let probePath = "/proxies/\(MihomoConfig.proxyName)/delay?url=https%3A%2F%2Fwww.gstatic.com%2Fgenerate_204&timeout=1500&expected=204"
private let probeEvery: TimeInterval = 15
private let probeRetry: TimeInterval = 1
private let probeStale: TimeInterval = 45
private let pingEvery: TimeInterval = 3
private let probeFailRestart = 6
private let probeMaxRecover = 3

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
        blockQUIC: rules.blockQUIC,
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
    let failure = FairleadL10n.text("The tunnel could not be started.")
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
tunEnabled
True when GET /configs reports tun.enable. WireGuard can answer delay probes with TUN off.
===============
*/
private func tunEnabled() -> Bool {
    guard let reply = mihomoAPI("GET", "/configs", secret: mihomoSecret, timeout: 2), reply.status == 200 else {
        return false
    }

    let text = String(decoding: reply.data, as: UTF8.self)
    guard let tun = text.range(of: "\"tun\"") else { return false }
    let window = text[tun.lowerBound...]
    guard let enable = window.range(of: "\"enable\""),
          window.distance(from: window.startIndex, to: enable.lowerBound) < 400 else {
        return false
    }

    var value = window[enable.upperBound...]
    while value.first == " " || value.first == "\t" || value.first == ":" {
        value = value.dropFirst()
    }

    return value.hasPrefix("true")
}

/*
===============
tunnelIPv4
First IPv4 token from Interface Address (strip /prefix). Kept for diagnostics / future checks.
===============
*/
private func tunnelIPv4(from wg: String) -> String {
    let raw = WGDocument.parse(wg).interfaceAddresses
    for token in raw.split(whereSeparator: { $0 == "," || $0 == " " || $0 == "\t" }) {
        let piece = token.split(separator: "/").first.map(String.init) ?? ""
        if piece.contains("."), !piece.contains(":") {
            return piece
        }
    }
    return ""
}

/*
===============
tunDeviceName
mihomo's tun.device from GET /configs when present (e.g. utun4).
===============
*/
private func tunDeviceName() -> String? {
    guard let reply = mihomoAPI("GET", "/configs", secret: mihomoSecret, timeout: 2), reply.status == 200 else {
        return nil
    }

    let text = String(decoding: reply.data, as: UTF8.self)
    guard let tun = text.range(of: "\"tun\"") else { return nil }
    let window = text[tun.lowerBound...]
    guard let device = window.range(of: "\"device\""),
          window.distance(from: window.startIndex, to: device.lowerBound) < 800 else {
        return nil
    }

    var value = window[device.upperBound...]
    while value.first == " " || value.first == "\t" || value.first == ":" {
        value = value.dropFirst()
    }

    guard value.first == "\"" else { return nil }
    value = value.dropFirst()
    let name = value.prefix(while: { $0 != "\"" })
    let trimmed = String(name).trimmingCharacters(in: .whitespacesAndNewlines)
    return trimmed.isEmpty ? nil : trimmed
}

/*
===============
tunRoutesActive
True when general IPv4 traffic is steered into a utun (mihomo auto-route).
Do not require Interface Address on that utun — mihomo keeps the WireGuard IP inside the
outbound stack; the system TUN often has a different address (unlike Amnezia's utun).
===============
*/
private func tunRoutesActive() -> Bool {
    let route = run(["/sbin/route", "-n", "get", "1.1.1.1"])
    guard route.code == 0 else { return false }

    var iface = ""
    for line in route.out.split(separator: "\n") {
        let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
        guard trimmed.hasPrefix("interface:") else { continue }
        iface = trimmed.dropFirst("interface:".count).trimmingCharacters(in: .whitespacesAndNewlines)
        break
    }

    guard iface.hasPrefix("utun") else { return false }
    if let device = tunDeviceName(), !device.isEmpty {
        return iface == device
    }

    return true
}

/*
===============
tunnelPathReady
TUN is enabled in mihomo and the system default path actually enters that adapter.
===============
*/
private func tunnelPathReady() -> Bool {
    tunEnabled() && tunRoutesActive()
}

/*
===============
waitReady
Wait for the controller, tun.enable, and system routes onto our utun.
===============
*/
private func waitReady() -> String? {
    var answered = false
    for _ in 0..<60 {
        if !mihomoRunning() {
            return FairleadL10n.format("The tunnel exited. %@", logTail())
        }

        if let reply = mihomoAPI("GET", "/version", secret: mihomoSecret, timeout: 1), reply.status == 200 {
            answered = true
            break
        }

        usleep(250_000)
    }

    guard answered else {
        return FairleadL10n.format("The tunnel did not answer on %@.", MihomoConfig.controller)
    }

    /* mihomo retries TUN for ~45s before giving up and leaving tun.enable false. */
    for _ in 0..<100 {
        if !mihomoRunning() {
            return FairleadL10n.format("The tunnel exited. %@", logTail())
        }

        if tunnelPathReady() {
            return nil
        }

        let log = (try? String(contentsOfFile: logPath, encoding: .utf8)) ?? ""
        if log.contains("TUN listening error") {
            return FairleadL10n.format("The tunnel could not start the network adapter. %@", logTail())
        }

        usleep(500_000)
    }

    if tunEnabled() {
        return FairleadL10n.text("The tunnel adapter is up, but system routes still point at the regular network.")
    }

    return FairleadL10n.text("The tunnel started without a network adapter.")
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
private func waitControllerGone(maxSeconds: TimeInterval = 8) {
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
    stopPingLoop()
    let pid = mihomoPID
    if mihomoRunning() {
        // Drop live flows before tearing the TUN down so Chrome can reopen on the real path.
        closeConnections()
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
        return MihomoUp(error: FairleadL10n.text("This copy of Fairlead is missing the tunnel engine."))
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
    mihomoTunnelIPv4 = tunnelIPv4(from: wg)
    deviceLabel = mihomoProtocolLabel(wg)
    if let error = spawn(binary: binary) {
        return MihomoUp(error: error)
    }

    if let error = waitReady() {
        stopProcess()
        return MihomoUp(error: error)
    }

    recoveries = 0
    probeFails = 0
    recoverUntil = nil
    startPingLoop()
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
    mihomoTunnelIPv4 = ""
    deviceLabel = mihomoInterface
    probeFails = 0
    recoveries = 0
    recoverUntil = nil
}

/*
===============
mihomoReload
Rewrite config.yaml with the new rules and restart mihomo.
Soft PUT /configs recreates proxies/DNS inside a running process and can leave browsers
on en0 while the internal delay probe still succeeds. A clean restart keeps TUN, routes,
and WireGuard in one consistent state.
===============
*/
func mihomoReload(wg: String, rules: MihomoRules) -> String? {
    guard mihomoPID > 0 else { return FairleadL10n.text("The tunnel is not running.") }
    if let error = writeYAML(wg: wg, rules: rules, secret: mihomoSecret) { return error }

    mihomoTunnelIPv4 = tunnelIPv4(from: wg)
    mihomoEndpoint = peerEndpoint(wg)
    mihomoEndpointIP = resolveEndpointIP(mihomoEndpoint)
    deviceLabel = mihomoProtocolLabel(wg)

    fputs("fairlead-helper: applying rules with mihomo restart\n", stderr)
    // Drop flows before tear-down so apps reopen on the new path after routes return.
    closeConnections()
    stopProcess()
    guard let binary = ToolLocator.locate().mihomo, spawn(binary: binary) == nil, waitReady() == nil else {
        stopProcess()
        return FairleadL10n.text("The tunnel could not be restarted.")
    }

    recoveries = 0
    probeFails = 0
    lastProbeAt = nil
    lastOkAt = nil
    lastDelayMs = 0
    recoverUntil = Date().addingTimeInterval(45)
    startPingLoop()
    return nil
}

/*
===============
closeConnections
Drop live flows inside mihomo so new rules apply.
Do not post com.apple.system.config.network_change — that makes Chrome rebind onto en0
while routes are settling and is how 2ip starts showing the real IP after rule toggles.
===============
*/
private func closeConnections() {
    _ = mihomoAPI("DELETE", "/connections", secret: mihomoSecret, timeout: 3)
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
HTTP probe through the WireGuard outbound for the latency readout. 0 when it fails.
Path readiness is checked separately — gstatic can time out on a working tunnel.
===============
*/
private func probeDelay() -> Int {
    guard let reply = mihomoAPI("GET", probePath, secret: mihomoSecret, timeout: 5), reply.status == 200 else {
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
ICMP echo to the pre-resolved endpoint IP (route-excluded, so it stays on en0).
Uses posix_spawn via run() — Foundation.Process is flaky from the launchd helper.
===============
*/
private func pingEndpoint() -> Int {
    guard !mihomoEndpointIP.isEmpty else { return 0 }
    let result = run(["/sbin/ping", "-c", "1", "-W", "800", mihomoEndpointIP])
    guard result.code == 0 else { return 0 }
    guard let range = result.out.range(of: "time=") else { return 0 }
    var num = ""
    for ch in result.out[range.upperBound...] {
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
stopPingLoop
===============
*/
private func stopPingLoop() {
    pingTimer?.cancel()
    pingTimer = nil
    lastPingMs = 0
    lastPingAt = nil
}

/*
===============
startPingLoop
One ICMP every few seconds on a side queue. Never blocks status / connect.
A failed sample keeps the last good RTT so the UI does not flicker to blank.
===============
*/
private func startPingLoop() {
    stopPingLoop()
    guard !mihomoEndpointIP.isEmpty else { return }

    let timer = DispatchSource.makeTimerSource(queue: pingQueue)
    timer.schedule(deadline: .now() + 0.3, repeating: pingEvery, leeway: .milliseconds(200))
    timer.setEventHandler {
        guard mihomoPID > 0, !mihomoEndpointIP.isEmpty else { return }
        let sample = pingEndpoint()
        if sample > 0 {
            lastPingMs = sample
            lastPingAt = Date()
        }
    }
    timer.resume()
    pingTimer = timer
}

/*
===============
mihomoTraffic
One /traffic sample. up/down are bytes/s for the last second.
===============
*/
private func mihomoTraffic() -> (rx: UInt64, tx: UInt64, down: UInt64, up: UInt64)? {
    guard let url = URL(string: MihomoConfig.controllerURL + "/traffic") else { return nil }
    var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 2.5)
    request.httpMethod = "GET"
    request.setValue("Bearer \(mihomoSecret)", forHTTPHeaderField: "Authorization")

    final class Box: NSObject, URLSessionDataDelegate, @unchecked Sendable {
        var status = 0
        var data = Data()
        let done = DispatchSemaphore(value: 0)
        private var finished = false

        func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
                        completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
            status = (response as? HTTPURLResponse)?.statusCode ?? 0
            completionHandler(status == 200 ? .allow : .cancel)
        }

        func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
            self.data.append(data)
            let text = String(decoding: self.data, as: UTF8.self)
            if self.data.contains(UInt8(ascii: "\n")) || (text.contains("\"down\":") && text.contains("\"up\":")) {
                finish()
            }
        }

        func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
            finish()
        }

        private func finish() {
            guard !finished else { return }
            finished = true
            done.signal()
        }
    }

    let box = Box()
    let session = URLSession(configuration: apiSession.configuration, delegate: box, delegateQueue: nil)
    let task = session.dataTask(with: request)
    task.resume()
    _ = box.done.wait(timeout: .now() + 3)
    task.cancel()
    session.invalidateAndCancel()

    guard box.status == 200, !box.data.isEmpty else { return nil }
    var body = String(decoding: box.data, as: UTF8.self)
    if let nl = body.firstIndex(of: "\n") {
        body = String(body[..<nl])
    }
    return (
        rx: total(body, "\"downTotal\":"),
        tx: total(body, "\"upTotal\":"),
        down: total(body, "\"down\":"),
        up: total(body, "\"up\":")
    )
}

/*
===============
mihomoStatus
Counters, HTTP probe for tunnel liveness, ICMP ping for the UI (only after the tunnel is up).
Pass probe: false right after up() so Connected still waits on later status ticks.
===============
*/
func mihomoStatus(probe: Bool = true) -> WGDeviceStatus? {
    let now = Date()
    if mihomoPID <= 0 || !mihomoRunning() {
        lastProbeAt = nil
        lastOkAt = nil
        lastDelayMs = 0
        lastPingMs = 0
        probeFails = 0

        /* Session still expected (config on disk) — try a few respawns. */
        if FileManager.default.fileExists(atPath: configPath), recoveries < probeMaxRecover,
           let binary = ToolLocator.locate().mihomo {
            fputs("fairlead-helper: mihomo exited, restarting\n", stderr)
            recoveries += 1
            recoverUntil = now.addingTimeInterval(45)
            stopProcess()
            if spawn(binary: binary) == nil {
                startPingLoop()
                let peer = WGPeerStatus(
                    publicKey: "", endpoint: mihomoEndpoint, allowedIPs: "",
                    handshake: 0, rx: 0, tx: 0, keepalive: 0, delay: 0, ping: 0,
                    stage: FairleadL10n.text("Opening the tunnel…")
                )
                return WGDeviceStatus(interface: deviceLabel, publicKey: "", listenPort: 0, peers: [peer])
            }
        }

        return nil
    }

    var rx: UInt64 = 0
    var tx: UInt64 = 0
    var downRate: UInt64? = nil
    var upRate: UInt64? = nil

    var stage = ""
    if let until = recoverUntil, now < until, !tunnelPathReady() {
        let peer = WGPeerStatus(
            publicKey: "", endpoint: mihomoEndpoint, allowedIPs: "",
            handshake: 0, rx: 0, tx: 0, keepalive: 0, delay: 0, ping: 0,
            stage: FairleadL10n.text("Opening the tunnel…")
        )
        return WGDeviceStatus(interface: deviceLabel, publicKey: "", listenPort: 0, peers: [peer])
    }

    if recoverUntil != nil, (recoverUntil.map { now >= $0 } ?? true) || tunnelPathReady() {
        recoverUntil = nil
    }

    /*
     * Connected = system path enters our TUN (what Chrome/curl use).
     * HTTP delay only fills delayMs — never gates Connected or restarts.
     */
    let pathReady = tunnelPathReady()
    if pathReady {
        lastOkAt = now
        probeFails = 0
        recoverUntil = nil
        stage = ""
    } else {
        lastOkAt = nil
        lastDelayMs = 0
        stage = FairleadL10n.text(lastProbeAt == nil ? "Opening the tunnel…" : "Checking the tunnel…")
    }

    if probe {
        let gap = pathReady ? probeEvery : probeRetry
        let needProbe = lastProbeAt == nil || now.timeIntervalSince(lastProbeAt!) >= gap
        if needProbe {
            lastProbeAt = now
            if !pathReady {
                probeFails += 1
                if probeFails >= probeFailRestart, recoveries < probeMaxRecover {
                    fputs("fairlead-helper: tunnel path lost, restarting mihomo\n", stderr)
                    recoveries += 1
                    probeFails = 0
                    lastProbeAt = nil
                    recoverUntil = now.addingTimeInterval(45)
                    let binary = ToolLocator.locate().mihomo
                    stopProcess()
                    if let binary, spawn(binary: binary) == nil {
                        startPingLoop()
                        stage = FairleadL10n.text("Opening the tunnel…")
                    } else {
                        stage = FairleadL10n.text("The tunnel could not be reached.")
                    }
                } else if recoveries >= probeMaxRecover, probeFails >= probeFailRestart {
                    stage = FairleadL10n.text("The tunnel could not be reached.")
                }
            }
        }
    }

    if mihomoRunning(), !mihomoEndpointIP.isEmpty, pingTimer == nil {
        startPingLoop()
    }

    var handshake = 0
    var ping = 0
    if pathReady, let ok = lastOkAt, now.timeIntervalSince(ok) <= probeStale {
        handshake = Int(ok.timeIntervalSince1970)
        stage = ""
        /* ICMP RTT from the background loop — last good sample, never blocks this RPC. */
        ping = lastPingMs
        if let sample = mihomoTraffic() {
            rx = sample.rx
            tx = sample.tx
            downRate = sample.down
            upRate = sample.up
        }
    }

    let peer = WGPeerStatus(
        publicKey: "",
        endpoint: mihomoEndpoint,
        allowedIPs: "",
        handshake: handshake,
        rx: rx,
        tx: tx,
        keepalive: 0,
        delay: 0,
        ping: ping,
        stage: stage,
        downRate: downRate,
        upRate: upRate
    )
    return WGDeviceStatus(interface: deviceLabel, publicKey: "", listenPort: 0, peers: [peer])
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
    if FileManager.default.fileExists(atPath: configPath),
       let yaml = try? String(contentsOfFile: configPath, encoding: .utf8) {
        /* config.yaml is mihomo YAML; tunnel IPv4 is under "ip:" on the wireguard proxy. */
        if let range = yaml.range(of: "\n    ip: ") {
            let rest = yaml[range.upperBound...]
            let line = rest.prefix(while: { $0 != "\n" }).trimmingCharacters(in: .whitespacesAndNewlines)
            mihomoTunnelIPv4 = line
        }
        deviceLabel = yaml.contains("amnezia-wg-option") ? "AmneziaWG" : "WireGuard"
        mihomoEndpointIP = resolveEndpointIP(endpoint)
    }
    startPingLoop()
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
