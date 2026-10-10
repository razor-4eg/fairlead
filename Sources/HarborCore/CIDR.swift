import Darwin
import Foundation

public enum IPFamily: Equatable {
    case v4
    case v6
}

public struct IPAddress: Equatable, Hashable {
    public var family: IPFamily
    public var bytes: [UInt8]

    public var string: String {
        var storage = bytes
        var buffer = [CChar](repeating: 0, count: Int(INET6_ADDRSTRLEN))
        let family: Int32 = self.family == .v4 ? AF_INET : AF_INET6
        let ok = storage.withUnsafeMutableBytes { raw -> Bool in
            guard let base = raw.baseAddress else { return false }
            return inet_ntop(family, base, &buffer, socklen_t(buffer.count)) != nil
        }
        guard ok else { return "" }
        return String(cString: buffer)
    }

    public static func parse(_ text: String) -> IPAddress? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        var v4 = [UInt8](repeating: 0, count: 4)
        if v4.withUnsafeMutableBytes({ raw in
            inet_pton(AF_INET, trimmed, raw.baseAddress) == 1
        }) {
            return IPAddress(family: .v4, bytes: v4)
        }
        var v6 = [UInt8](repeating: 0, count: 16)
        if v6.withUnsafeMutableBytes({ raw in
            inet_pton(AF_INET6, trimmed, raw.baseAddress) == 1
        }) {
            return IPAddress(family: .v6, bytes: v6)
        }
        return nil
    }

    public var isUnspecified: Bool { bytes.allSatisfy { $0 == 0 } }

    public var isLoopback: Bool {
        switch family {
        case .v4:
            return bytes[0] == 127
        case .v6:
            return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0
                && bytes[4] == 0 && bytes[5] == 0 && bytes[6] == 0 && bytes[7] == 0
                && bytes[8] == 0 && bytes[9] == 0 && bytes[10] == 0 && bytes[11] == 0
                && bytes[12] == 0 && bytes[13] == 0 && bytes[14] == 0 && bytes[15] == 1
        }
    }

    public var isMulticast: Bool {
        switch family {
        case .v4:
            return bytes[0] >= 224 && bytes[0] <= 239
        case .v6:
            return bytes[0] == 0xff
        }
    }

    public var isLinkLocal: Bool {
        switch family {
        case .v4:
            return bytes[0] == 169 && bytes[1] == 254
        case .v6:
            return bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80
        }
    }

    public var isBroadcast: Bool {
        family == .v4 && bytes.allSatisfy { $0 == 255 }
    }

    /// An address worth a bypass route: a real remote host, not loopback or multicast.
    public var isRoutableDestination: Bool {
        !isUnspecified && !isLoopback && !isMulticast && !isLinkLocal && !isBroadcast
    }
}

public struct CIDR: Equatable, Hashable {
    public var address: IPAddress
    public var prefix: Int

    public var string: String {
        "\(address.string)/\(prefix)"
    }

    public var family: IPFamily { address.family }

    public var maxPrefix: Int { family == .v4 ? 32 : 128 }

    public static func parse(_ text: String) -> CIDR? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        let parts = trimmed.split(separator: "/", omittingEmptySubsequences: false)
        if parts.count > 2 { return nil }
        guard let ip = IPAddress.parse(String(parts[0])) else { return nil }
        let prefix: Int
        if parts.count == 1 {
            prefix = ip.family == .v4 ? 32 : 128
        } else {
            guard let value = Int(parts[1]), value >= 0 else { return nil }
            prefix = value
        }
        let limit = ip.family == .v4 ? 32 : 128
        guard prefix <= limit else { return nil }
        return CIDR(address: ip.masked(prefix: prefix), prefix: prefix)
    }

    public func contains(_ ip: IPAddress) -> Bool {
        guard ip.family == address.family else { return false }
        if prefix == 0 { return true }
        let fullBytes = prefix / 8
        let remainder = prefix % 8
        for index in 0..<fullBytes where ip.bytes[index] != address.bytes[index] {
            return false
        }
        if remainder == 0 { return true }
        let mask = UInt8(0xff << (8 - remainder))
        return (ip.bytes[fullBytes] & mask) == (address.bytes[fullBytes] & mask)
    }
}

extension IPAddress {
    func masked(prefix: Int) -> IPAddress {
        var copy = bytes
        let limit = family == .v4 ? 32 : 128
        let bits = max(0, min(prefix, limit))
        let fullBytes = bits / 8
        let remainder = bits % 8
        if fullBytes < copy.count {
            if remainder == 0 {
                for index in fullBytes..<copy.count {
                    copy[index] = 0
                }
            } else {
                let mask = UInt8(0xff << (8 - remainder))
                copy[fullBytes] &= mask
                if fullBytes + 1 < copy.count {
                    for index in (fullBytes + 1)..<copy.count {
                        copy[index] = 0
                    }
                }
            }
        }
        return IPAddress(family: family, bytes: copy)
    }
}

public enum EndpointParse {
    /// `host:port` or `[ipv6]:port`. Returns the host portion.
    public static func host(from endpoint: String) -> String? {
        let value = endpoint.trimmingCharacters(in: .whitespacesAndNewlines)
        if value.isEmpty || value == "(none)" { return nil }
        if value.hasPrefix("[") {
            guard let end = value.firstIndex(of: "]") else { return nil }
            let host = String(value[value.index(after: value.startIndex)..<end])
            return host.isEmpty ? nil : host
        }
        guard let colon = value.lastIndex(of: ":") else { return value }
        let host = String(value[..<colon])
        let port = String(value[value.index(after: colon)...])
        guard Int(port) != nil else { return value }
        return host.isEmpty ? nil : host
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
        var destination: String?
        var mask: String?
        var gateway: String?
        var interface: String?
        for line in text.split(whereSeparator: \.isNewline) {
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            func value(_ label: String) -> String? {
                guard trimmed.hasPrefix(label) else { return nil }
                let parsed = trimmed.dropFirst(label.count).trimmingCharacters(in: .whitespaces)
                return parsed.isEmpty ? nil : parsed
            }
            if let parsed = value("destination:") { destination = parsed }
            else if let parsed = value("mask:") { mask = parsed }
            else if let parsed = value("gateway:") { gateway = parsed }
            else if let parsed = value("interface:") { interface = parsed }
        }
        return RouteSnapshot(destination: destination, mask: mask, gateway: gateway, interface: interface)
    }

    /// True when `route get` is describing this prefix itself, not a less-specific match.
    public static func isExact(_ cidr: CIDR, snapshot: RouteSnapshot) -> Bool {
        guard let destination = snapshot.destination, destination != "default",
              let dest = IPAddress.parse(destination), dest.family == cidr.family else {
            return false
        }
        let prefix: Int
        if let mask = snapshot.mask, mask != "default" {
            if let bits = Int(mask), (0...cidr.maxPrefix).contains(bits) {
                prefix = bits
            } else if let bits = prefixLength(dotted: mask) {
                prefix = bits
            } else {
                return false
            }
        } else {
            prefix = cidr.maxPrefix
        }
        return prefix == cidr.prefix && dest.masked(prefix: prefix).bytes == cidr.address.bytes
    }

    /// Pulls on-link networks from `ifconfig` output for one interface.
    public static func networks(fromIfconfig text: String) -> [String] {
        var found: [String] = []
        for raw in text.split(whereSeparator: \.isNewline) {
            let line = raw.trimmingCharacters(in: .whitespaces)
            let parts = line.split(whereSeparator: \.isWhitespace).map(String.init)
            if parts.count >= 4, parts[0] == "inet", let ip = IPAddress.parse(parts[1]) {
                let maskIndex = parts.firstIndex(of: "netmask").map { $0 + 1 }
                guard let maskIndex, maskIndex < parts.count, let prefix = prefixLength(hexMask: parts[maskIndex]) else {
                    continue
                }
                if let cidr = CIDR(address: ip.masked(prefix: prefix), prefix: prefix).validatedNetwork() {
                    found.append(cidr.string)
                }
            } else if parts.count >= 4, parts[0] == "inet6" {
                let addrToken = parts[1].split(separator: "%").first.map(String.init) ?? parts[1]
                guard let ip = IPAddress.parse(addrToken), !ip.isLinkLocal, !ip.isLoopback else { continue }
                guard let prefixIndex = parts.firstIndex(of: "prefixlen"), prefixIndex + 1 < parts.count,
                      let prefix = Int(parts[prefixIndex + 1]), (0...128).contains(prefix) else { continue }
                if let cidr = CIDR(address: ip.masked(prefix: prefix), prefix: prefix).validatedNetwork() {
                    found.append(cidr.string)
                }
            }
        }
        return unique(found)
    }

    private static func prefixLength(dotted mask: String) -> Int? {
        let parts = mask.split(separator: ".").compactMap { UInt8($0) }
        guard parts.count == 4 else { return nil }
        var value: UInt32 = 0
        for part in parts {
            value = (value << 8) | UInt32(part)
        }
        return prefixLength(value: value)
    }

    private static func prefixLength(hexMask: String) -> Int? {
        let hex = hexMask.hasPrefix("0x") ? String(hexMask.dropFirst(2)) : hexMask
        guard let value = UInt32(hex, radix: 16) else { return nil }
        return prefixLength(value: value)
    }

    private static func prefixLength(value: UInt32) -> Int? {
        var bits = 0
        var seenZero = false
        for shift in stride(from: 31, through: 0, by: -1) {
            let on = (value & (UInt32(1) << shift)) != 0
            if on {
                if seenZero { return nil }
                bits += 1
            } else {
                seenZero = true
            }
        }
        return bits
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

private extension CIDR {
    func validatedNetwork() -> CIDR? {
        guard prefix > 0, prefix < maxPrefix else { return nil }
        return self
    }
}
