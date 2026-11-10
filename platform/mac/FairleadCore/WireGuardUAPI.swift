import FairleadC
import Foundation

public enum WireGuardUAPI {
    /// Builds the userspace configuration message. Endpoints are `ip:port` or `[ip]:port`.
    public static func setMessage(document: WGDocument, endpoints: [Int: String]) -> String? {
        let count = document.sections.count
        var owned: [UnsafeMutablePointer<CChar>] = []
        defer { owned.forEach { free($0) } }
        var bySection = [UnsafePointer<CChar>?](repeating: nil, count: max(count, 1))
        for (section, endpoint) in endpoints where section >= 0 && section < count && !endpoint.isEmpty {
            let copy = strdup(endpoint)!
            owned.append(copy)
            bySection[section] = UnsafePointer(copy)
        }
        return document.withC { doc in
            CBridge.optionalString(capacity: 65536) { buffer, cap in
                bySection.withUnsafeMutableBufferPointer { list in
                    fl_uapi_set(doc, list.baseAddress, buffer, cap)
                }
            }
        }
    }

    public static func socketEndpoint(ip: String, original: String) -> String? {
        CBridge.optionalString(capacity: 256) { buffer, cap in
            fl_socket_endpoint(ip, original.trimmingCharacters(in: .whitespacesAndNewlines), buffer, cap)
        }
    }

    public static func parseGet(_ text: String, interface: String) -> WGDeviceStatus {
        var device = fl_device_status_t()
        fl_uapi_parse_get(text, interface, &device)
        return WGDeviceStatus(device)
    }

    public static func hexKey(_ text: String) -> String? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        return CBridge.optionalString(capacity: 128) { buffer, cap in
            fl_key_hex(trimmed, buffer, cap)
        }
    }

    public static func base64Key(_ hex: String) -> String {
        CBridge.optionalString(capacity: 128) { buffer, cap in
            fl_hex_b64(hex, buffer, cap)
        } ?? ""
    }
}
