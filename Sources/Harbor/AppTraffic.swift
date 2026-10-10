import CProc
import Foundation
import HarborCore

enum AppTraffic {
    static func scan(rules: [AppRule], skip: Set<String>) -> [UUID: [String]] {
        let active = rules.filter { $0.enabled && $0.learn && !$0.path.isEmpty }
        guard !active.isEmpty else { return [:] }

        var pids = [Int32](repeating: 0, count: 8192)
        let count = pids.withUnsafeMutableBufferPointer { buffer -> Int32 in
            guard let base = buffer.baseAddress else { return 0 }
            return procspy_list_pids(base, Int32(buffer.count))
        }

        var found: [UUID: Set<String>] = [:]
        let limit = Int(max(0, count))
        for index in 0..<limit {
            let pid = pids[index]
            guard pid > 0, let path = executablePath(pid) else { continue }
            let matched = active.filter { process(path, belongsTo: $0.path) }
            guard !matched.isEmpty else { continue }
            let ips = remoteIPs(pid).filter { ip in
                !skip.contains(ip) && IPAddress.parse(ip)?.isRoutableDestination == true
            }
            guard !ips.isEmpty else { continue }
            for rule in matched {
                found[rule.id, default: []].formUnion(ips)
            }
        }
        return found.mapValues { Array($0).sorted() }
    }

    static func process(_ path: String, belongsTo appPath: String) -> Bool {
        let normalized = appPath.hasSuffix("/") ? String(appPath.dropLast()) : appPath
        return path == normalized || path.hasPrefix(normalized + "/")
    }

    private static func executablePath(_ pid: Int32) -> String? {
        var buffer = [CChar](repeating: 0, count: 4096)
        let length = procspy_copy_path(pid, &buffer, Int32(buffer.count))
        guard length > 0 else { return nil }
        return String(cString: buffer)
    }

    private static func remoteIPs(_ pid: Int32) -> [String] {
        let capacity = 65_536
        var buffer = [CChar](repeating: 0, count: capacity)
        let length = procspy_copy_remotes(pid, &buffer, Int32(buffer.count))
        guard length > 0 else { return [] }
        let text = String(cString: buffer)
        return text.split(separator: "\n").map(String.init)
    }
}
