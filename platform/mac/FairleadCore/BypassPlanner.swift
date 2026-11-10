import FairleadC
import Foundation

/// Which way the address list steers traffic.
public enum RoutePolicy: String, Codable, Equatable, Sendable {
    /// The tunnel keeps its routes. Listed addresses go out through the regular network.
    case exceptions
    /// Listed addresses go through the tunnel. Everything else stays on the regular network.
    case selected
}

public struct BypassInput: Equatable {
    public var rules: [String]
    public var learned: [String]
    public var lan: [String]
    public var excludeLAN: Bool
    public var endpoints: [String]
    public var policy: RoutePolicy

    public init(
        rules: [String],
        learned: [String],
        lan: [String],
        excludeLAN: Bool,
        endpoints: [String],
        policy: RoutePolicy = .exceptions
    ) {
        self.rules = rules
        self.learned = learned
        self.lan = lan
        self.excludeLAN = excludeLAN
        self.endpoints = endpoints
        self.policy = policy
    }
}

public enum BypassPlanner {
    private static let warningTemplates = [
        "Skipped invalid address %@.",
        "Address %@ covers the whole internet. Use the other filter for that.",
        "Address %@ is too broad and would disable the whole tunnel.",
        "%@ overlaps the WireGuard server and stays on the regular network.",
        "%@ overlaps the WireGuard server and stays in the tunnel.",
    ]

    /// `routes` follow the policy. `direct` is the local network kept off the tunnel when only the list uses it.
    public static func routes(_ input: BypassInput) -> (routes: [String], direct: [String], warnings: [String]) {
        var plan = fl_bypass_t()
        let selected: Int32 = input.policy == .selected ? 1 : 0
        let excludeLAN: Int32 = input.excludeLAN ? 1 : 0
        CBridge.withCStrings(input.rules) { rules, ruleCount in
            CBridge.withCStrings(input.learned) { learned, learnedCount in
                CBridge.withCStrings(input.lan) { lan, lanCount in
                    CBridge.withCStrings(input.endpoints) { endpoints, endpointCount in
                        fl_bypass_plan(
                            selected,
                            rules, ruleCount,
                            learned, learnedCount,
                            lan, lanCount,
                            excludeLAN,
                            endpoints, endpointCount,
                            &plan
                        )
                    }
                }
            }
        }
        return (
            strings(plan.routes, count: Int(plan.route_count)),
            strings(plan.direct, count: Int(plan.direct_count)),
            strings(plan.warnings, count: Int(plan.warning_count), width: 256).map {
                CBridge.localize($0, templates: warningTemplates)
            }
        )
    }

    private static func strings<T>(_ field: T, count: Int, width: Int = 64) -> [String] {
        var copy = field
        return withUnsafeBytes(of: &copy) { raw in
            (0..<max(0, count)).map { index in
                let start = index * width
                let bytes = raw[start..<(start + width)].prefix { $0 != 0 }
                return String(decoding: bytes, as: UTF8.self)
            }
        }
    }
}
