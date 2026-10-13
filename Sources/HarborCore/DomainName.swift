import Foundation

/// A single hostname for a direct route. Wildcards, URLs, and bare IPs are rejected.
public enum DomainName {
    public static func normalize(_ raw: String) -> String? {
        var text = raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if text.hasSuffix(".") {
            text.removeLast()
        }
        if text.isEmpty || text.contains("*") || text.contains("/") || text.contains("\\")
            || text.contains(":") || text.contains(" ") || text.contains("@") {
            return nil
        }
        if text.unicodeScalars.contains(where: { $0.value > 127 }) {
            var parts = URLComponents()
            parts.scheme = "http"
            parts.host = text
            guard let host = parts.url?.host?.lowercased(), !host.isEmpty else { return nil }
            text = host.hasSuffix(".") ? String(host.dropLast()) : host
        }
        if IPAddress.parse(text) != nil { return nil }
        let labels = text.split(separator: ".", omittingEmptySubsequences: false)
        guard labels.count >= 2, text.count <= 253 else { return nil }
        let allowed = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyz0123456789-")
        for label in labels {
            if label.isEmpty || label.count > 63 { return nil }
            if label.hasPrefix("-") || label.hasSuffix("-") { return nil }
            if label.unicodeScalars.contains(where: { !allowed.contains($0) }) { return nil }
        }
        return text
    }
}
