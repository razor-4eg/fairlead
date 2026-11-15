//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// MihomoConfig.swift -- The mihomo config.yaml for a WireGuard document and the route rules.
//
//===============================================================

import FairleadC
import Foundation

public enum MihomoConfig {
    public static let controller = "127.0.0.1:18984"
    public static let controllerURL = "http://127.0.0.1:18984"
    public static let proxyName = "fairlead"

/*
===============
cidr
A bare address becomes /32 or /128. mihomo's IP-CIDR rule needs the prefix.
===============
*/
    public static func cidr(_ text: String) -> String? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, trimmed.utf8.count < 56 else { return nil }
        let full = trimmed.contains("/") ? trimmed : trimmed + (trimmed.contains(":") ? "/128" : "/32")
        return CIDR.parse(full) == nil ? nil : full
    }

/*
===============
build
The shared C builder does the work. Returns the YAML, or nil with a message.
===============
*/
    public static func build(
        wg: String,
        selected: Bool,
        excludeLAN: Bool,
        cidrs: [String],
        domains: [String],
        apps: [String],
        secret: String
    ) -> (yaml: String?, error: String) {
        let ips = cidrs.compactMap(cidr)
        return CBridge.onLargeStack {
            var err = [CChar](repeating: 0, count: 256)
            let yaml: String? = CBridge.withCStrings(ips) { c, cn in
                CBridge.withCStrings(domains) { d, dn in
                    CBridge.withCStrings(apps) { a, an in
                        var rules = mihomo_rules_t(cidrs: c, cidr_count: cn, domains: d, domain_count: dn, apps: a, app_count: an)
                        return CBridge.take(mihomo_config(wg, selected ? 1 : 0, excludeLAN ? 1 : 0, &rules, controller, secret, &err, 256))
                    }
                }
            }

            if yaml != nil { return (yaml, "") }
            let message = String(cString: err)
            return (nil, message.isEmpty ? FairleadL10n.text("Could not build the mihomo config.") : CBridge.localize(message, templates: []))
        }
    }
}
