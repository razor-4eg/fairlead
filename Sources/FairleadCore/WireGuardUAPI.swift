import Foundation

public enum WireGuardUAPI {
    /// Builds the userspace configuration message. Endpoints are `ip:port` or `[ip]:port`.
    public static func setMessage(document: WGDocument, endpoints: [Int: String]) -> String? {
        guard let iface = document.interfaceIndex else { return nil }
        guard let privateKey = hexKey(document.value(section: iface, key: "PrivateKey")) else { return nil }
        var lines = ["set=1", "private_key=\(privateKey)"]
        let port = document.value(section: iface, key: "ListenPort")
        if let value = Int(port), (0...65535).contains(value) {
            lines.append("listen_port=\(value)")
        }
        lines.append("replace_peers=true")
        for peer in document.peerIndexes {
            guard let publicKey = hexKey(document.peerPublicKey(peer)) else { return nil }
            lines.append("public_key=\(publicKey)")
            let preshared = document.peerPreshared(peer)
            if !preshared.isEmpty {
                guard let hex = hexKey(preshared) else { return nil }
                lines.append("preshared_key=\(hex)")
            }
            if let endpoint = endpoints[peer], !endpoint.isEmpty {
                lines.append("endpoint=\(endpoint)")
            }
            let keep = document.peerKeepalive(peer)
            if let value = Int(keep), (0...65535).contains(value) {
                lines.append("persistent_keepalive_interval=\(value)")
            }
            lines.append("replace_allowed_ips=true")
            for raw in document.peerAllowedIPs(peer).split(separator: ",") {
                guard let cidr = CIDR.parse(String(raw)) else { return nil }
                lines.append("allowed_ip=\(cidr.string)")
            }
        }
        return lines.joined(separator: "\n") + "\n\n"
    }

    public static func socketEndpoint(ip: String, original: String) -> String? {
        let value = original.trimmingCharacters(in: .whitespacesAndNewlines)
        let port: String
        if value.hasPrefix("[") {
            guard let end = value.lastIndex(of: "]") else { return nil }
            let after = value.index(after: end)
            guard after < value.endIndex, value[after] == ":" else { return nil }
            port = String(value[value.index(after: after)...])
        } else {
            guard let colon = value.lastIndex(of: ":") else { return nil }
            port = String(value[value.index(after: colon)...])
        }
        guard let number = Int(port), (1...65535).contains(number) else { return nil }
        if ip.contains(":") { return "[\(ip)]:\(port)" }
        return "\(ip):\(port)"
    }

    public static func parseGet(_ text: String, interface: String) -> WGDeviceStatus {
        var deviceKey = ""
        var listenPort = 0
        var pendingKey: String?
        var peer = WGPeerStatus(publicKey: "", endpoint: "", allowedIPs: "", handshake: 0, rx: 0, tx: 0, keepalive: 0)
        var inPeer = false
        var peers: [WGPeerStatus] = []
        var allowed: [String] = []

        func finishPeer() {
            guard inPeer else { return }
            peer.allowedIPs = allowed.joined(separator: ", ")
            peers.append(peer)
            inPeer = false
            allowed = []
        }

        for raw in text.split(whereSeparator: \.isNewline) {
            let line = String(raw)
            guard let eq = line.firstIndex(of: "=") else { continue }
            let key = String(line[..<eq])
            let value = String(line[line.index(after: eq)...])
            switch key {
            case "listen_port":
                listenPort = Int(value) ?? listenPort
            case "public_key":
                if inPeer { finishPeer() }
                if pendingKey == nil {
                    pendingKey = value
                } else if deviceKey.isEmpty {
                    deviceKey = base64Key(pendingKey ?? "")
                    pendingKey = value
                } else {
                    pendingKey = value
                }
            case "preshared_key":
                peer = WGPeerStatus(
                    publicKey: base64Key(pendingKey ?? ""),
                    endpoint: "",
                    allowedIPs: "",
                    handshake: 0,
                    rx: 0,
                    tx: 0,
                    keepalive: 0
                )
                pendingKey = nil
                inPeer = true
                allowed = []
            case "endpoint":
                peer.endpoint = value
            case "last_handshake_time_sec":
                peer.handshake = Int(value) ?? 0
            case "rx_bytes":
                peer.rx = UInt64(value) ?? 0
            case "tx_bytes":
                peer.tx = UInt64(value) ?? 0
            case "persistent_keepalive_interval":
                peer.keepalive = Int(value) ?? 0
            case "allowed_ip":
                if !value.isEmpty { allowed.append(value) }
            default:
                break
            }
        }
        finishPeer()
        if deviceKey.isEmpty, let pendingKey {
            deviceKey = base64Key(pendingKey)
        }
        return WGDeviceStatus(interface: interface, publicKey: deviceKey, listenPort: listenPort, peers: peers)
    }

    public static func hexKey(_ text: String) -> String? {
        guard let data = Data(base64Encoded: text.trimmingCharacters(in: .whitespacesAndNewlines)), data.count == 32 else {
            return nil
        }
        return data.map { String(format: "%02x", $0) }.joined()
    }

    public static func base64Key(_ hex: String) -> String {
        var bytes: [UInt8] = []
        var index = hex.startIndex
        while index < hex.endIndex {
            guard let next = hex.index(index, offsetBy: 2, limitedBy: hex.endIndex), next > index else { break }
            guard let byte = UInt8(hex[index..<next], radix: 16) else { break }
            bytes.append(byte)
            index = next
        }
        return Data(bytes).base64EncodedString()
    }
}
