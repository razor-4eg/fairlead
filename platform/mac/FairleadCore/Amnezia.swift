import FairleadC
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

/// AmneziaWG parameters carried in a WireGuard config. Plain configs use the same amneziawg-go binary.
/// The key lists, checks, and UAPI encoding all come from the shared C kernel.
public enum Amnezia {
    public static func isAmnezia(_ document: WGDocument) -> Bool {
        document.withC { amnezia_is($0) != 0 }
    }

    public static func presentKeys(in document: WGDocument) -> [String] {
        document.withC { doc in
            var keys = [UnsafePointer<CChar>?](repeating: nil, count: 64)
            let count = Int(amnezia_present(doc, &keys, Int32(keys.count)))
            return keys.prefix(max(0, min(count, keys.count))).compactMap { $0.map { String(cString: $0) } }
        }
    }

    /// Device lines for amneziawg-go. Empty for a plain WireGuard config. Nil when a value cannot be encoded.
    public static func uapiLines(for document: WGDocument) -> [String]? {
        let text: String? = document.withC { doc in
            CBridge.optionalString(capacity: 16384) { buffer, cap in
                amnezia_uapi(doc, buffer, cap)
            }
        }
        guard let text else { return nil }
        return text.split(whereSeparator: \.isNewline).map(String.init)
    }

    /// True for a number, or a range such as `20-30`, no larger than `max`.
    public static func isUintRange(_ text: String, max: UInt64 = UInt64(UInt32.max)) -> Bool {
        amnezia_uint_range(text.trimmingCharacters(in: .whitespacesAndNewlines), max) != 0
    }

    public static func issues(in document: WGDocument) -> [ConfigIssue] {
        document.withC { doc in
            let (count, rows) = CBridge.rows(count: 16, width: 192) { raw in
                CBridge.rebind(raw) { amnezia_issues(doc, $0, 16) }
            }
            return rows.prefix(Int(count)).map { ConfigIssue(FairleadL10n.text($0)) }
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
        let configCap = max(256 * 1024, text.utf8.count + 1)
        let nameCap = 256
        return CBridge.onLargeStack {
            var config = [CChar](repeating: 0, count: configCap)
            var name = [CChar](repeating: 0, count: nameCap)
            let code = import_normalize(text, &config, Int32(configCap), &name, Int32(nameCap))
            switch Int(code) {
            case Int(IMPORT_WG):
                let body = config.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
                let title = name.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
                return .wireGuard(body, name: title.isEmpty ? nil : title)
            case Int(IMPORT_UNSUPPORTED):
                return .unsupported
            default:
                return .unreadable
            }
        }
    }

    /// A WireGuard config, or the same text when it is not an Amnezia share.
    public static func wireGuardText(_ text: String) -> String? {
        switch normalize(text) {
        case .wireGuard(let config, _): return config
        case .unsupported, .unreadable: return nil
        }
    }
}
