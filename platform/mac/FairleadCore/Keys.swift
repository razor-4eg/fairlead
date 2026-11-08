import CryptoKit
import Foundation

public enum WGKeys {
    public struct Pair: Equatable {
        public var privateKey: String
        public var publicKey: String
    }

    public static func generate() -> Pair {
        let key = Curve25519.KeyAgreement.PrivateKey()
        return Pair(
            privateKey: key.rawRepresentation.base64EncodedString(),
            publicKey: key.publicKey.rawRepresentation.base64EncodedString()
        )
    }

    public static func publicKey(forPrivateKey privateKey: String) -> String? {
        guard let data = Data(base64Encoded: privateKey.trimmingCharacters(in: .whitespacesAndNewlines)),
              data.count == 32,
              let key = try? Curve25519.KeyAgreement.PrivateKey(rawRepresentation: data) else {
            return nil
        }
        return key.publicKey.rawRepresentation.base64EncodedString()
    }

    public static func isKey(_ text: String) -> Bool {
        guard let data = Data(base64Encoded: text.trimmingCharacters(in: .whitespacesAndNewlines)) else {
            return false
        }
        return data.count == 32
    }
}
