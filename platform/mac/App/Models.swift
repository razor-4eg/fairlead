//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// Models.swift -- Tunnels, rules, and the connection phase.
//
//===============================================================

import Foundation
import FairleadCore

enum RouteKind: String, Codable, Equatable {
    case ip
    case domain
}

/// Older tunnels.json files also carry resolved addresses and a refresh time. They decode and are dropped.
struct RouteEntry: Codable, Identifiable, Equatable {
    var id: UUID
    var kind: RouteKind
    var value: String

/*
===============
init
===============
*/
    init(id: UUID = UUID(), kind: RouteKind, value: String) {
        self.id = id
        self.kind = kind
        self.value = value
    }
}

struct RouteList: Codable, Identifiable, Equatable {
    var id: UUID
    var name: String
    var enabled: Bool
    var entries: [RouteEntry]

/*
===============
init
===============
*/
    init(id: UUID = UUID(), name: String, enabled: Bool = true, entries: [RouteEntry] = []) {
        self.id = id
        self.name = name
        self.enabled = enabled
        self.entries = entries
    }
}

/*
===============
LegacyIPRule
An address rule from a tunnel saved before lists.
===============
*/
private struct LegacyIPRule: Codable {
    var id: UUID
    var cidr: String
    var note: String?
    var enabled: Bool?
}

/*
===============
LegacyDomainRule
A domain rule from a tunnel saved before lists.
===============
*/
private struct LegacyDomainRule: Codable {
    var id: UUID
    var name: String
    var note: String?
    var enabled: Bool?
}

/// App path for mihomo PROCESS-PATH. Unknown keys from older tunnels.json are ignored.
struct AppRule: Codable, Identifiable, Equatable {
    var id: UUID
    var path: String
    var bundleIdentifier: String
    var name: String
    var enabled: Bool

/*
===============
init
===============
*/
    init(
        id: UUID = UUID(),
        path: String,
        bundleIdentifier: String,
        name: String,
        enabled: Bool = true
    ) {
        self.id = id
        self.path = path
        self.bundleIdentifier = bundleIdentifier
        self.name = name
        self.enabled = enabled
    }
}

struct Tunnel: Codable, Identifiable, Equatable {
    var id: UUID
    var name: String
    var slug: String
    var configText: String
    var routeLists: [RouteList]
    var appRules: [AppRule]
    var excludeLAN: Bool
    /// When on, UDP/443 (HTTP/3 QUIC) is rejected for tunnel traffic so browsers recover after rule flips.
    var blockQUIC: Bool
    var routePolicy: RoutePolicy

/*
===============
init
===============
*/
    init(
        id: UUID = UUID(),
        name: String,
        slug: String = Tunnel.makeSlug(),
        configText: String,
        routeLists: [RouteList] = [],
        appRules: [AppRule] = [],
        excludeLAN: Bool = true,
        blockQUIC: Bool = true,
        routePolicy: RoutePolicy = .exceptions
    ) {
        self.id = id
        self.name = name
        self.slug = slug
        self.configText = configText
        self.routeLists = routeLists
        self.appRules = appRules
        self.excludeLAN = excludeLAN
        self.blockQUIC = blockQUIC
        self.routePolicy = routePolicy
    }

    enum CodingKeys: String, CodingKey {
        case id, name, slug, configText, ipRules, appRules, domainRules, routeLists, excludeLAN, blockQUIC, routePolicy
    }

/*
===============
init
===============
*/
    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(UUID.self, forKey: .id)
        name = try container.decode(String.self, forKey: .name)
        slug = try container.decode(String.self, forKey: .slug)
        configText = try container.decode(String.self, forKey: .configText)
        appRules = try container.decodeIfPresent([AppRule].self, forKey: .appRules) ?? []
        excludeLAN = try container.decodeIfPresent(Bool.self, forKey: .excludeLAN) ?? true
        blockQUIC = try container.decodeIfPresent(Bool.self, forKey: .blockQUIC) ?? true
        routePolicy = try container.decodeIfPresent(RoutePolicy.self, forKey: .routePolicy) ?? .exceptions
        if container.contains(.routeLists) {
            routeLists = try container.decodeIfPresent([RouteList].self, forKey: .routeLists) ?? []
        } else {
            let ips = try container.decodeIfPresent([LegacyIPRule].self, forKey: .ipRules) ?? []
            let domains = try container.decodeIfPresent([LegacyDomainRule].self, forKey: .domainRules) ?? []
            routeLists = Tunnel.lists(from: ips, domains: domains)
        }
    }

/*
===============
encode
===============
*/
    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(id, forKey: .id)
        try container.encode(name, forKey: .name)
        try container.encode(slug, forKey: .slug)
        try container.encode(configText, forKey: .configText)
        try container.encode(routeLists, forKey: .routeLists)
        try container.encode(appRules, forKey: .appRules)
        try container.encode(excludeLAN, forKey: .excludeLAN)
        try container.encode(blockQUIC, forKey: .blockQUIC)
        try container.encode(routePolicy, forKey: .routePolicy)
    }

/*
===============
lists
One old rule becomes one list. The note, when it had one, is the list name.
===============
*/
    private static func lists(from ips: [LegacyIPRule], domains: [LegacyDomainRule]) -> [RouteList] {
        var lists: [RouteList] = []
        for rule in ips {
            let note = (rule.note ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            lists.append(RouteList(
                id: rule.id,
                name: note.isEmpty ? rule.cidr : note,
                enabled: rule.enabled ?? true,
                entries: [RouteEntry(kind: .ip, value: rule.cidr)]
            ))
        }

        for rule in domains {
            let note = (rule.note ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            lists.append(RouteList(
                id: rule.id,
                name: note.isEmpty ? rule.name : note,
                enabled: rule.enabled ?? true,
                entries: [RouteEntry(kind: .domain, value: rule.name)]
            ))
        }

        return lists
    }

/*
===============
makeSlug
===============
*/
    static func makeSlug() -> String {
        let hex = UUID().uuidString.replacingOccurrences(of: "-", with: "").lowercased()
        return "fld" + hex.prefix(8)
    }

/*
===============
blank
===============
*/
    static func blank(name: String) -> Tunnel {
        let keys = WGKeys.generate()
        let document = WGDocument.blank(privateKey: keys.privateKey)
        return Tunnel(name: name, configText: document.render())
    }

    var document: WGDocument { WGDocument.parse(configText) }

    var endpointLabel: String {
        let peers = document.peerIndexes
        guard let peer = peers.first else { return FairleadL10n.text("no peer") }
        let endpoint = document.peerEndpoint(peer)
        return endpoint.isEmpty ? FairleadL10n.text("endpoint not set") : endpoint
    }

    var addressLabel: String {
        let addresses = document.interfaceAddresses
        return addresses.isEmpty ? FairleadL10n.text("address not set") : addresses
    }

/*
===============
directCIDRs
Literal addresses and networks only. mihomo resolves the domains itself.
===============
*/
    func directCIDRs() -> [String] {
        entryValues(.ip)
    }

/*
===============
directDomains
===============
*/
    func directDomains() -> [String] {
        entryValues(.domain)
    }

/*
===============
directApps
Process paths of the enabled apps. mihomo matches them by path.
===============
*/
    func directApps() -> [String] {
        appRules.filter { $0.enabled && !$0.path.isEmpty }.map(\.path)
    }

/*
===============
entryValues
===============
*/
    private func entryValues(_ kind: RouteKind) -> [String] {
        var values: [String] = []
        for list in routeLists where list.enabled {
            for entry in list.entries where entry.kind == kind {
                values.append(entry.value)
            }
        }

        return values
    }
}

enum Phase: Equatable {
    case idle
    case connecting
    case connected
    case disconnecting
}

struct LiveTunnel: Equatable {
    var phase: Phase = .idle
    var realInterface: String?
    var gateway: String?
    var lanCIDRs: [String] = []
    var warnings: [String] = []
    var rx: UInt64 = 0
    var tx: UInt64 = 0
    var rateDown: Double = 0
    var rateUp: Double = 0
    var handshake: Date?
    /// Last successful HTTP probe RTT through the tunnel (liveness only).
    var delayMs: Int = 0
    /// ICMP echo RTT to the WireGuard endpoint, shown in the status line.
    var pingMs: Int = 0
    /// Progress while Connecting. Empty once Connected.
    var stage: String?
    var endpoint: String?
    var sampleDate: Date?
    var sampleRx: UInt64 = 0
    var sampleTx: UInt64 = 0
}
