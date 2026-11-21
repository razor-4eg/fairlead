//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// SystemDNS.swift -- Push public DNS onto hardware services while the tunnel is up
// (Amnezia-style). dns-hijack on utun never sees queries to the LAN gateway because
// the connected route on en0 wins, so ISP resolvers keep answering with poison.
//
//===============================================================

import Foundation
import FairleadCore

private struct DNSServiceBackup: Codable {
    var service: String
    var servers: [String]
    var search: [String]
}

private struct DNSBackupFile: Codable {
    var services: [DNSServiceBackup]
}

/*
===============
SystemDNS
===============
*/
enum SystemDNS {
    private static var backupPath: String { FairleadPaths.dnsBackup }

    private static func ensureBackupDir() {
        let dir = (backupPath as NSString).deletingLastPathComponent
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        chmod(dir, 0o755)
    }

/*
===============
servers
WireGuard Interface DNS when set; otherwise the same public resolvers Amnezia pushes.
===============
*/
    static func servers(fromWG wg: String) -> [String] {
        let raw = WGDocument.parse(wg).interfaceDNS
        let parts = raw.split(separator: ",").map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }.filter { !$0.isEmpty }
        if !parts.isEmpty { return parts }
        return ["1.1.1.1", "8.8.8.8"]
    }

/*
===============
listServices
networksetup service names. Lines that start with * are disabled — skip them.
===============
*/
    private static func listServices() -> [String] {
        let text = run(["/usr/sbin/networksetup", "-listallnetworkservices"]).out
        return text.split(separator: "\n").dropFirst().compactMap { line in
            let name = String(line)
            if name.isEmpty || name.hasPrefix("*") { return nil }
            return name
        }
    }

/*
===============
readServers
===============
*/
    private static func readServers(_ service: String) -> [String] {
        let text = run(["/usr/sbin/networksetup", "-getdnsservers", service]).out
            .trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty || text.localizedCaseInsensitiveContains("aren't any") || text.localizedCaseInsensitiveContains("are not") {
            return []
        }
        return text.split(separator: "\n").map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }.filter { !$0.isEmpty }
    }

/*
===============
readSearch
===============
*/
    private static func readSearch(_ service: String) -> [String] {
        let text = run(["/usr/sbin/networksetup", "-getsearchdomains", service]).out
            .trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty || text.localizedCaseInsensitiveContains("aren't any") || text.localizedCaseInsensitiveContains("are not") {
            return []
        }
        return text.split(separator: "\n").map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }.filter { !$0.isEmpty }
    }

/*
===============
apply
networksetup wants the token Empty when clearing servers or search domains.
===============
*/
    private static func apply(service: String, servers: [String], search: [String]) {
        let dns = servers.isEmpty ? ["Empty"] : servers
        let domains = search.isEmpty ? ["Empty"] : search
        _ = run(["/usr/sbin/networksetup", "-setdnsservers", service] + dns)
        _ = run(["/usr/sbin/networksetup", "-setsearchdomains", service] + domains)
    }

/*
===============
flushCache
===============
*/
    private static func flushCache() {
        _ = run(["/usr/bin/dscacheutil", "-flushcache"])
        _ = run(["/usr/bin/killall", "-HUP", "mDNSResponder"])
    }

/*
===============
push
Save the current per-service DNS once, then point every hardware service at tunnel DNS.
Reload keeps the backup and only refreshes the servers if the WG DNS list changed.
===============
*/
    static func push(fromWG wg: String) {
        let wanted = servers(fromWG: wg)
        guard !wanted.isEmpty else { return }

        var backup = loadBackup()
        if backup == nil {
            var services: [DNSServiceBackup] = []
            for name in listServices() {
                // Skip services that networksetup cannot configure (virtual / missing hardware).
                let probe = run(["/usr/sbin/networksetup", "-getdnsservers", name])
                if probe.code != 0 { continue }
                services.append(DNSServiceBackup(service: name, servers: readServers(name), search: readSearch(name)))
            }
            backup = DNSBackupFile(services: services)
            saveBackup(backup!)
            fputs("fairlead-helper: saved DNS for \(services.count) network service(s)\n", stderr)
        }

        guard let services = backup?.services, !services.isEmpty else { return }
        for item in services {
            apply(service: item.service, servers: wanted, search: item.search)
        }
        flushCache()
        fputs("fairlead-helper: system DNS → \(wanted.joined(separator: ", "))\n", stderr)
    }

/*
===============
restore
Put the saved servers back. Safe to call when no backup exists (crash / first boot).
===============
*/
    static func restore() {
        guard let backup = loadBackup() else { return }
        for item in backup.services {
            apply(service: item.service, servers: item.servers, search: item.search)
        }
        clearBackup()
        flushCache()
        fputs("fairlead-helper: restored system DNS for \(backup.services.count) network service(s)\n", stderr)
    }

/*
===============
loadBackup
===============
*/
    private static func loadBackup() -> DNSBackupFile? {
        guard let data = FileManager.default.contents(atPath: backupPath) else { return nil }
        return try? JSONDecoder().decode(DNSBackupFile.self, from: data)
    }

/*
===============
saveBackup
===============
*/
    private static func saveBackup(_ backup: DNSBackupFile) {
        ensureBackupDir()
        guard let data = try? JSONEncoder().encode(backup) else { return }
        try? data.write(to: URL(fileURLWithPath: backupPath), options: .atomic)
        chmod(backupPath, 0o600)
    }

/*
===============
clearBackup
===============
*/
    private static func clearBackup() {
        try? FileManager.default.removeItem(atPath: backupPath)
    }
}
