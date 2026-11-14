import FairleadC
import Foundation

/// WireGuard keys. The shared C kernel does the Base64 and Curve25519 work.
public enum WGKeys {
    public struct Pair: Equatable {
        public var privateKey: String
        public var publicKey: String
    }

    public static func generate() -> Pair {
        var priv = [CChar](repeating: 0, count: 128)
        var pub = [CChar](repeating: 0, count: 128)
        _ = key_generate(&priv, 128, &pub, 128)
        return Pair(
            privateKey: priv.withUnsafeBufferPointer { String(cString: $0.baseAddress!) },
            publicKey: pub.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
        )
    }

    public static func publicKey(forPrivateKey privateKey: String) -> String? {
        let trimmed = privateKey.trimmingCharacters(in: .whitespacesAndNewlines)
        return CBridge.optionalString(capacity: 128) { buffer, cap in
            key_public(trimmed, buffer, cap)
        }
    }

    public static func isKey(_ text: String) -> Bool {
        key_is(text.trimmingCharacters(in: .whitespacesAndNewlines)) != 0
    }
}
