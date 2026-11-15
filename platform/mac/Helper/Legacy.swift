//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// Legacy.swift -- One-time cleanup of a tunnel started by an older Fairlead (amneziawg-go, utun, route, networksetup).
//
//===============================================================

import Darwin
import Foundation
import FairleadCore

private struct LegacyDNS: Codable {
    var service: String
    var servers: [String]
    var search: [String]
}

private struct LegacyMeta: Codable {
    var routes: [String]
    var dns: [LegacyDNS]
}

/*
===============
isLegacyDaemon
===============
*/
private func isLegacyDaemon(_ command: String) -> Bool {
    if command.contains("fairlead-helper") || command.contains("harbor-helper") { return false }
    let names = [
        "fairlead-amneziawg-go", "fairlead-wireguard-go", "harbor-wireguard-go", "fairlead-wg-go", "harbor-wg-go",
    ]
    return names.contains { command.contains("/usr/local/libexec/" + $0) }
        || command.contains("WG_TUN_NAME_FILE=/var/run/wireguard/fld")
        || command.contains("WG_TUN_NAME_FILE=/var/run/wireguard/hbr")
}

/*
===============
deleteLegacyRoute
===============
*/
private func deleteLegacyRoute(_ stored: String) {
    let parts = stored.split(separator: " ", maxSplits: 1).map(String.init)
    guard parts.count == 2, parts[0] == "inet" || parts[0] == "inet6", CIDR.parse(parts[1]) != nil || IPAddress.parse(parts[1]) != nil else { return }
    _ = run(["/sbin/route", "-q", "-n", "delete", "-\(parts[0])", parts[1]])
}

/*
===============
cleanupLegacyTunnels
Ends an old userspace daemon, puts the DNS it changed back, and drops the routes it added. A no-op on a clean machine.
===============
*/
func cleanupLegacyTunnels() {
    let selfPid = getpid()
    let daemons = processList().filter { $0.pid != selfPid && isLegacyDaemon($0.command) }
    for proc in daemons { kill(proc.pid, SIGTERM) }
    if !daemons.isEmpty {
        usleep(300_000)
        for proc in daemons where kill(proc.pid, 0) == 0 { kill(proc.pid, SIGKILL) }
        fputs("fairlead-helper: ended \(daemons.count) old tunnel process(es)\n", stderr)
    }

    unlink("/var/run/fairlead/wireguard-go.pid")

    // Routes the old helper added by hand. A new-format state entry has a pid and is left alone.
    if let data = FileManager.default.contents(atPath: FairleadPaths.state),
       let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
       let tunnels = root["tunnels"] as? [String: [String: Any]] {
        for (_, tunnel) in tunnels where tunnel["pid"] == nil {
            for cidr in tunnel["ownedRoutes"] as? [String] ?? [] {
                guard let parsed = CIDR.parse(cidr) else { continue }
                deleteLegacyRoute("\(parsed.family == .v4 ? "inet" : "inet6") \(parsed.string)")
            }
        }
    }

    guard let files = try? FileManager.default.contentsOfDirectory(atPath: FairleadPaths.directory) else { return }
    for file in files where file.hasSuffix(".meta.json") {
        let path = "\(FairleadPaths.directory)/\(file)"
        if let data = FileManager.default.contents(atPath: path), let meta = try? JSONDecoder().decode(LegacyMeta.self, from: data) {
            for item in meta.dns {
                let servers = item.servers.isEmpty ? ["Empty"] : item.servers
                let search = item.search.isEmpty ? ["Empty"] : item.search
                _ = run(["/usr/sbin/networksetup", "-setdnsservers", item.service] + servers)
                _ = run(["/usr/sbin/networksetup", "-setsearchdomains", item.service] + search)
            }

            for route in meta.routes { deleteLegacyRoute(route) }
        }

        try? FileManager.default.removeItem(atPath: path)
    }
}
