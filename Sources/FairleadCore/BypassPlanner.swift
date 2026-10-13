import Foundation

public struct BypassInput: Equatable {
    public var rules: [String]
    public var learned: [String]
    public var lan: [String]
    public var excludeLAN: Bool
    public var endpoints: [String]

    public init(rules: [String], learned: [String], lan: [String], excludeLAN: Bool, endpoints: [String]) {
        self.rules = rules
        self.learned = learned
        self.lan = lan
        self.excludeLAN = excludeLAN
        self.endpoints = endpoints
    }
}

public enum BypassPlanner {
    public static func routes(_ input: BypassInput) -> (routes: [String], warnings: [String]) {
        var warnings: [String] = []
        var cidrs: [CIDR] = []

        func take(_ raw: String, origin: String) {
            guard let cidr = CIDR.parse(raw) else {
                warnings.append(FairleadL10n.format("Skipped invalid address %@.", raw))
                return
            }
            if cidr.prefix == 0 {
                warnings.append(FairleadL10n.format("Address %@ is too broad and would disable the whole tunnel.", raw))
                return
            }
            let endpointIPs = input.endpoints.compactMap(IPAddress.parse)
            if endpointIPs.contains(where: { cidr.contains($0) }) {
                warnings.append(FairleadL10n.format("%@ overlaps the WireGuard server and stays in the tunnel.", origin))
                return
            }
            cidrs.append(cidr)
        }

        for rule in input.rules {
            take(rule, origin: rule)
        }
        for learned in input.learned {
            take(learned, origin: learned)
        }
        if input.excludeLAN {
            for lan in input.lan {
                take(lan, origin: FairleadL10n.format("Local network %@", lan))
            }
        }

        var seen = Set<String>()
        var routes: [String] = []
        for cidr in cidrs {
            let key = cidr.string
            if seen.insert(key).inserted {
                routes.append(key)
            }
        }
        routes.sort()
        return (routes, warnings)
    }
}
