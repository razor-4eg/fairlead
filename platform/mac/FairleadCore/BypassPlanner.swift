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
    /// `routes` follow the policy. `direct` is the local network kept off the tunnel when only the list uses it.
    public static func routes(_ input: BypassInput) -> (routes: [String], direct: [String], warnings: [String]) {
        var warnings: [String] = []
        var cidrs: [CIDR] = []
        var directCIDRs: [CIDR] = []

        func take(_ raw: String, origin: String, into list: inout [CIDR]) {
            guard let cidr = CIDR.parse(raw) else {
                warnings.append(FairleadL10n.format("Skipped invalid address %@.", raw))
                return
            }
            if cidr.prefix == 0 {
                if input.policy == .selected {
                    warnings.append(FairleadL10n.format("Address %@ covers the whole internet. Use the other filter for that.", raw))
                } else {
                    warnings.append(FairleadL10n.format("Address %@ is too broad and would disable the whole tunnel.", raw))
                }
                return
            }
            let endpointIPs = input.endpoints.compactMap(IPAddress.parse)
            if endpointIPs.contains(where: { cidr.contains($0) }) {
                if input.policy == .selected {
                    warnings.append(FairleadL10n.format("%@ overlaps the WireGuard server and stays on the regular network.", origin))
                } else {
                    warnings.append(FairleadL10n.format("%@ overlaps the WireGuard server and stays in the tunnel.", origin))
                }
                return
            }
            list.append(cidr)
        }

        for rule in input.rules {
            take(rule, origin: rule, into: &cidrs)
        }
        for learned in input.learned {
            take(learned, origin: learned, into: &cidrs)
        }
        if input.excludeLAN {
            for lan in input.lan {
                let origin = FairleadL10n.format("Local network %@", lan)
                if input.policy == .selected {
                    take(lan, origin: origin, into: &directCIDRs)
                } else {
                    take(lan, origin: origin, into: &cidrs)
                }
            }
        }

        var routes = unique(cidrs)
        let direct = unique(directCIDRs)
        if input.policy == .selected {
            let keptOff = Set(direct)
            routes.removeAll { keptOff.contains($0) }
        }
        return (routes, direct, warnings)
    }

    private static func unique(_ cidrs: [CIDR]) -> [String] {
        var seen = Set<String>()
        var routes: [String] = []
        for cidr in cidrs {
            if seen.insert(cidr.string).inserted {
                routes.append(cidr.string)
            }
        }
        routes.sort()
        return routes
    }
}
