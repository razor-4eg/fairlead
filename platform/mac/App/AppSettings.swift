//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// AppSettings.swift -- Settings saved for this user, and the login item.
//
//===============================================================

import Foundation
import ServiceManagement

struct AppSettings: Equatable {
    /// Sentinel stored in `defaultTunnel` for “reconnect whatever was up last”.
    static let lastActiveKey = "last"

    var launchAtLogin: Bool
    /// First launch opens in the menu bar only (no main window).
    var startHidden: Bool
    /// `nil` / empty = none; `last` = last active; otherwise a tunnel UUID string.
    var defaultTunnel: String?
    /// Updated on each successful connect. Used when `defaultTunnel == last`.
    var lastActiveTunnelID: UUID?

    static let empty = AppSettings(
        launchAtLogin: false,
        startHidden: false,
        defaultTunnel: nil,
        lastActiveTunnelID: nil
    )

    var defaultTunnelSelection: String {
        let key = defaultTunnel?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return key
    }
}

extension AppSettings: Codable {
    enum CodingKeys: String, CodingKey {
        case launchAtLogin
        case startHidden
        case defaultTunnel
        case lastActiveTunnelID
        case defaultTunnelID
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        launchAtLogin = try container.decodeIfPresent(Bool.self, forKey: .launchAtLogin) ?? false
        startHidden = try container.decodeIfPresent(Bool.self, forKey: .startHidden) ?? false
        lastActiveTunnelID = try container.decodeIfPresent(UUID.self, forKey: .lastActiveTunnelID)
        if let key = try container.decodeIfPresent(String.self, forKey: .defaultTunnel) {
            let trimmed = key.trimmingCharacters(in: .whitespacesAndNewlines)
            defaultTunnel = trimmed.isEmpty ? nil : trimmed
        } else if let id = try container.decodeIfPresent(UUID.self, forKey: .defaultTunnelID) {
            defaultTunnel = id.uuidString
        } else {
            defaultTunnel = nil
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(launchAtLogin, forKey: .launchAtLogin)
        try container.encode(startHidden, forKey: .startHidden)
        try container.encodeIfPresent(defaultTunnel, forKey: .defaultTunnel)
        try container.encodeIfPresent(lastActiveTunnelID, forKey: .lastActiveTunnelID)
    }
}

final class SettingsStore {
    private let url: URL

/*
===============
init
===============
*/
    init() {
        let directory = AppSupport.folder
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        url = directory.appendingPathComponent("settings.json")
    }

/*
===============
load
===============
*/
    func load() -> AppSettings {
        guard let data = try? Data(contentsOf: url),
              let settings = try? JSONDecoder().decode(AppSettings.self, from: data) else {
            return .empty
        }

        return settings
    }

/*
===============
save
===============
*/
    func save(_ settings: AppSettings) {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        guard let data = try? encoder.encode(settings) else { return }
        try? data.write(to: url, options: .atomic)
        try? FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }
}

enum LoginItem {
    static var status: SMAppService.Status {
        SMAppService.mainApp.status
    }

    static var isOn: Bool {
        status == .enabled || status == .requiresApproval
    }

/*
===============
setEnabled
===============
*/
    static func setEnabled(_ enabled: Bool) throws {
        let service = SMAppService.mainApp
        if enabled {
            if service.status != .enabled {
                try service.register()
            }
        } else if service.status != .notRegistered {
            try service.unregister()
        }
    }
}
