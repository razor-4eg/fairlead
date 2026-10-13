import Foundation

enum AppSupport {
    /// `FAIRLEAD_SUPPORT`, when set, is the folder that holds tunnels and settings.
    static var folder: URL {
        if let override = ProcessInfo.processInfo.environment["FAIRLEAD_SUPPORT"], !override.isEmpty {
            return URL(fileURLWithPath: override, isDirectory: true)
        }
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Library/Application Support")
        let folder = base.appendingPathComponent("Fairlead", isDirectory: true)
        adoptPreviousFolder(folder, base: base)
        return folder
    }

    /// Copies tunnels and settings saved under the previous name, once.
    private static func adoptPreviousFolder(_ folder: URL, base: URL) {
        let previous = base.appendingPathComponent("Harbor", isDirectory: true)
        let files = FileManager.default
        for name in ["tunnels.json", "settings.json"] {
            let destination = folder.appendingPathComponent(name)
            let source = previous.appendingPathComponent(name)
            if files.fileExists(atPath: destination.path) || !files.fileExists(atPath: source.path) { continue }
            try? files.createDirectory(at: folder, withIntermediateDirectories: true)
            try? files.copyItem(at: source, to: destination)
        }
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
