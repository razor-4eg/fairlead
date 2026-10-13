import Foundation

public struct WGDocument: Equatable {
    public struct Entry: Equatable {
        public var raw: String?
        public var key: String?
        public var value: String?

        public static func line(_ raw: String) -> Entry {
            Entry(raw: raw, key: nil, value: nil)
        }

        public static func field(_ key: String, _ value: String) -> Entry {
            Entry(raw: nil, key: key, value: value)
        }
    }

    public struct Section: Equatable, Identifiable {
        public var id: Int { index }
        public var index: Int
        public var name: String
        public var entries: [Entry]
    }

    public var preamble: [String]
    public var sections: [Section]

    public init(preamble: [String] = [], sections: [Section] = []) {
        self.preamble = preamble
        self.sections = sections
    }

    public static func parse(_ text: String) -> WGDocument {
        var preamble: [String] = []
        var sections: [Section] = []
        var sawSection = false
        let normalized = text.replacingOccurrences(of: "\r\n", with: "\n").replacingOccurrences(of: "\r", with: "\n")

        for line in normalized.split(separator: "\n", omittingEmptySubsequences: false).map(String.init) {
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            if trimmed.hasPrefix("[") && trimmed.hasSuffix("]") && trimmed.count > 2 {
                sawSection = true
                let name = String(trimmed.dropFirst().dropLast()).trimmingCharacters(in: .whitespaces)
                sections.append(Section(index: sections.count, name: name, entries: []))
                continue
            }
            if !sawSection {
                preamble.append(line)
                continue
            }
            guard var section = sections.last else { continue }
            if trimmed.isEmpty || trimmed.hasPrefix("#") || trimmed.hasPrefix(";") {
                section.entries.append(.line(line))
            } else if let eq = line.firstIndex(of: "=") {
                let key = line[..<eq].trimmingCharacters(in: .whitespaces)
                let value = line[line.index(after: eq)...].trimmingCharacters(in: .whitespaces)
                section.entries.append(.field(key, value))
            } else {
                section.entries.append(.line(line))
            }
            sections[sections.count - 1] = section
        }
        return WGDocument(preamble: preamble, sections: sections)
    }

    public func render() -> String {
        var lines: [String] = preamble
        if !preamble.isEmpty && (preamble.last?.isEmpty == false) && !sections.isEmpty {
            lines.append("")
        }
        for (offset, section) in sections.enumerated() {
            if offset > 0, lines.last?.isEmpty != true { lines.append("") }
            lines.append("[\(section.name)]")
            for entry in section.entries {
                if let key = entry.key {
                    lines.append("\(key) = \(entry.value ?? "")")
                } else {
                    lines.append(entry.raw ?? "")
                }
            }
        }
        var text = lines.joined(separator: "\n")
        if !text.hasSuffix("\n") { text.append("\n") }
        return text
    }

    public var interfaceIndex: Int? {
        sections.firstIndex { $0.name.caseInsensitiveCompare("Interface") == .orderedSame }
    }

    public var peerIndexes: [Int] {
        sections.indices.filter { sections[$0].name.caseInsensitiveCompare("Peer") == .orderedSame }
    }

    public func values(section: Int, key: String) -> [String] {
        guard sections.indices.contains(section) else { return [] }
        return sections[section].entries.compactMap { entry in
            guard entry.key?.caseInsensitiveCompare(key) == .orderedSame else { return nil }
            let value = entry.value?.trimmingCharacters(in: .whitespaces) ?? ""
            return value.isEmpty ? nil : value
        }
    }

    public func value(section: Int, key: String) -> String {
        values(section: section, key: key).joined(separator: ", ")
    }

    public mutating func setValues(section: Int, key: String, values: [String]) {
        guard sections.indices.contains(section) else { return }
        let cleaned = values.map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        var entries = sections[section].entries
        if let first = entries.firstIndex(where: { $0.key?.caseInsensitiveCompare(key) == .orderedSame }) {
            entries.removeAll { $0.key?.caseInsensitiveCompare(key) == .orderedSame }
            if !cleaned.isEmpty {
                entries.insert(.field(canonical(key), cleaned.joined(separator: ", ")), at: min(first, entries.count))
            }
        } else if !cleaned.isEmpty {
            entries.append(.field(canonical(key), cleaned.joined(separator: ", ")))
        }
        sections[section].entries = entries
    }

    public mutating func setValue(section: Int, key: String, value: String) {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        setValues(section: section, key: key, values: trimmed.isEmpty ? [] : [trimmed])
    }

    public mutating func ensureInterface() -> Int {
        if let interfaceIndex { return interfaceIndex }
        sections.insert(Section(index: 0, name: "Interface", entries: []), at: 0)
        reindex()
        return 0
    }

    public mutating func addPeer() -> Int {
        sections.append(Section(index: sections.count, name: "Peer", entries: []))
        reindex()
        return sections.count - 1
    }

    public mutating func removeSection(at index: Int) {
        guard sections.indices.contains(index) else { return }
        sections.remove(at: index)
        reindex()
    }

    public var interfacePrivateKey: String {
        guard let interfaceIndex else { return "" }
        return value(section: interfaceIndex, key: "PrivateKey")
    }

    public var interfaceAddresses: String {
        guard let interfaceIndex else { return "" }
        return splitList(values(section: interfaceIndex, key: "Address")).joined(separator: ", ")
    }

    public var interfaceDNS: String {
        guard let interfaceIndex else { return "" }
        return splitList(values(section: interfaceIndex, key: "DNS")).joined(separator: ", ")
    }

    public var interfaceMTU: String {
        guard let interfaceIndex else { return "" }
        return value(section: interfaceIndex, key: "MTU")
    }

    public var interfaceListenPort: String {
        guard let interfaceIndex else { return "" }
        return value(section: interfaceIndex, key: "ListenPort")
    }

    public func peerPublicKey(_ index: Int) -> String { value(section: index, key: "PublicKey") }
    public func peerPreshared(_ index: Int) -> String { value(section: index, key: "PresharedKey") }
    public func peerEndpoint(_ index: Int) -> String { value(section: index, key: "Endpoint") }
    public func peerAllowedIPs(_ index: Int) -> String {
        splitList(values(section: index, key: "AllowedIPs")).joined(separator: ", ")
    }
    public func peerKeepalive(_ index: Int) -> String { value(section: index, key: "PersistentKeepalive") }

    public func endpointHosts() -> [String] {
        peerIndexes.compactMap { EndpointParse.host(from: peerEndpoint($0)) }
    }

    public static func blank(privateKey: String) -> WGDocument {
        var doc = WGDocument()
        let iface = doc.ensureInterface()
        doc.setValue(section: iface, key: "PrivateKey", value: privateKey)
        doc.setValue(section: iface, key: "Address", value: "10.8.0.2/32")
        doc.setValue(section: iface, key: "DNS", value: "1.1.1.1")
        let peer = doc.addPeer()
        doc.setValue(section: peer, key: "PublicKey", value: "")
        doc.setValue(section: peer, key: "AllowedIPs", value: "0.0.0.0/0, ::/0")
        doc.setValue(section: peer, key: "Endpoint", value: "")
        doc.setValue(section: peer, key: "PersistentKeepalive", value: "25")
        return doc
    }

    private mutating func reindex() {
        for index in sections.indices {
            sections[index].index = index
        }
    }

    private func canonical(_ key: String) -> String {
        switch key.lowercased() {
        case "privatekey": return "PrivateKey"
        case "address": return "Address"
        case "dns": return "DNS"
        case "mtu": return "MTU"
        case "listenport": return "ListenPort"
        case "publickey": return "PublicKey"
        case "presharedkey": return "PresharedKey"
        case "endpoint": return "Endpoint"
        case "allowedips": return "AllowedIPs"
        case "persistentkeepalive": return "PersistentKeepalive"
        case "table": return "Table"
        default: return key
        }
    }

    private func splitList(_ values: [String]) -> [String] {
        values.flatMap { value in
            value.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }
        }.filter { !$0.isEmpty }
    }
}

public struct ConfigIssue: Equatable, Identifiable {
    public var id: String { message }
    public var message: String

    public init(_ message: String) {
        self.message = message
    }
}

public enum ConfigCheck {
    public static func issues(in document: WGDocument) -> [ConfigIssue] {
        var issues: [ConfigIssue] = []
        guard let iface = document.interfaceIndex else {
            return [ConfigIssue(FairleadL10n.text("The config has no Interface section."))]
        }
        let privateKey = document.value(section: iface, key: "PrivateKey")
        if !WGKeys.isKey(privateKey) {
            issues.append(ConfigIssue(FairleadL10n.text("The private key must be a WireGuard key.")))
        }
        let addresses = document.interfaceAddresses.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }
        if addresses.isEmpty || addresses.contains(where: { CIDR.parse(String($0)) == nil }) {
            issues.append(ConfigIssue(FairleadL10n.text("Set an interface address, for example 10.8.0.2/32.")))
        }
        if let mtu = Int(document.interfaceMTU), !(1280...1500).contains(mtu), !document.interfaceMTU.isEmpty {
            issues.append(ConfigIssue(FairleadL10n.text("MTU is usually between 1280 and 1500.")))
        } else if !document.interfaceMTU.isEmpty && Int(document.interfaceMTU) == nil {
            issues.append(ConfigIssue(FairleadL10n.text("MTU must be a number.")))
        }
        let peers = document.peerIndexes
        if peers.isEmpty {
            issues.append(ConfigIssue(FairleadL10n.text("Add at least one Peer.")))
        }
        for peer in peers {
            if !WGKeys.isKey(document.peerPublicKey(peer)) {
                issues.append(ConfigIssue(FairleadL10n.text("A peer has an invalid public key.")))
            }
            let psk = document.peerPreshared(peer)
            if !psk.isEmpty && !WGKeys.isKey(psk) {
                issues.append(ConfigIssue(FairleadL10n.text("PresharedKey is set, but it is not a WireGuard key.")))
            }
            if !validEndpoint(document.peerEndpoint(peer)) {
                issues.append(ConfigIssue(FairleadL10n.text("Endpoint must look like host:port.")))
            }
            let allowed = document.peerAllowedIPs(peer).split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }
            if allowed.isEmpty || allowed.contains(where: { CIDR.parse(String($0)) == nil }) {
                issues.append(ConfigIssue(FairleadL10n.text("AllowedIPs must list networks, for example 0.0.0.0/0.")))
            }
            let keep = document.peerKeepalive(peer)
            if !keep.isEmpty, let value = Int(keep), !(0...65535).contains(value) {
                issues.append(ConfigIssue(FairleadL10n.text("PersistentKeepalive is out of range.")))
            } else if !keep.isEmpty && Int(keep) == nil {
                issues.append(ConfigIssue(FairleadL10n.text("PersistentKeepalive must be a number.")))
            }
        }
        return issues
    }

    public static func validEndpoint(_ endpoint: String) -> Bool {
        let value = endpoint.trimmingCharacters(in: .whitespaces)
        guard let host = EndpointParse.host(from: value), !host.isEmpty else { return false }
        let portString: String
        if value.hasPrefix("[") {
            guard let bracket = value.lastIndex(of: "]"), value.index(after: bracket) < value.endIndex,
                  value[value.index(after: bracket)] == ":" else { return false }
            portString = String(value[value.index(bracket, offsetBy: 2)...])
        } else {
            guard let colon = value.lastIndex(of: ":") else { return false }
            portString = String(value[value.index(after: colon)...])
        }
        guard let port = Int(portString), (1...65535).contains(port) else { return false }
        return true
    }
}
