import Foundation
import FairleadCore

struct IPRule: Codable, Identifiable, Equatable {
    var id: UUID
    var cidr: String
    var note: String
    var enabled: Bool

    init(id: UUID = UUID(), cidr: String, note: String = "", enabled: Bool = true) {
        self.id = id
        self.cidr = cidr
        self.note = note
        self.enabled = enabled
    }
}

struct AppRule: Codable, Identifiable, Equatable {
    var id: UUID
    var path: String
    var bundleIdentifier: String
    var name: String
    var enabled: Bool
    var learn: Bool
    var learned: [String]

    init(
        id: UUID = UUID(),
        path: String,
        bundleIdentifier: String,
        name: String,
        enabled: Bool = true,
        learn: Bool = true,
        learned: [String] = []
    ) {
        self.id = id
        self.path = path
        self.bundleIdentifier = bundleIdentifier
        self.name = name
        self.enabled = enabled
        self.learn = learn
        self.learned = learned
    }
}

struct DomainRule: Codable, Identifiable, Equatable {
    var id: UUID
    var name: String
    var note: String
    var enabled: Bool
    var addresses: [String]
    var refreshAt: Date?

    init(
        id: UUID = UUID(),
        name: String,
        note: String = "",
        enabled: Bool = true,
        addresses: [String] = [],
        refreshAt: Date? = nil
    ) {
        self.id = id
        self.name = name
        self.note = note
        self.enabled = enabled
        self.addresses = addresses
        self.refreshAt = refreshAt
    }
}

struct Tunnel: Codable, Identifiable, Equatable {
    var id: UUID
    var name: String
    var slug: String
    var configText: String
    var ipRules: [IPRule]
    var appRules: [AppRule]
    var domainRules: [DomainRule]
    var excludeLAN: Bool

    init(
        id: UUID = UUID(),
        name: String,
        slug: String = Tunnel.makeSlug(),
        configText: String,
        ipRules: [IPRule] = [],
        appRules: [AppRule] = [],
        domainRules: [DomainRule] = [],
        excludeLAN: Bool = true
    ) {
        self.id = id
        self.name = name
        self.slug = slug
        self.configText = configText
        self.ipRules = ipRules
        self.appRules = appRules
        self.domainRules = domainRules
        self.excludeLAN = excludeLAN
    }

    enum CodingKeys: String, CodingKey {
        case id, name, slug, configText, ipRules, appRules, domainRules, excludeLAN
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(UUID.self, forKey: .id)
        name = try container.decode(String.self, forKey: .name)
        slug = try container.decode(String.self, forKey: .slug)
        configText = try container.decode(String.self, forKey: .configText)
        ipRules = try container.decodeIfPresent([IPRule].self, forKey: .ipRules) ?? []
        appRules = try container.decodeIfPresent([AppRule].self, forKey: .appRules) ?? []
        domainRules = try container.decodeIfPresent([DomainRule].self, forKey: .domainRules) ?? []
        excludeLAN = try container.decodeIfPresent(Bool.self, forKey: .excludeLAN) ?? true
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(id, forKey: .id)
        try container.encode(name, forKey: .name)
        try container.encode(slug, forKey: .slug)
        try container.encode(configText, forKey: .configText)
        try container.encode(ipRules, forKey: .ipRules)
        try container.encode(appRules, forKey: .appRules)
        try container.encode(domainRules, forKey: .domainRules)
        try container.encode(excludeLAN, forKey: .excludeLAN)
    }

    static func makeSlug() -> String {
        let hex = UUID().uuidString.replacingOccurrences(of: "-", with: "").lowercased()
        return "fld" + hex.prefix(8)
    }

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

    func directCIDRs() -> [String] {
        var values: [String] = []
        for rule in ipRules where rule.enabled {
            values.append(rule.cidr)
        }
        for app in appRules where app.enabled {
            values.append(contentsOf: app.learned)
        }
        for domain in domainRules where domain.enabled {
            values.append(contentsOf: domain.addresses)
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
    var gatewayIface: String?
    var resolvers: [String] = []
    var resolverIface: String?
    var lanCIDRs: [String] = []
    var endpointIPs: [String] = []
    var appliedRoutes: [String] = []
    var warnings: [String] = []
    var rx: UInt64 = 0
    var tx: UInt64 = 0
    var rateDown: Double = 0
    var rateUp: Double = 0
    var handshake: Date?
    var endpoint: String?
    var sampleDate: Date?
    var sampleRx: UInt64 = 0
    var sampleTx: UInt64 = 0
}
