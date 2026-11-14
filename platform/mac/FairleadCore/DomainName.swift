import FairleadC
import Foundation

/// A single hostname for a direct route. Wildcards, URLs, and bare IPs are rejected.
public enum DomainName {
    public static func normalize(_ raw: String) -> String? {
        CBridge.optionalString(capacity: 512) { buffer, cap in
            domain_normalize(raw, buffer, cap)
        }
    }
}
