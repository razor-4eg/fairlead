import FairleadC
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
        let cap = 16
        let devices = UnsafeMutablePointer<fl_device_status_t>.allocate(capacity: cap)
        devices.initialize(repeating: fl_device_status_t(), count: cap)
        defer {
            devices.deinitialize(count: cap)
            devices.deallocate()
        }
        let count = Int(fl_dump_parse(text, devices, Int32(cap)))
        return (0..<max(0, min(count, cap))).map { WGDeviceStatus(devices[$0]) }
    }
}

extension WGPeerStatus {
    init(_ peer: fl_peer_status_t) {
        self.init(
            publicKey: CBridge.text(peer.public_key),
            endpoint: CBridge.text(peer.endpoint),
            allowedIPs: CBridge.text(peer.allowed),
            handshake: Int(peer.handshake),
            rx: UInt64(peer.rx),
            tx: UInt64(peer.tx),
            keepalive: Int(peer.keepalive)
        )
    }
}

extension WGDeviceStatus {
    init(_ device: fl_device_status_t) {
        let peers = CBridge.elements(device.peers, as: fl_peer_status_t.self, count: Int(FL_PEER_MAX))
            .prefix(max(0, min(Int(device.peer_count), Int(FL_PEER_MAX))))
            .map(WGPeerStatus.init)
        self.init(
            interface: CBridge.text(device.interface_name),
            publicKey: CBridge.text(device.public_key),
            listenPort: Int(device.listen_port),
            peers: Array(peers)
        )
    }
}
