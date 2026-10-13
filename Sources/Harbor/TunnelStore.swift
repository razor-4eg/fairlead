import Foundation

enum AppSupport {
    /// `HARBOR_SUPPORT`, when set, is the folder that holds tunnels and settings.
    static var folder: URL {
        if let override = ProcessInfo.processInfo.environment["HARBOR_SUPPORT"], !override.isEmpty {
            return URL(fileURLWithPath: override, isDirectory: true)
        }
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Library/Application Support")
        return base.appendingPathComponent("Harbor", isDirectory: true)
    }
}

final class TunnelStore {
    private let url: URL

    init() {
        let directory = AppSupport.folder
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try? FileManager.default.setAttributes([.posixPermissions: 0o700], ofItemAtPath: directory.path)
        url = directory.appendingPathComponent("tunnels.json")
    }

    func load() -> [Tunnel] {
        guard let data = try? Data(contentsOf: url) else { return [] }
        return (try? JSONDecoder().decode([Tunnel].self, from: data)) ?? []
    }

    func save(_ tunnels: [Tunnel]) {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        guard let data = try? encoder.encode(tunnels) else { return }
        try? data.write(to: url, options: .atomic)
        try? FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }

    var folder: URL { url.deletingLastPathComponent() }
}
