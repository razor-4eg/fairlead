import Foundation
import FairleadCore

enum SystemDNS {
    static func current() -> ScutilDNS.Snapshot {
        let pipe = Pipe()
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/sbin/scutil")
        process.arguments = ["--dns"]
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
        } catch {
            return ScutilDNS.Snapshot(servers: [], interface: nil)
        }
        process.waitUntilExit()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        guard let text = String(data: data, encoding: .utf8) else {
            return ScutilDNS.Snapshot(servers: [], interface: nil)
        }
        return ScutilDNS.parse(text)
    }
}
