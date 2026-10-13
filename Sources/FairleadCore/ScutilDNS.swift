import Foundation

/// Reads the default resolver from `scutil --dns` text.
public enum ScutilDNS {
    public struct Snapshot: Equatable, Sendable {
        public var servers: [String]
        public var interface: String?

        public init(servers: [String], interface: String?) {
            self.servers = servers
            self.interface = interface
        }
    }

    public static func parse(_ text: String) -> Snapshot {
        let blocks = text.components(separatedBy: "resolver #")
        for block in blocks.dropFirst() {
            if block.contains("mdns") { continue }
            let lines = block.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
            let scoped = lines.contains { line in
                let trimmed = line.trimmingCharacters(in: .whitespaces)
                return trimmed.hasPrefix("domain") && trimmed.contains(":")
            }
            if scoped { continue }
            var servers: [String] = []
            var interface: String?
            for line in lines {
                let trimmed = line.trimmingCharacters(in: .whitespaces)
                if trimmed.hasPrefix("nameserver[") {
                    let parts = trimmed.split(separator: ":", maxSplits: 1).map {
                        $0.trimmingCharacters(in: .whitespaces)
                    }
                    if parts.count == 2, let ip = IPAddress.parse(parts[1]) {
                        servers.append(ip.string)
                    }
                }
                if trimmed.hasPrefix("if_index"),
                   let open = trimmed.lastIndex(of: "("),
                   let close = trimmed.lastIndex(of: ")"),
                   open < close {
                    let name = String(trimmed[trimmed.index(after: open)..<close])
                    if !name.hasPrefix("utun") { interface = name }
                }
            }
            if !servers.isEmpty {
                return Snapshot(servers: servers, interface: interface)
            }
        }
        return Snapshot(servers: [], interface: nil)
    }
}
