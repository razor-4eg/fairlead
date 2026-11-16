import Foundation

/// Which way the address list steers traffic.
public enum RoutePolicy: String, Codable, Equatable, Sendable {
    /// The tunnel keeps its routes. Listed addresses go out through the regular network.
    case exceptions
    /// Listed addresses go through the tunnel. Everything else stays on the regular network.
    case selected
}

public struct ToolReport: Codable, Equatable, Sendable {
    public var mihomo: String?

    /// mihomo carries plain WireGuard and AmneziaWG, the TUN, and the routing rules.
    public var ready: Bool { mihomo != nil }

    public init(mihomo: String? = nil) {
        self.mihomo = mihomo
    }
}

public struct ManagedTunnel: Codable, Equatable, Sendable {
    public var name: String
    public var realInterface: String
    public var gatewayV4: String?
    public var lanCIDRs: [String]

    public init(
        name: String,
        realInterface: String,
        gatewayV4: String?,
        lanCIDRs: [String]
    ) {
        self.name = name
        self.realInterface = realInterface
        self.gatewayV4 = gatewayV4
        self.lanCIDRs = lanCIDRs
    }
}

public struct RPCRequest: Codable, Sendable {
    public var id: String
    public var op: String
    public var name: String?
    public var config: String?
    public var cidrs: [String]?
    public var domains: [String]?
    public var apps: [String]?
    public var excludeLAN: Bool?
    public var blockQUIC: Bool?
    public var policy: String?

    public init(
        id: String = UUID().uuidString,
        op: String,
        name: String? = nil,
        config: String? = nil,
        cidrs: [String]? = nil,
        domains: [String]? = nil,
        apps: [String]? = nil,
        excludeLAN: Bool? = nil,
        blockQUIC: Bool? = nil,
        policy: String? = nil
    ) {
        self.id = id
        self.op = op
        self.name = name
        self.config = config
        self.cidrs = cidrs
        self.domains = domains
        self.apps = apps
        self.excludeLAN = excludeLAN
        self.blockQUIC = blockQUIC
        self.policy = policy
    }
}

public struct RPCResponse: Codable, Sendable {
    public var id: String
    public var ok: Bool
    public var error: String?
    public var warnings: [String]?
    public var tools: ToolReport?
    public var managed: [ManagedTunnel]?
    public var devices: [WGDeviceStatus]?

    public init(
        id: String,
        ok: Bool,
        error: String? = nil,
        warnings: [String]? = nil,
        tools: ToolReport? = nil,
        managed: [ManagedTunnel]? = nil,
        devices: [WGDeviceStatus]? = nil
    ) {
        self.id = id
        self.ok = ok
        self.error = error
        self.warnings = warnings
        self.tools = tools
        self.managed = managed
        self.devices = devices
    }
}

public enum RPCFrame {
    public static let maxBytes = 2_000_000

    public static func encode<T: Encodable>(_ value: T) throws -> Data {
        try JSONEncoder().encode(value)
    }

    public static func decode<T: Decodable>(_ type: T.Type, from data: Data) throws -> T {
        try JSONDecoder().decode(type, from: data)
    }
}

public enum ToolLocator {
    public static func locate() -> ToolReport {
        var candidates = ["/usr/local/libexec/fairlead-mihomo"]
        if let directory = Bundle.main.executableURL?.deletingLastPathComponent() {
            candidates.insert(directory.appendingPathComponent("mihomo").path, at: 0)
        }
        return ToolReport(mihomo: firstExecutable(candidates))
    }

    private static func firstExecutable(_ paths: [String]) -> String? {
        paths.first { FileManager.default.isExecutableFile(atPath: $0) }
    }
}
