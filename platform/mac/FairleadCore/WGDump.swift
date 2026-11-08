import Foundation

public struct WGPeerStatus: Equatable, Codable, Sendable {
    public var publicKey: String
    public var endpoint: String
    public var allowedIPs: String
    public var handshake: Int
    public var rx: UInt64
    public var tx: UInt64
    public var keepalive: Int

    public init(publicKey: String, endpoint: String, allowedIPs: String, handshake: Int, rx: UInt64, tx: UInt64, keepalive: Int) {
        self.publicKey = publicKey
        self.endpoint = endpoint
        self.allowedIPs = allowedIPs
        self.handshake = handshake
        self.rx = rx
        self.tx = tx
        self.keepalive = keepalive
    }
}

public struct WGDeviceStatus: Equatable, Codable, Sendable {
    public var interface: String
    public var publicKey: String
    public var listenPort: Int
    public var peers: [WGPeerStatus]

    public init(interface: String, publicKey: String, listenPort: Int, peers: [WGPeerStatus]) {
        self.interface = interface
        self.publicKey = publicKey
        self.listenPort = listenPort
        self.peers = peers
    }
}

public enum WGDump {
    public static func parse(_ text: String) -> [WGDeviceStatus] {
        var devices: [String: WGDeviceStatus] = [:]
        var order: [String] = []

        for raw in text.split(whereSeparator: \.isNewline) {
            let fields = raw.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
            guard let name = fields.first, !name.isEmpty else { continue }
            if fields.count >= 9 {
                let peer = WGPeerStatus(
                    publicKey: fields[1],
                    endpoint: fields[3],
                    allowedIPs: fields[4],
                    handshake: Int(fields[5]) ?? 0,
                    rx: UInt64(fields[6]) ?? 0,
                    tx: UInt64(fields[7]) ?? 0,
                    keepalive: Int(fields[8]) ?? 0
                )
                if devices[name] == nil {
                    devices[name] = WGDeviceStatus(interface: name, publicKey: "", listenPort: 0, peers: [])
                    order.append(name)
                }
                devices[name]?.peers.append(peer)
            } else if fields.count >= 5 {
                let device = WGDeviceStatus(
                    interface: name,
                    publicKey: fields[2],
                    listenPort: Int(fields[3]) ?? 0,
                    peers: devices[name]?.peers ?? []
                )
                if devices[name] == nil { order.append(name) }
                devices[name] = device
            }
        }
        return order.compactMap { devices[$0] }
    }
}
