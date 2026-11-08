import CZlib
import Foundation

public enum QCompress {
    public static func compress(_ data: Data) -> Data? {
        guard !data.isEmpty, data.count <= 8_000_000 else { return nil }
        let capacity = data.count + data.count / 10 + 64
        var dest = Data(count: capacity)
        let written = dest.withUnsafeMutableBytes { dst -> Int in
            data.withUnsafeBytes { src -> Int in
                guard let d = dst.baseAddress?.assumingMemoryBound(to: UInt8.self),
                      let s = src.baseAddress?.assumingMemoryBound(to: UInt8.self) else { return 0 }
                return fairlead_qcompress(s, src.count, d, dst.count)
            }
        }
        guard written > 0 else { return nil }
        dest.count = written
        return dest
    }

    public static func uncompress(_ data: Data) -> Data? {
        guard data.count >= 6 else { return nil }
        let expected = data.prefix(4).reduce(0) { ($0 << 8) | Int($1) }
        guard expected > 0, expected <= 8_000_000 else { return nil }
        var dest = Data(count: expected)
        let written = dest.withUnsafeMutableBytes { dst -> Int in
            data.withUnsafeBytes { src -> Int in
                guard let d = dst.baseAddress?.assumingMemoryBound(to: UInt8.self),
                      let s = src.baseAddress?.assumingMemoryBound(to: UInt8.self) else { return 0 }
                return fairlead_quncompress(s, src.count, d, dst.count)
            }
        }
        guard written == expected else { return nil }
        return dest
    }
}

/// AmneziaWG parameters carried in a WireGuard config. Ordinary configs stay on wireguard-go.
public enum Amnezia {
    public static let countKeys = ["Jc", "Jmin", "Jmax"]
    public static let paddingKeys = ["S1", "S2", "S3", "S4"]
    public static let headerKeys = ["H1", "H2", "H3", "H4"]
    public static let signatureKeys = ["I1", "I2", "I3", "I4", "I5"]
    public static let rangeKeys = [
        "ContentPaddingAddition", "RekeyAfterTime", "RekeyTimeout", "RejectAfterTime",
        "KeepaliveTimeout", "MaxHandshakeAttempts",
    ]
    public static let boolKeys = ["RandomTrailers", "DisableCookies"]
    public static let orderedKeys =
        countKeys + paddingKeys + headerKeys + signatureKeys + ["HeaderProtectionKey"] + rangeKeys + boolKeys

    private static let uapiNames: [String: String] = [
        "Jc": "jc", "Jmin": "jmin", "Jmax": "jmax",
        "S1": "s1", "S2": "s2", "S3": "s3", "S4": "s4",
        "H1": "h1", "H2": "h2", "H3": "h3", "H4": "h4",
        "I1": "i1", "I2": "i2", "I3": "i3", "I4": "i4", "I5": "i5",
        "HeaderProtectionKey": "header_protection_key",
        "ContentPaddingAddition": "content_padding_addition",
        "RekeyAfterTime": "rekey_after_time",
        "RekeyTimeout": "rekey_timeout",
        "RejectAfterTime": "reject_after_time",
        "KeepaliveTimeout": "keepalive_timeout",
        "MaxHandshakeAttempts": "max_handshake_attempts",
        "RandomTrailers": "random_trailers",
        "DisableCookies": "disable_cookies",
    ]

    private static let headerDefaults: [String: UInt64] = ["H1": 1, "H2": 2, "H3": 3, "H4": 4]

    public static func isAmnezia(_ document: WGDocument) -> Bool {
        !presentKeys(in: document).isEmpty
    }

    public static func presentKeys(in document: WGDocument) -> [String] {
        guard let iface = document.interfaceIndex else { return [] }
        return orderedKeys.filter { !document.value(section: iface, key: $0).isEmpty }
    }

    /// Device lines for amneziawg-go. Empty for a plain WireGuard config. Nil when a value cannot be encoded.
    public static func uapiLines(for document: WGDocument) -> [String]? {
        guard isAmnezia(document), let iface = document.interfaceIndex else { return [] }
        var lines: [String] = []
        for key in orderedKeys {
            let raw = document.value(section: iface, key: key).trimmingCharacters(in: .whitespacesAndNewlines)
            if raw.isEmpty { continue }
            guard let name = uapiNames[key], let value = uapiValue(key: key, raw: raw) else { return nil }
            lines.append("\(name)=\(value)")
        }
        return lines
    }

    public static func uintRange(_ text: String, max: UInt64 = UInt64(UInt32.max)) -> ClosedRange<UInt64>? {
        let value = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let parts = value.split(separator: "-", omittingEmptySubsequences: false)
        guard parts.count == 1 || parts.count == 2 else { return nil }
        var numbers: [UInt64] = []
        for part in parts {
            guard !part.isEmpty, part.allSatisfy(\.isNumber), let number = UInt64(part), number <= max else { return nil }
            numbers.append(number)
        }
        if numbers.count == 2, numbers[0] > numbers[1] { return nil }
        return numbers[0]...(numbers.count == 2 ? numbers[1] : numbers[0])
    }

    public static func issues(in document: WGDocument) -> [ConfigIssue] {
        guard let iface = document.interfaceIndex, isAmnezia(document) else { return [] }
        var issues: [ConfigIssue] = []
        func add(_ message: String) {
            if !issues.contains(where: { $0.message == message }) {
                issues.append(ConfigIssue(message))
            }
        }

        var counts: [String: UInt64] = [:]
        for key in countKeys {
            let raw = document.value(section: iface, key: key)
            if raw.isEmpty { continue }
            if let number = UInt64(raw), number <= UInt64(UInt32.max), raw.allSatisfy(\.isNumber) {
                counts[key] = number
            } else {
                add(FairleadL10n.text("Jc, Jmin, and Jmax must be numbers, and Jmin cannot be greater than Jmax."))
            }
        }
        if let min = counts["Jmin"], let max = counts["Jmax"], min > max {
            add(FairleadL10n.text("Jc, Jmin, and Jmax must be numbers, and Jmin cannot be greater than Jmax."))
        }

        for key in paddingKeys {
            let raw = document.value(section: iface, key: key)
            if raw.isEmpty { continue }
            if UInt64(raw) == nil || !raw.allSatisfy(\.isNumber) || (UInt64(raw) ?? 0) > 65535 {
                add(FairleadL10n.text("S1, S2, S3, and S4 must be numbers from 0 to 65535."))
            }
        }

        var headers: [ClosedRange<UInt64>] = []
        var headersValid = true
        for key in headerKeys {
            let raw = document.value(section: iface, key: key)
            if raw.isEmpty {
                let fallback = headerDefaults[key] ?? 0
                headers.append(fallback...fallback)
                continue
            }
            if let span = uintRange(raw) {
                headers.append(span)
            } else {
                headersValid = false
                add(FairleadL10n.text("H1, H2, H3, and H4 must be a number or a range such as 123-456."))
            }
        }
        if headersValid {
            var overlap = false
            for i in headers.indices {
                for j in headers.indices where j > i {
                    if headers[i].overlaps(headers[j]) { overlap = true }
                }
            }
            if overlap {
                add(FairleadL10n.text("H1, H2, H3, and H4 must not overlap. Unset headers stay at 1, 2, 3, and 4."))
            }
        }

        for key in signatureKeys {
            let raw = document.value(section: iface, key: key)
            if raw.isEmpty { continue }
            if raw.contains("\n") || raw.contains("\r") || raw.utf8.count > 4096 {
                add(FairleadL10n.text("Signature packets must stay on one line."))
            }
        }

        let protection = document.value(section: iface, key: "HeaderProtectionKey")
        if !protection.isEmpty && WireGuardUAPI.hexKey(protection) == nil {
            add(FairleadL10n.text("HeaderProtectionKey must be a WireGuard key."))
        }
        if !protection.isEmpty {
            let pads = paddingKeys.compactMap { key -> UInt64? in
                let raw = document.value(section: iface, key: key)
                guard raw.allSatisfy(\.isNumber), let number = UInt64(raw) else { return nil }
                return number
            }
            if pads.count != paddingKeys.count || pads.contains(where: { $0 < 12 }) {
                add(FairleadL10n.text("Header protection needs S1, S2, S3, and S4 of at least 12."))
            }
        }

        for key in rangeKeys {
            let raw = document.value(section: iface, key: key)
            if raw.isEmpty { continue }
            if uintRange(raw) == nil {
                add(FairleadL10n.text("An AmneziaWG value must be a number or a range such as 20-30."))
            }
        }
        for key in boolKeys {
            let raw = document.value(section: iface, key: key)
            if raw.isEmpty { continue }
            if boolString(raw) == nil {
                add(FairleadL10n.text("RandomTrailers and DisableCookies must be true or false."))
            }
        }
        return issues
    }

    private static func uapiValue(key: String, raw: String) -> String? {
        if countKeys.contains(key) {
            guard raw.allSatisfy(\.isNumber), let number = UInt64(raw), number <= UInt64(UInt32.max) else { return nil }
            return String(number)
        }
        if paddingKeys.contains(key) {
            guard raw.allSatisfy(\.isNumber), let number = UInt64(raw), number <= 65535 else { return nil }
            return String(number)
        }
        if headerKeys.contains(key) || rangeKeys.contains(key) {
            guard uintRange(raw) != nil else { return nil }
            return raw.trimmingCharacters(in: .whitespacesAndNewlines)
        }
        if signatureKeys.contains(key) {
            guard !raw.contains("\n"), !raw.contains("\r"), raw.utf8.count <= 4096 else { return nil }
            return raw
        }
        if key == "HeaderProtectionKey" {
            return WireGuardUAPI.hexKey(raw)
        }
        if boolKeys.contains(key) {
            return boolString(raw)
        }
        return nil
    }

    private static func boolString(_ raw: String) -> String? {
        switch raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased() {
        case "true", "1", "yes", "on": return "true"
        case "false", "0", "no", "off": return "false"
        default: return nil
        }
    }
}

public enum ConfigImport {
    public enum Outcome: Equatable {
        case wireGuard(String, name: String?)
        case unsupported
        case unreadable
    }

    public static func normalize(_ text: String) -> Outcome {
        var trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.hasPrefix("\u{feff}") { trimmed.removeFirst() }
        let lowered = trimmed.lowercased()
        if lowered.hasPrefix("vpn://") {
            return decodeShare(String(trimmed.dropFirst(6)))
        }
        if trimmed.hasPrefix("{") {
            return decodeJSON(trimmed)
        }
        return .wireGuard(text, name: nil)
    }

    /// A WireGuard config, or the same text when it is not an Amnezia share.
    public static func wireGuardText(_ text: String) -> String? {
        switch normalize(text) {
        case .wireGuard(let config, _): return config
        case .unsupported, .unreadable: return nil
        }
    }

    private static func decodeShare(_ payload: String) -> Outcome {
        let compact = payload.unicodeScalars.filter { !CharacterSet.whitespacesAndNewlines.contains($0) }
        var encoded = String(String.UnicodeScalarView(compact))
        encoded = encoded.replacingOccurrences(of: "-", with: "+").replacingOccurrences(of: "_", with: "/")
        let pad = (4 - encoded.count % 4) % 4
        encoded += String(repeating: "=", count: pad)
        guard let data = Data(base64Encoded: encoded) else { return .unreadable }
        if let text = String(data: data, encoding: .utf8) {
            let body = text.trimmingCharacters(in: .whitespacesAndNewlines)
            if body.hasPrefix("{") { return decodeJSON(body) }
            if body.range(of: "[interface]", options: .caseInsensitive) != nil {
                return .wireGuard(body, name: nil)
            }
        }
        guard let inflated = QCompress.uncompress(data),
              let text = String(data: inflated, encoding: .utf8) else { return .unreadable }
        let body = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if body.hasPrefix("{") { return decodeJSON(body) }
        if body.range(of: "[interface]", options: .caseInsensitive) != nil {
            return .wireGuard(body, name: nil)
        }
        return .unreadable
    }

    private static func decodeJSON(_ text: String) -> Outcome {
        guard let data = text.data(using: .utf8),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return .unreadable
        }
        return config(from: object, dns: [], host: nil, port: nil, name: nil)
    }

    private static let supportedKinds: Set<String> = [
        "amnezia-awg", "amnezia-wireguard", "awg", "wireguard",
    ]

    private static func config(
        from object: [String: Any],
        dns: [String],
        host: String?,
        port: String?,
        name: String?
    ) -> Outcome {
        let servers = dnsServers(object).isEmpty ? dns : dnsServers(object)
        let nextHost = jsonString(object["hostName"]) ?? host
        let nextPort = jsonString(object["port"]) ?? port
        let nextName = jsonString(object["description"]) ?? jsonString(object["name"]) ?? name

        if object["containers"] == nil, looksLikeTunnel(object) {
            guard let rendered = render(object, dns: servers, host: nextHost, port: nextPort) else {
                return .unreadable
            }
            return .wireGuard(rendered, name: nextName)
        }

        guard let containers = object["containers"] as? [Any] else {
            if object["hostName"] != nil || object["defaultContainer"] != nil || object["description"] != nil {
                return .unsupported
            }
            return .unreadable
        }
        if let preferred = jsonString(object["defaultContainer"]), !supportedKinds.contains(preferred) {
            return .unsupported
        }
        let preferred = jsonString(object["defaultContainer"])
        let ranked: [(String, [String: Any])] = containers.compactMap { item in
            guard let container = item as? [String: Any] else { return nil }
            return (jsonString(container["container"]) ?? "", container)
        }.sorted { lhs, rhs in
            rank(lhs.0, preferred: preferred) < rank(rhs.0, preferred: preferred)
        }
        var sawKind = false
        for (kind, container) in ranked where supportedKinds.contains(kind) {
            sawKind = true
            let protocolKey = (kind == "amnezia-wireguard" || kind == "wireguard") ? "wireguard" : "awg"
            guard let box = container[protocolKey] as? [String: Any] else { continue }
            let boxPort = jsonString(box["port"]) ?? nextPort
            guard let last = box["last_config"] else { continue }
            switch unwrap(last) {
            case .object(let proto):
                let mergedHost = jsonString(proto["hostName"]) ?? nextHost
                let mergedPort = jsonString(proto["port"]) ?? boxPort
                if let rendered = render(proto, dns: servers, host: mergedHost, port: mergedPort) {
                    return .wireGuard(rendered, name: nextName)
                }
            case .conf(let text):
                if let rendered = render(conf: text, proto: [:], dns: servers, host: nextHost, port: boxPort) {
                    return .wireGuard(rendered, name: nextName)
                }
            case .bad:
                return .unreadable
            }
        }
        return sawKind ? .unreadable : .unsupported
    }

    private static func rank(_ kind: String, preferred: String?) -> Int {
        if let preferred, kind == preferred, supportedKinds.contains(kind) { return 0 }
        switch kind {
        case "amnezia-awg", "awg": return 1
        case "amnezia-wireguard", "wireguard": return 2
        default: return 99
        }
    }

    private enum LastConfig {
        case object([String: Any])
        case conf(String)
        case bad
    }

    private static func unwrap(_ value: Any) -> LastConfig {
        if let object = value as? [String: Any] { return .object(object) }
        guard let text = jsonString(value) else { return .bad }
        if text.hasPrefix("{") {
            guard let data = text.data(using: .utf8),
                  let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return .bad }
            return .object(object)
        }
        if text.range(of: "[interface]", options: .caseInsensitive) != nil { return .conf(text) }
        return .bad
    }

    private static func looksLikeTunnel(_ object: [String: Any]) -> Bool {
        if jsonString(object["client_priv_key"]) != nil || jsonString(object["PrivateKey"]) != nil { return true }
        if let config = jsonString(object["config"]) ?? jsonString(object["nativeConfig"]),
           config.range(of: "[interface]", options: .caseInsensitive) != nil { return true }
        return false
    }

    private static func render(_ object: [String: Any], dns: [String], host: String?, port: String?) -> String? {
        let embedded = jsonString(object["config"]) ?? jsonString(object["nativeConfig"])
        if let embedded, embedded.range(of: "[interface]", options: .caseInsensitive) != nil {
            return render(conf: embedded, proto: object, dns: dns, host: host, port: port)
        }
        return render(conf: nil, proto: object, dns: dns, host: host, port: port)
    }

    private static func render(conf: String?, proto: [String: Any], dns: [String], host: String?, port: String?) -> String? {
        var doc = conf.map { WGDocument.parse($0) } ?? WGDocument()
        let iface = doc.ensureInterface()
        func fill(_ key: String, _ value: String?, section: Int) {
            guard let value, !value.isEmpty, doc.value(section: section, key: key).isEmpty else { return }
            doc.setValue(section: section, key: key, value: value)
        }
        fill("PrivateKey", jsonString(proto["client_priv_key"]) ?? jsonString(proto["PrivateKey"]), section: iface)
        if doc.value(section: iface, key: "Address").isEmpty,
           let address = jsonString(proto["client_ip"]) ?? jsonString(proto["Address"]) {
            doc.setValue(section: iface, key: "Address", value: normalizeAddresses(address))
        }
        if doc.interfaceDNS.isEmpty, !dns.isEmpty {
            doc.setValue(section: iface, key: "DNS", value: dns.joined(separator: ", "))
        }
        fill("MTU", jsonString(proto["mtu"]) ?? jsonString(proto["MTU"]), section: iface)
        for key in Amnezia.orderedKeys {
            fill(key, jsonString(proto[key]), section: iface)
        }
        if doc.peerIndexes.isEmpty { _ = doc.addPeer() }
        guard let peer = doc.peerIndexes.first else { return nil }
        fill("PublicKey", jsonString(proto["server_pub_key"]) ?? jsonString(proto["PublicKey"]), section: peer)
        fill("PresharedKey", jsonString(proto["psk_key"]) ?? jsonString(proto["PresharedKey"]), section: peer)
        if doc.peerAllowedIPs(peer).isEmpty {
            let allowed = jsonList(proto["allowed_ips"])
            if !allowed.isEmpty {
                doc.setValue(section: peer, key: "AllowedIPs", value: allowed.joined(separator: ", "))
            }
        }
        fill(
            "PersistentKeepalive",
            jsonString(proto["persistent_keep_alive"]) ?? jsonString(proto["PersistentKeepalive"]),
            section: peer
        )
        let endpointHost = jsonString(proto["hostName"]) ?? host
        let endpointPort = jsonString(proto["port"]) ?? port
        if doc.peerEndpoint(peer).isEmpty, let endpointHost, let endpointPort {
            doc.setValue(section: peer, key: "Endpoint", value: endpoint(host: endpointHost, port: endpointPort))
        }
        guard !doc.interfacePrivateKey.isEmpty, !doc.peerPublicKey(peer).isEmpty else { return nil }
        return doc.render()
    }

    private static func dnsServers(_ object: [String: Any]) -> [String] {
        var list: [String] = []
        list.append(contentsOf: jsonList(object["dns1"]))
        list.append(contentsOf: jsonList(object["dns2"]))
        if let dns = jsonString(object["DNS"]) {
            list.append(contentsOf: dns.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) })
        }
        return list.filter { !$0.isEmpty }
    }

    private static func jsonList(_ value: Any?) -> [String] {
        if let items = value as? [Any] {
            return items.compactMap { jsonString($0) }
        }
        guard let text = jsonString(value) else { return [] }
        return text.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
    }

    private static func jsonString(_ value: Any?) -> String? {
        guard let value, !(value is NSNull) else { return nil }
        if let text = value as? String {
            let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
            return trimmed.isEmpty ? nil : trimmed
        }
        if let flag = value as? Bool {
            return flag ? "true" : "false"
        }
        if let number = value as? NSNumber {
            if CFGetTypeID(number) == CFBooleanGetTypeID() {
                return number.boolValue ? "true" : "false"
            }
            return number.stringValue
        }
        return nil
    }

    private static func normalizeAddresses(_ text: String) -> String {
        text.split(separator: ",").map { item in
            let address = item.trimmingCharacters(in: .whitespaces)
            if address.isEmpty || address.contains("/") { return address }
            return address.contains(":") ? address + "/128" : address + "/32"
        }.filter { !$0.isEmpty }.joined(separator: ", ")
    }

    private static func endpoint(host: String, port: String) -> String {
        let trimmedHost = host.trimmingCharacters(in: .whitespaces)
        let trimmedPort = port.trimmingCharacters(in: .whitespaces)
        if trimmedHost.contains(":"), !trimmedHost.hasPrefix("[") {
            return "[\(trimmedHost)]:\(trimmedPort)"
        }
        return "\(trimmedHost):\(trimmedPort)"
    }
}
