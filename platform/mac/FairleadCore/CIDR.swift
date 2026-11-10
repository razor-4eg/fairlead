import FairleadC
import Foundation

public enum IPFamily: Equatable {
    case v4
    case v6
}

/// An IPv4 or IPv6 address. Parsing, printing, and classification live in the shared C kernel.
public struct IPAddress: Equatable, Hashable {
    public var family: IPFamily
    public var bytes: [UInt8]

    public init(family: IPFamily, bytes: [UInt8]) {
        self.family = family
        self.bytes = bytes
    }

    public var string: String {
        var ip = cValue
        return CBridge.string(capacity: 64) { buffer, cap in
            fl_ip_format(&ip, buffer, cap)
        }
    }

    public static func parse(_ text: String) -> IPAddress? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        var ip = fl_ip_t()
        guard fl_ip_parse(trimmed, &ip) != 0 else { return nil }
        return IPAddress(cValue: ip)
    }

    public var isUnspecified: Bool { check(fl_ip_unspecified) }
    public var isLoopback: Bool { check(fl_ip_loopback) }
    public var isMulticast: Bool { check(fl_ip_multicast) }
    public var isLinkLocal: Bool { check(fl_ip_link_local) }
    public var isBroadcast: Bool { check(fl_ip_broadcast) }

    /// An address worth a bypass route: a real remote host, not loopback or multicast.
    public var isRoutableDestination: Bool { check(fl_ip_routable) }

    private func check(_ test: (UnsafePointer<fl_ip_t>?) -> Int32) -> Bool {
        var ip = cValue
        return test(&ip) != 0
    }

    init(cValue ip: fl_ip_t) {
        var copy = ip
        let family: IPFamily = ip.v6 != 0 ? .v6 : .v4
        let length = family == .v4 ? 4 : 16
        self.family = family
        self.bytes = withUnsafeBytes(of: &copy.bytes) { Array($0.prefix(length)) }
    }

    var cValue: fl_ip_t {
        var ip = fl_ip_t()
        ip.v6 = family == .v6 ? 1 : 0
        withUnsafeMutableBytes(of: &ip.bytes) { raw in
            for (index, byte) in bytes.prefix(16).enumerated() {
                raw[index] = byte
            }
        }
        return ip
    }

    func masked(prefix: Int) -> IPAddress {
        var ip = cValue
        fl_ip_mask(&ip, Int32(prefix))
        return IPAddress(cValue: ip)
    }
}

public struct CIDR: Equatable, Hashable {
    public var address: IPAddress
    public var prefix: Int

    public init(address: IPAddress, prefix: Int) {
        self.address = address
        self.prefix = prefix
    }

    public var string: String {
        var cidr = cValue
        return CBridge.string(capacity: 96) { buffer, cap in
            fl_cidr_format(&cidr, buffer, cap)
        }
    }

    public var family: IPFamily { address.family }

    public var maxPrefix: Int {
        var cidr = cValue
        return Int(fl_cidr_max_prefix(&cidr))
    }

    public static func parse(_ text: String) -> CIDR? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        var cidr = fl_cidr_t()
        guard fl_cidr_parse(trimmed, &cidr) != 0 else { return nil }
        return CIDR(cValue: cidr)
    }

    public func contains(_ ip: IPAddress) -> Bool {
        var cidr = cValue
        var other = ip.cValue
        return fl_cidr_contains(&cidr, &other) != 0
    }

    init(cValue cidr: fl_cidr_t) {
        self.address = IPAddress(cValue: cidr.ip)
        self.prefix = Int(cidr.prefix)
    }

    var cValue: fl_cidr_t {
        var cidr = fl_cidr_t()
        cidr.ip = address.cValue
        cidr.prefix = Int32(prefix)
        return cidr
    }
}

public enum EndpointParse {
    /// `host:port` or `[ipv6]:port`. Returns the host portion.
    public static func host(from endpoint: String) -> String? {
        let value = endpoint.trimmingCharacters(in: .whitespacesAndNewlines)
        return CBridge.optionalString(capacity: 512) { buffer, cap in
            fl_endpoint_host(value, buffer, cap)
        }
    }

    public static func port(from endpoint: String) -> Int? {
        var port: Int32 = 0
        let value = endpoint.trimmingCharacters(in: .whitespacesAndNewlines)
        guard fl_endpoint_port(value, &port) != 0 else { return nil }
        return Int(port)
    }

    public static func isValid(_ endpoint: String) -> Bool {
        fl_endpoint_valid(endpoint.trimmingCharacters(in: .whitespaces)) != 0
    }
}

public struct RouteSnapshot: Equatable {
    public var destination: String?
    public var mask: String?
    public var gateway: String?
    public var interface: String?
}

public enum RouteText {
    public static func parseRouteGet(_ text: String) -> (gateway: String?, interface: String?) {
        let snapshot = snapshot(from: text)
        return (snapshot.gateway, snapshot.interface)
    }

    public static func snapshot(from text: String) -> RouteSnapshot {
        var snap = fl_route_snap_t()
        fl_route_snapshot(text, &snap)
        return RouteSnapshot(
            destination: snap.has_destination != 0 ? CBridge.text(snap.destination) : nil,
            mask: snap.has_mask != 0 ? CBridge.text(snap.mask) : nil,
            gateway: snap.has_gateway != 0 ? CBridge.text(snap.gateway) : nil,
            interface: snap.has_interface != 0 ? CBridge.text(snap.interface_name) : nil
        )
    }

    /// True when `route get` is describing this prefix itself, not a less-specific match.
    public static func isExact(_ cidr: CIDR, snapshot: RouteSnapshot) -> Bool {
        var snap = fl_route_snap_t()
        if let destination = snapshot.destination {
            CBridge.fill(&snap.destination, destination)
            snap.has_destination = 1
        }
        if let mask = snapshot.mask {
            CBridge.fill(&snap.mask, mask)
            snap.has_mask = 1
        }
        if let gateway = snapshot.gateway {
            CBridge.fill(&snap.gateway, gateway)
            snap.has_gateway = 1
        }
        if let interface = snapshot.interface {
            CBridge.fill(&snap.interface_name, interface)
            snap.has_interface = 1
        }
        var value = cidr.cValue
        return fl_route_exact(&value, &snap) != 0
    }

    /// Pulls on-link networks from `ifconfig` output for one interface.
    public static func networks(fromIfconfig text: String) -> [String] {
        let cap = 64
        let (count, rows) = CBridge.rows(count: cap, width: 64) { raw in
            CBridge.rebind(raw) { fl_ifconfig_networks(text, $0, Int32(cap)) }
        }
        return unique(Array(rows.prefix(Int(count))))
    }

    public static func unique(_ values: [String]) -> [String] {
        var seen = Set<String>()
        var ordered: [String] = []
        for value in values where seen.insert(value).inserted {
            ordered.append(value)
        }
        return ordered
    }
}
