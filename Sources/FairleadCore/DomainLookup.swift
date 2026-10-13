import Darwin
import Foundation

/// Resolves one hostname with a direct UDP query. Binding to the physical interface
/// keeps the lookup off the tunnel while a default route points at utun.
public enum DomainLookup {
    public struct Answer: Equatable, Sendable {
        public var v4: [String]?
        public var v6: [String]?
        public var ttl: Int

        public init(v4: [String]?, v6: [String]?, ttl: Int) {
            self.v4 = v4
            self.v6 = v6
            self.ttl = ttl
        }
    }

    public static func resolve(name: String, servers: [String], interface: String?) -> Answer? {
        let servers = servers.prefix(3).filter { IPAddress.parse($0) != nil }
        guard !servers.isEmpty, DomainName.normalize(name) != nil else { return nil }
        var v4: [String]?
        var v6: [String]?
        var ttl = UInt32.max
        for server in servers {
            if v4 == nil, let part = ask(server: server, name: name, type: DNSCodec.typeA, interface: interface) {
                v4 = part.addresses
                ttl = min(ttl, part.ttl)
            }
            if v6 == nil, let part = ask(server: server, name: name, type: DNSCodec.typeAAAA, interface: interface) {
                v6 = part.addresses
                ttl = min(ttl, part.ttl)
            }
            if v4 != nil, v6 != nil { break }
        }
        guard v4 != nil || v6 != nil else { return nil }
        let seconds = ttl == UInt32.max ? 60 : min(3600, max(30, Int(ttl)))
        return Answer(v4: v4, v6: v6, ttl: seconds)
    }

    private struct Part {
        var addresses: [String]
        var ttl: UInt32
    }

    private static func ask(server: String, name: String, type: UInt16, interface: String?) -> Part? {
        guard let reply = exchange(server: server, name: name, type: type, interface: interface) else { return nil }
        if reply.rcode == 3 { return Part(addresses: [], ttl: 60) }
        guard reply.rcode == 0, !reply.truncated else { return nil }
        let chosen = reply.answers.isEmpty && reply.sawCNAME ? reply.additional : reply.answers
        var addresses: [String] = []
        var ttl: UInt32 = 60
        var saw = false
        for record in chosen {
            guard let ip = IPAddress.parse(record.address), ip.isRoutableDestination else { continue }
            if addresses.contains(ip.string) { continue }
            addresses.append(ip.string)
            ttl = min(ttl, max(record.ttl, 1))
            saw = true
            if addresses.count == 32 { break }
        }
        return Part(addresses: addresses, ttl: saw ? ttl : 60)
    }

    private static func exchange(server: String, name: String, type: UInt16, interface: String?) -> DNSCodec.Reply? {
        let id = UInt16.random(in: .min ... .max)
        if let query = DNSCodec.query(id: id, name: name, type: type, edns: true),
           let data = transmit(query, to: server, interface: interface),
           let reply = DNSCodec.parse(data, id: id),
           !reply.truncated,
           reply.rcode == 0 || reply.rcode == 3 {
            return reply
        }
        guard let query = DNSCodec.query(id: id, name: name, type: type, edns: false),
              let data = transmit(query, to: server, interface: interface),
              let reply = DNSCodec.parse(data, id: id), !reply.truncated else { return nil }
        return reply
    }

    private static func transmit(_ payload: Data, to server: String, interface: String?) -> Data? {
        let v6 = server.contains(":")
        let fd = socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP)
        guard fd >= 0 else { return nil }
        defer { close(fd) }
        var nosig: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, socklen_t(MemoryLayout<Int32>.size))
        var timeout = timeval(tv_sec: 2, tv_usec: 0)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        if let interface {
            let index = if_nametoindex(interface)
            if index != 0 {
                var value = index
                let level: Int32 = v6 ? Int32(IPPROTO_IPV6) : Int32(IPPROTO_IP)
                let option: Int32 = v6 ? 125 : 25
                setsockopt(fd, level, option, &value, socklen_t(MemoryLayout<UInt32>.size))
            }
        }
        guard connectUDP(fd, server: server, v6: v6) else { return nil }
        let sent = payload.withUnsafeBytes { raw -> Int in
            guard let base = raw.baseAddress else { return -1 }
            return Darwin.send(fd, base, raw.count, 0)
        }
        guard sent == payload.count else { return nil }
        var buffer = [UInt8](repeating: 0, count: 4096)
        let got = buffer.withUnsafeMutableBytes { raw -> Int in
            guard let base = raw.baseAddress else { return -1 }
            return recv(fd, base, raw.count, 0)
        }
        guard got > 0 else { return nil }
        return Data(buffer.prefix(got))
    }

    private static func connectUDP(_ fd: Int32, server: String, v6: Bool) -> Bool {
        if v6 {
            var addr = sockaddr_in6()
            addr.sin6_len = UInt8(MemoryLayout<sockaddr_in6>.size)
            addr.sin6_family = sa_family_t(AF_INET6)
            addr.sin6_port = UInt16(53).bigEndian
            let parsed = withUnsafeMutableBytes(of: &addr.sin6_addr) { raw in
                inet_pton(AF_INET6, server, raw.baseAddress)
            }
            guard parsed == 1 else { return false }
            return withUnsafePointer(to: &addr) { pointer in
                pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) { raw in
                    connect(fd, raw, socklen_t(MemoryLayout<sockaddr_in6>.size)) == 0
                }
            }
        }
        var addr = sockaddr_in()
        addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = UInt16(53).bigEndian
        let parsed = withUnsafeMutableBytes(of: &addr.sin_addr) { raw in
            inet_pton(AF_INET, server, raw.baseAddress)
        }
        guard parsed == 1 else { return false }
        return withUnsafePointer(to: &addr) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) { raw in
                connect(fd, raw, socklen_t(MemoryLayout<sockaddr_in>.size)) == 0
            }
        }
    }
}
