import Darwin
import Foundation
import HarborCore

struct DNSBackup: Codable, Equatable {
    var service: String
    var servers: [String]
    var search: [String]
}

struct EndpointHop: Codable, Equatable {
    var family: String
    var ip: String
    var gateway: String
}

struct TunnelMeta: Codable, Equatable {
    var routes: [String]
    var dns: [DNSBackup]
    var interfaceName: String
    var dnsServers: [String]
    var dnsSearch: [String]
    var endpoints: [EndpointHop]
}

struct UserspaceUp {
    var interface: String?
    var error: String?
}

private struct RouteDest {
    var family: String
    var destination: String
}

private final class MonitorState {
    let lock = NSLock()
    var stop = false
    var iface = ""
    var endpoints: [EndpointHop] = []
    var servers: [String] = []
    var search: [String] = []
}

private let monitorState = MonitorState()
private var monitorThread: Thread?
private var userspaceProcess: Process?
private var userspaceLog: FileHandle?

func userspaceStatus(_ iface: String) -> WGDeviceStatus? {
    guard !iface.isEmpty, let text = uapiExchange(iface: iface, message: "get=1\n\n") else { return nil }
    let device = WireGuardUAPI.parseGet(text, interface: iface)
    return device.publicKey.isEmpty && device.peers.isEmpty ? nil : device
}

func bringUserspaceUp(
    slug: String,
    document: WGDocument,
    gatewayV4: String?,
    gatewayV6: String?,
    gatewayIface: String?
) -> UserspaceUp {
    guard let binary = ToolLocator.locate().wireguardGo else {
        return UserspaceUp(interface: nil, error: HarborL10n.text("wireguard-go is not included in this copy of Harbor."))
    }
    guard let ifaceIndex = document.interfaceIndex else {
        return UserspaceUp(interface: nil, error: HarborL10n.text("The config has no Interface section."))
    }
    var endpoints: [Int: String] = [:]
    var hops: [EndpointHop] = []
    for peer in document.peerIndexes {
        let original = document.peerEndpoint(peer)
        guard let host = EndpointParse.host(from: original), !host.isEmpty else { continue }
        guard let ip = resolveHost(host), let socket = WireGuardUAPI.socketEndpoint(ip: ip, original: original) else {
            return UserspaceUp(interface: nil, error: HarborL10n.text("Could not resolve the endpoint."))
        }
        endpoints[peer] = socket
        let family = ip.contains(":") ? "inet6" : "inet"
        let gateway = family == "inet" ? gatewayV4 : gatewayV6
        if let gateway {
            hops.append(EndpointHop(family: family, ip: ip, gateway: gateway))
        }
    }
    guard let message = WireGuardUAPI.setMessage(document: document, endpoints: endpoints) else {
        return UserspaceUp(interface: nil, error: HarborL10n.text("The config is damaged."))
    }
    if let error = startUserspace(slug: slug, binary: binary) {
        return UserspaceUp(interface: nil, error: error)
    }
    guard let real = waitForUserspace(slug: slug) else {
        let detail = userspaceLogTail()
        abortUserspace(slug: slug, routes: [], dns: [])
        let base = HarborL10n.text("wireguard-go did not create an interface.")
        let error = detail.isEmpty ? base : base + "\n" + detail
        return UserspaceUp(interface: nil, error: error)
    }
    guard let response = uapiExchange(iface: real, message: message), response.contains("errno=0") else {
        abortUserspace(slug: slug, routes: [], dns: [])
        return UserspaceUp(interface: nil, error: HarborL10n.text("wireguard-go did not create an interface."))
    }
    let addresses = document.interfaceAddresses.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }
    for address in addresses where !address.isEmpty {
        let result: CommandResult
        if address.contains(":") {
            result = run(["/sbin/ifconfig", real, "inet6", address, "alias"])
        } else {
            let host = String(address.split(separator: "/").first ?? Substring(address))
            result = run(["/sbin/ifconfig", real, "inet", address, host, "alias"])
        }
        if result.code != 0 {
            abortUserspace(slug: slug, routes: [], dns: [])
            let detail = result.blob.trimmingCharacters(in: .whitespacesAndNewlines)
            return UserspaceUp(interface: nil, error: detail.isEmpty ? HarborL10n.text("wireguard-go did not create an interface.") : detail)
        }
    }
    let mtu = tunnelMTU(document: document, iface: gatewayIface)
    _ = run(["/sbin/ifconfig", real, "mtu", "\(mtu)"])
    let up = run(["/sbin/ifconfig", real, "up"])
    if up.code != 0 {
        abortUserspace(slug: slug, routes: [], dns: [])
        return UserspaceUp(interface: nil, error: up.blob.trimmingCharacters(in: .whitespacesAndNewlines))
    }

    var stored: [String] = []
    let table = document.value(section: ifaceIndex, key: "Table")
    if table.caseInsensitiveCompare("off") != .orderedSame {
        let destinations = tunnelDestinations(document)
        let coversInternet = destinations.contains { $0.destination == "0.0.0.0/1" || $0.destination == "::/1" }
        if coversInternet {
            let needsV4 = destinations.contains { $0.family == "inet" }
            let needsV6 = destinations.contains { $0.family == "inet6" }
            if needsV4 && hops.first(where: { $0.family == "inet" }) == nil && endpoints.values.contains(where: { !$0.contains("[") }) {
                abortUserspace(slug: slug, routes: [], dns: [])
                let sample = endpoints.values.first ?? ""
                return UserspaceUp(interface: nil, error: HarborL10n.format("No gateway for %@.", sample))
            }
            if needsV6 && hops.first(where: { $0.family == "inet6" }) == nil && endpoints.values.contains(where: { $0.hasPrefix("[") }) {
                abortUserspace(slug: slug, routes: [], dns: [])
                let sample = endpoints.values.first ?? ""
                return UserspaceUp(interface: nil, error: HarborL10n.format("No gateway for %@.", sample))
            }
        }
        for route in destinations {
            let result = run(["/sbin/route", "-q", "-n", "add", "-\(route.family)", route.destination, "-interface", real])
            if result.code == 0 || result.blob.localizedCaseInsensitiveContains("exists") {
                stored.append("\(route.family) \(route.destination)")
            } else {
                abortUserspace(slug: slug, routes: stored, dns: [])
                let detail = result.blob.trimmingCharacters(in: .whitespacesAndNewlines)
                return UserspaceUp(interface: nil, error: detail.isEmpty ? HarborL10n.format("Could not add route %@.", route.destination) : detail)
            }
        }
        for hop in hops {
            let result = run(["/sbin/route", "-q", "-n", "add", "-\(hop.family)", hop.ip, "-gateway", hop.gateway])
            if result.code == 0 || result.blob.localizedCaseInsensitiveContains("exists") {
                stored.append("\(hop.family) \(hop.ip)")
            }
        }
    }

    let wanted = splitDNS(document)
    let saved = applyDNS(servers: wanted.servers, search: wanted.search)
    let meta = TunnelMeta(
        routes: stored,
        dns: saved,
        interfaceName: real,
        dnsServers: wanted.servers,
        dnsSearch: wanted.search,
        endpoints: hops
    )
    saveTunnelMeta(slug: slug, meta)
    startMonitor(meta)
    fputs("harbor-helper: tunnel \(real) up\n", stderr)
    return UserspaceUp(interface: real, error: nil)
}

func bringUserspaceDown(slug: String, interfaceName: String?) {
    stopMonitor()
    stopManagedProcesses()
    let meta = loadTunnelMeta(slug: slug)
    if let meta {
        restoreDNS(meta.dns)
        for route in meta.routes { deleteStoredRoute(route) }
    }
    stopUserspaceProcess()
    let name = meta?.interfaceName ?? interfaceName
    if let name, name.allSatisfy({ $0.isLetter || $0.isNumber }) {
        unlink("\(HarborPaths.wireguardRun)/\(name).sock")
    }
    removeInterfaceFiles(slug: slug)
    removeTunnelMeta(slug: slug)
}

private func abortUserspace(slug: String, routes: [String], dns: [DNSBackup]) {
    stopMonitor()
    restoreDNS(dns)
    for route in routes { deleteStoredRoute(route) }
    stopUserspaceProcess()
    removeInterfaceFiles(slug: slug)
    removeTunnelMeta(slug: slug)
}

private func startUserspace(slug: String, binary: String) -> String? {
    stopUserspaceProcess()
    try? FileManager.default.createDirectory(atPath: HarborPaths.wireguardRun, withIntermediateDirectories: true)
    let nameFile = "\(HarborPaths.wireguardRun)/\(slug).name"
    unlink(nameFile)
    let logPath = "/var/log/harbor-wireguard-go.log"
    if !FileManager.default.fileExists(atPath: logPath) {
        FileManager.default.createFile(atPath: logPath, contents: nil)
    }
    guard let log = FileHandle(forWritingAtPath: logPath) else {
        return HarborL10n.text("wireguard-go did not create an interface.")
    }
    log.seekToEndOfFile()
    let process = Process()
    process.executableURL = URL(fileURLWithPath: binary)
    process.arguments = ["-f", "utun"]
    var environment = ProcessInfo.processInfo.environment
    environment["WG_TUN_NAME_FILE"] = nameFile
    environment["LOG_LEVEL"] = "error"
    environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
    process.environment = environment
    process.standardInput = FileHandle.nullDevice
    process.standardOutput = FileHandle.nullDevice
    process.standardError = log
    do {
        try process.run()
    } catch {
        try? log.close()
        return error.localizedDescription
    }
    userspaceProcess = process
    userspaceLog = log
    try? "\(process.processIdentifier)\n".write(toFile: "/var/run/harbor/wireguard-go.pid", atomically: true, encoding: .utf8)
    return nil
}

private func stopUserspaceProcess() {
    let tracked = userspaceProcess?.processIdentifier
    if let process = userspaceProcess, process.isRunning {
        process.terminate()
        process.waitUntilExit()
    }
    userspaceProcess = nil
    try? userspaceLog?.close()
    userspaceLog = nil
    guard let text = try? String(contentsOfFile: "/var/run/harbor/wireguard-go.pid", encoding: .utf8),
          let pid = Int32(text.trimmingCharacters(in: .whitespacesAndNewlines)),
          pid > 1, pid != tracked else { return }
    let listed = run(["/bin/ps", "-p", "\(pid)", "-o", "command="])
    if listed.out.contains("wireguard-go") {
        kill(pid, SIGTERM)
    }
}

private func waitForUserspace(slug: String) -> String? {
    let nameFile = "\(HarborPaths.wireguardRun)/\(slug).name"
    for _ in 0..<100 {
        if let process = userspaceProcess, !process.isRunning { return nil }
        if let text = try? String(contentsOfFile: nameFile, encoding: .utf8) {
            let name = text.trimmingCharacters(in: .whitespacesAndNewlines)
            let sock = "\(HarborPaths.wireguardRun)/\(name).sock"
            if !name.isEmpty,
               name.allSatisfy({ $0.isLetter || $0.isNumber }),
               FileManager.default.fileExists(atPath: sock),
               interfaceExists(name) {
                return name
            }
        }
        usleep(50_000)
    }
    return nil
}

private func userspaceLogTail() -> String {
    guard let data = FileManager.default.contents(atPath: "/var/log/harbor-wireguard-go.log"),
          let text = String(data: data, encoding: .utf8) else { return "" }
    return text.split(separator: "\n").suffix(8).joined(separator: "\n")
}

private func uapiExchange(iface: String, message: String) -> String? {
    let fd = socket(AF_UNIX, SOCK_STREAM, 0)
    guard fd >= 0 else { return nil }
    defer { close(fd) }
    var timeout = timeval(tv_sec: 3, tv_usec: 0)
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
    var addr = sockaddr_un()
    addr.sun_family = sa_family_t(AF_UNIX)
    let path = "\(HarborPaths.wireguardRun)/\(iface).sock"
    guard SocketFrame.setUnixPath(path, into: &addr) else { return nil }
    let length = socklen_t(MemoryLayout<sockaddr_un>.size)
    let connected = withUnsafePointer(to: &addr) { pointer in
        pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
            Darwin.connect(fd, sa, length)
        }
    }
    guard connected == 0 else { return nil }
    let bytes = Array(message.utf8)
    var sent = 0
    while sent < bytes.count {
        let wrote = bytes[sent...].withUnsafeBytes { raw -> Int in
            guard let base = raw.baseAddress else { return -1 }
            return Darwin.write(fd, base, bytes.count - sent)
        }
        if wrote < 0 {
            if errno == EINTR { continue }
            return nil
        }
        if wrote == 0 { return nil }
        sent += wrote
    }
    var response = Data()
    var buffer = [UInt8](repeating: 0, count: 8192)
    while response.count < 1_000_000 {
        let count = buffer.withUnsafeMutableBytes { raw -> Int in
            guard let base = raw.baseAddress else { return -1 }
            return Darwin.read(fd, base, raw.count)
        }
        if count == 0 { break }
        if count < 0 {
            if errno == EINTR { continue }
            break
        }
        response.append(buffer, count: count)
        if let text = String(data: response, encoding: .utf8), text.contains("errno=") { break }
    }
    return String(data: response, encoding: .utf8)
}

private func resolveHost(_ host: String) -> String? {
    if IPAddress.parse(host) != nil { return host }
    var hints = addrinfo()
    hints.ai_socktype = SOCK_DGRAM
    hints.ai_family = AF_UNSPEC
    var info: UnsafeMutablePointer<addrinfo>?
    guard getaddrinfo(host, nil, &hints, &info) == 0, let info else { return nil }
    defer { freeaddrinfo(info) }
    var v4: String?
    var v6: String?
    var node: UnsafeMutablePointer<addrinfo>? = info
    while let current = node {
        if current.pointee.ai_family == AF_INET, let addr = current.pointee.ai_addr {
            var sin = sockaddr_in()
            memcpy(&sin, addr, MemoryLayout<sockaddr_in>.size)
            var buffer = [CChar](repeating: 0, count: Int(INET_ADDRSTRLEN))
            var raw = sin.sin_addr
            if inet_ntop(AF_INET, &raw, &buffer, socklen_t(INET_ADDRSTRLEN)) != nil, v4 == nil {
                v4 = String(cString: buffer)
            }
        } else if current.pointee.ai_family == AF_INET6, let addr = current.pointee.ai_addr {
            var sin = sockaddr_in6()
            memcpy(&sin, addr, MemoryLayout<sockaddr_in6>.size)
            var buffer = [CChar](repeating: 0, count: Int(INET6_ADDRSTRLEN))
            var raw = sin.sin6_addr
            if inet_ntop(AF_INET6, &raw, &buffer, socklen_t(INET6_ADDRSTRLEN)) != nil, v6 == nil {
                v6 = String(cString: buffer)
            }
        }
        node = current.pointee.ai_next
    }
    return v4 ?? v6
}

private func tunnelMTU(document: WGDocument, iface: String?) -> Int {
    if let index = document.interfaceIndex, let value = Int(document.value(section: index, key: "MTU")), value >= 1280 {
        return value
    }
    guard let iface else { return 1420 }
    let info = run(["/sbin/ifconfig", iface]).out
    guard let match = info.range(of: #"mtu [0-9]+"#, options: .regularExpression) else { return 1420 }
    let digits = info[match].filter(\.isNumber)
    guard let mtu = Int(digits), mtu > 80 else { return 1420 }
    return max(1280, mtu - 80)
}

private func tunnelDestinations(_ document: WGDocument) -> [RouteDest] {
    var seen = Set<String>()
    var result: [RouteDest] = []
    func add(_ family: String, _ destination: String) {
        let key = family + " " + destination
        guard seen.insert(key).inserted else { return }
        result.append(RouteDest(family: family, destination: destination))
    }
    for peer in document.peerIndexes {
        for raw in document.peerAllowedIPs(peer).split(separator: ",") {
            guard let cidr = CIDR.parse(String(raw)) else { continue }
            if cidr.family == .v4 && cidr.prefix == 0 {
                add("inet", "0.0.0.0/1")
                add("inet", "128.0.0.0/1")
            } else if cidr.family == .v6 && cidr.prefix == 0 {
                add("inet6", "::/1")
                add("inet6", "8000::/1")
            } else {
                add(cidr.family == .v4 ? "inet" : "inet6", cidr.string)
            }
        }
    }
    return result
}

private func splitDNS(_ document: WGDocument) -> (servers: [String], search: [String]) {
    var servers: [String] = []
    var search: [String] = []
    for token in document.interfaceDNS.split(separator: ",").map({ $0.trimmingCharacters(in: .whitespaces) }) where !token.isEmpty {
        if IPAddress.parse(token) != nil {
            servers.append(token)
        } else {
            search.append(token)
        }
    }
    return (servers, search)
}

private func networkServices() -> [String] {
    let out = run(["/usr/sbin/networksetup", "-listallnetworkservices"]).out
    return out.split(separator: "\n").dropFirst().map(String.init).filter { !$0.isEmpty && !$0.hasPrefix("*") }
}

private func dnsLines(_ flag: String, _ service: String) -> [String] {
    let out = run(["/usr/sbin/networksetup", flag, service]).out
    let lines = out.split(separator: "\n").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
    if lines.isEmpty || lines.contains(where: { $0.localizedCaseInsensitiveContains("aren't any") }) {
        return ["Empty"]
    }
    return lines
}

private func applyDNS(servers: [String], search: [String]) -> [DNSBackup] {
    guard !servers.isEmpty else { return [] }
    var saved: [DNSBackup] = []
    let domains = search.isEmpty ? ["Empty"] : search
    for service in networkServices() {
        saved.append(DNSBackup(service: service, servers: dnsLines("-getdnsservers", service), search: dnsLines("-getsearchdomains", service)))
        _ = run(["/usr/sbin/networksetup", "-setdnsservers", service] + servers)
        _ = run(["/usr/sbin/networksetup", "-setsearchdomains", service] + domains)
    }
    return saved
}

private func restoreDNS(_ saved: [DNSBackup]) {
    for item in saved {
        let servers = item.servers.isEmpty ? ["Empty"] : item.servers
        let search = item.search.isEmpty ? ["Empty"] : item.search
        _ = run(["/usr/sbin/networksetup", "-setdnsservers", item.service] + servers)
        _ = run(["/usr/sbin/networksetup", "-setsearchdomains", item.service] + search)
    }
}

private func deleteStoredRoute(_ stored: String) {
    let parts = stored.split(separator: " ", maxSplits: 1).map(String.init)
    guard parts.count == 2 else { return }
    _ = run(["/sbin/route", "-q", "-n", "delete", "-\(parts[0])", parts[1]])
}

private func metaURL(_ slug: String) -> URL {
    URL(fileURLWithPath: HarborPaths.directory).appendingPathComponent("\(slug).meta.json")
}

private func loadTunnelMeta(slug: String) -> TunnelMeta? {
    guard let data = try? Data(contentsOf: metaURL(slug)) else { return nil }
    return try? JSONDecoder().decode(TunnelMeta.self, from: data)
}

private func saveTunnelMeta(slug: String, _ meta: TunnelMeta) {
    guard let data = try? JSONEncoder().encode(meta) else { return }
    try? data.write(to: metaURL(slug), options: .atomic)
    chmod(metaURL(slug).path, 0o600)
}

private func removeTunnelMeta(slug: String) {
    try? FileManager.default.removeItem(at: metaURL(slug))
}

private func startMonitor(_ meta: TunnelMeta) {
    stopMonitor()
    monitorState.lock.lock()
    monitorState.stop = false
    monitorState.iface = meta.interfaceName
    monitorState.endpoints = meta.endpoints
    monitorState.servers = meta.dnsServers
    monitorState.search = meta.dnsSearch
    monitorState.lock.unlock()
    let thread = Thread {
        while true {
            for _ in 0..<16 {
                monitorState.lock.lock()
                let stop = monitorState.stop
                monitorState.lock.unlock()
                if stop { return }
                Thread.sleep(forTimeInterval: 0.5)
            }
            monitorState.lock.lock()
            let stop = monitorState.stop
            let iface = monitorState.iface
            let endpoints = monitorState.endpoints
            let servers = monitorState.servers
            let search = monitorState.search
            monitorState.lock.unlock()
            if stop || iface.isEmpty { return }
            refreshTunnel(iface: iface, endpoints: endpoints, servers: servers, search: search)
        }
    }
    thread.stackSize = 512 * 1024
    monitorThread = thread
    thread.start()
}

private func stopMonitor() {
    monitorState.lock.lock()
    monitorState.stop = true
    let thread = monitorThread
    monitorState.lock.unlock()
    let deadline = Date().addingTimeInterval(5)
    while let thread, thread.isExecuting, Date() < deadline {
        Thread.sleep(forTimeInterval: 0.05)
    }
    monitorThread = nil
}

private func refreshTunnel(iface: String, endpoints: [EndpointHop], servers: [String], search: [String]) {
    guard interfaceExists(iface) else { return }
    for hop in endpoints {
        let looked = RouteText.parseRouteGet(run(["/sbin/route", "-n", "get", "-\(hop.family)", hop.ip]).out)
        if looked.interface == iface {
            _ = run(["/sbin/route", "-q", "-n", "add", "-\(hop.family)", hop.ip, "-gateway", hop.gateway])
        }
    }
    guard !servers.isEmpty else { return }
    let domains = search.isEmpty ? ["Empty"] : search
    for service in networkServices() {
        monitorState.lock.lock()
        let stop = monitorState.stop
        monitorState.lock.unlock()
        if stop { return }
        let current = dnsLines("-getdnsservers", service)
        if Set(current) == Set(servers) { continue }
        _ = run(["/usr/sbin/networksetup", "-setdnsservers", service] + servers)
        _ = run(["/usr/sbin/networksetup", "-setsearchdomains", service] + domains)
    }
}
