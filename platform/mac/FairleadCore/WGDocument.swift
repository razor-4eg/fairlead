import FairleadC
import Foundation

/// A WireGuard config as sections and fields. The Swift value keeps the document editable
/// from the UI. Every parse, edit, render, and lookup is done by the shared C kernel.
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
        var doc = doc_t()
        doc_init(&doc)
        defer { doc_free(&doc) }
        doc_parse(&doc, text)
        return WGDocument(copying: doc)
    }

    public func render() -> String {
        withC { doc in
            CBridge.take(doc_render(doc)) ?? ""
        }
    }

    public var interfaceIndex: Int? {
        withC { doc in
            let index = Int(doc_interface(doc))
            return index >= 0 ? index : nil
        }
    }

    public var peerIndexes: [Int] {
        withC { doc in
            var found = [Int32](repeating: 0, count: max(sections.count, 1))
            let count = Int(doc_peers(doc, &found, Int32(found.count)))
            return found.prefix(max(0, min(count, found.count))).map(Int.init)
        }
    }

    /// Each non-empty value of `key`, one per line in the file.
    public func values(section: Int, key: String) -> [String] {
        guard sections.indices.contains(section) else { return [] }
        return sections[section].entries.compactMap { entry in
            guard entry.key?.caseInsensitiveCompare(key) == .orderedSame else { return nil }
            let value = entry.value?.trimmingCharacters(in: .whitespaces) ?? ""
            return value.isEmpty ? nil : value
        }
    }

    public func value(section: Int, key: String) -> String {
        withC { doc in
            CBridge.string(capacity: 16384) { buffer, cap in
                doc_value(doc, Int32(section), key, buffer, cap)
            }
        }
    }

    public mutating func setValues(section: Int, key: String, values: [String]) {
        let cleaned = values.map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        edit { doc in
            doc_set(doc, Int32(section), key, cleaned.joined(separator: ", "))
        }
    }

    public mutating func setValue(section: Int, key: String, value: String) {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        edit { doc in
            doc_set(doc, Int32(section), key, trimmed)
        }
    }

    public mutating func ensureInterface() -> Int {
        var index = 0
        edit { doc in
            index = Int(doc_ensure_interface(doc))
        }
        return index
    }

    public mutating func addPeer() -> Int {
        var index = 0
        edit { doc in
            index = Int(doc_add_peer(doc))
        }
        return index
    }

    public mutating func removeSection(at index: Int) {
        guard sections.indices.contains(index) else { return }
        sections.remove(at: index)
        for position in sections.indices {
            sections[position].index = position
        }
    }

    public var interfacePrivateKey: String {
        guard let interfaceIndex else { return "" }
        return value(section: interfaceIndex, key: "PrivateKey")
    }

    public var interfaceAddresses: String {
        withC { doc in
            CBridge.string(capacity: 16384) { buffer, cap in
                doc_addresses(doc, buffer, cap)
            }
        }
    }

    public var interfaceDNS: String {
        withC { doc in
            CBridge.string(capacity: 16384) { buffer, cap in
                doc_dns(doc, buffer, cap)
            }
        }
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
        withC { doc in
            CBridge.string(capacity: 16384) { buffer, cap in
                doc_peer_allowed(doc, Int32(index), buffer, cap)
            }
        }
    }
    public func peerKeepalive(_ index: Int) -> String { value(section: index, key: "PersistentKeepalive") }

    public func endpointHosts() -> [String] {
        peerIndexes.compactMap { EndpointParse.host(from: peerEndpoint($0)) }
    }

    public static func blank(privateKey: String) -> WGDocument {
        var doc = doc_t()
        doc_init(&doc)
        defer { doc_free(&doc) }
        doc_blank(&doc, privateKey)
        return WGDocument(copying: doc)
    }
}

// MARK: - Crossing into doc_t

extension WGDocument {
    /// Builds a C document for the length of `body`. `doc_free` releases it, so every
    /// string and array is allocated with malloc.
    func withC<R>(_ body: (UnsafeMutablePointer<doc_t>) -> R) -> R {
        var doc = doc_t()
        doc_init(&doc)
        defer { doc_free(&doc) }

        if !preamble.isEmpty {
            let lines = malloc(preamble.count * MemoryLayout<UnsafeMutablePointer<CChar>?>.stride)!
                .bindMemory(to: UnsafeMutablePointer<CChar>?.self, capacity: preamble.count)
            for (offset, line) in preamble.enumerated() {
                lines[offset] = strdup(line)
            }
            doc.preamble = lines
            doc.preamble_count = Int32(preamble.count)
            doc.preamble_cap = Int32(preamble.count)
        }

        if !sections.isEmpty {
            let list = malloc(sections.count * MemoryLayout<section_t>.stride)!
                .bindMemory(to: section_t.self, capacity: sections.count)
            for (offset, section) in sections.enumerated() {
                var c = section_t()
                c.name = strdup(section.name)
                if !section.entries.isEmpty {
                    let entries = malloc(section.entries.count * MemoryLayout<entry_t>.stride)!
                        .bindMemory(to: entry_t.self, capacity: section.entries.count)
                    for (position, entry) in section.entries.enumerated() {
                        var e = entry_t()
                        if let key = entry.key {
                            e.is_field = 1
                            e.key = strdup(key)
                            e.value = strdup(entry.value ?? "")
                        } else {
                            e.is_field = 0
                            e.raw = strdup(entry.raw ?? "")
                        }
                        entries[position] = e
                    }
                    c.entries = entries
                    c.count = Int32(section.entries.count)
                    c.cap = Int32(section.entries.count)
                }
                list[offset] = c
            }
            doc.sections = list
            doc.section_count = Int32(sections.count)
            doc.section_cap = Int32(sections.count)
        }
        return body(&doc)
    }

    /// Runs a C edit against this document and keeps the result.
    private mutating func edit(_ change: (UnsafeMutablePointer<doc_t>) -> Void) {
        let updated: WGDocument = withC { doc in
            change(doc)
            return WGDocument(copying: doc.pointee)
        }
        self = updated
    }

    init(copying doc: doc_t) {
        var preamble: [String] = []
        for index in 0..<Int(doc.preamble_count) {
            preamble.append(doc.preamble[index].map { String(cString: $0) } ?? "")
        }
        var sections: [Section] = []
        for index in 0..<Int(doc.section_count) {
            let c = doc.sections[index]
            var entries: [Entry] = []
            for position in 0..<Int(c.count) {
                let e = c.entries[position]
                if e.is_field != 0 {
                    entries.append(.field(
                        e.key.map { String(cString: $0) } ?? "",
                        e.value.map { String(cString: $0) } ?? ""
                    ))
                } else {
                    entries.append(.line(e.raw.map { String(cString: $0) } ?? ""))
                }
            }
            sections.append(Section(
                index: index,
                name: c.name.map { String(cString: $0) } ?? "",
                entries: entries
            ))
        }
        self.init(preamble: preamble, sections: sections)
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
    /// What would stop this config from coming up, in the app language.
    public static func issues(in document: WGDocument) -> [ConfigIssue] {
        document.withC { doc in
            let (count, rows) = CBridge.rows(count: 64, width: 192) { raw in
                CBridge.rebind(raw) { config_issues(doc, $0, 64) }
            }
            return rows.prefix(Int(count)).map { ConfigIssue(FairleadL10n.text($0)) }
        }
    }

    public static func validEndpoint(_ endpoint: String) -> Bool {
        EndpointParse.isValid(endpoint)
    }
}
