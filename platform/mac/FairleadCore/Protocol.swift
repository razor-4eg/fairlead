import Foundation

public struct ToolReport: Codable, Equatable, Sendable {
    public var wg: String?
    public var wgQuick: String?
    public var wireguardGo: String?
    public var amneziaWG: String?
    public var brew: String?

    /// amneziawg-go carries plain WireGuard and AmneziaWG.
    public var ready: Bool { amneziaWG != nil }

    public init(
        wg: String? = nil,
        wgQuick: String? = nil,
        wireguardGo: String? = nil,
        amneziaWG: String? = nil,
        brew: String? = nil
    ) {
        self.wg = wg
        self.wgQuick = wgQuick
        self.wireguardGo = wireguardGo
        self.amneziaWG = amneziaWG
        self.brew = brew
    }
}

public struct ManagedTunnel: Codable, Equatable, Sendable {
    public var name: String
    public var realInterface: String
    public var gatewayV4: String?
    public var gatewayV6: String?
    public var gatewayIface: String?
    public var lanCIDRs: [String]
    public var bypass: [String]
    public var endpointIPs: [String]

    public init(
        name: String,
        realInterface: String,
        gatewayV4: String?,
        gatewayV6: String?,
        gatewayIface: String?,
        lanCIDRs: [String],
        bypass: [String],
        endpointIPs: [String]
    ) {
        self.name = name
        self.realInterface = realInterface
        self.gatewayV4 = gatewayV4
        self.gatewayV6 = gatewayV6
        self.gatewayIface = gatewayIface
        self.lanCIDRs = lanCIDRs
        self.bypass = bypass
        self.endpointIPs = endpointIPs
    }
}

public struct RPCRequest: Codable, Sendable {
    public var id: String
    public var op: String
    public var name: String?
    public var config: String?
    public var cidrs: [String]?
    public var excludeLAN: Bool?
    public var policy: String?

    public init(
        id: String = UUID().uuidString,
        op: String,
        name: String? = nil,
        config: String? = nil,
        cidrs: [String]? = nil,
        excludeLAN: Bool? = nil,
        policy: String? = nil
    ) {
        self.id = id
        self.op = op
        self.name = name
        self.config = config
        self.cidrs = cidrs
        self.excludeLAN = excludeLAN
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
        var amnezia = [
            "/usr/local/libexec/fairlead-amneziawg-go",
            "/opt/homebrew/bin/amneziawg-go",
            "/usr/local/bin/amneziawg-go",
        ]
        if let directory = Bundle.main.executableURL?.deletingLastPathComponent() {
            amnezia.insert(directory.appendingPathComponent("amneziawg-go").path, at: 0)
        }
        return ToolReport(amneziaWG: firstExecutable(amnezia))
    }

    private static func firstExecutable(_ paths: [String]) -> String? {
        paths.first { FileManager.default.isExecutableFile(atPath: $0) }
    }
}
