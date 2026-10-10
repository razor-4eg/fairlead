import Foundation

/// Looks up Harbor strings in the app bundle, or in the helper's localization bundle.
public enum HarborL10n {
    public static var bundle: Bundle = .main
    /// When set, overrides the process language. The helper uses the signed-in user's language.
    public static var languageOverride: String?

    public static func text(_ key: String) -> String {
        localizedBundle().localizedString(forKey: key, value: key, table: nil)
    }

    public static func format(_ key: String, _ args: CVarArg...) -> String {
        let template = text(key)
        return String(format: template, locale: activeLocale, arguments: args)
    }

    public static func addressCount(_ count: Int) -> String {
        let form: String
        switch resolvedLanguage() {
        case "ru":
            let mod100 = count % 100
            let mod10 = count % 10
            if (11...14).contains(mod100) {
                form = "many"
            } else if mod10 == 1 {
                form = "one"
            } else if (2...4).contains(mod10) {
                form = "few"
            } else {
                form = "many"
            }
        case "fr":
            form = count <= 1 ? "one" : "other"
        case "zh-Hans":
            form = "other"
        default:
            form = count == 1 ? "one" : "other"
        }
        return format("%d addresses.\(form)", count)
    }

    /// Shows a helper message in the app language. The helper may still be speaking the other one.
    public static func display(_ message: String) -> String {
        let catalogs = loadCatalogs()
        let parts = message.components(separatedBy: "\n")
        guard let head = parts.first, !head.isEmpty else { return message }
        if let key = match(head, catalogs: catalogs) ?? match(message, catalogs: catalogs) {
            let localized = text(key)
            if parts.count == 1 || key == match(message, catalogs: catalogs) {
                return localized
            }
            return ([localized] + parts.dropFirst()).joined(separator: "\n")
        }
        return message
    }

    public static var prefersRussian: Bool {
        resolvedLanguage() == "ru"
    }

    private static var catalogCache: [[String: [String]]]?

    private static func loadCatalogs() -> [[String: [String]]] {
        if let catalogCache { return catalogCache }
        var catalogs: [[String: [String]]] = []
        for code in Self.languages {
            guard let path = bundle.path(forResource: code, ofType: "lproj")
                ?? Bundle.main.path(forResource: code, ofType: "lproj"),
                  let loc = Bundle(path: path),
                  let file = loc.url(forResource: "Localizable", withExtension: "strings"),
                  let data = try? Data(contentsOf: file),
                  let table = try? PropertyListSerialization.propertyList(from: data, format: nil) as? [String: String] else {
                continue
            }
            var variants: [String: [String]] = [:]
            for (key, value) in table {
                variants[key] = [key, value]
            }
            catalogs.append(variants)
        }
        catalogCache = catalogs
        return catalogs
    }

    private static func match(_ message: String, catalogs: [[String: [String]]]) -> String? {
        let trimmed = message.trimmingCharacters(in: .whitespacesAndNewlines)
        for table in catalogs {
            for (key, variants) in table where variants.contains(trimmed) {
                return key
            }
        }
        for table in catalogs {
            for (key, variants) in table {
                for variant in variants where variant.contains("%") {
                    let prefix = String(variant.split(separator: "%", maxSplits: 1).first ?? "")
                        .trimmingCharacters(in: .whitespaces)
                    if prefix.count >= 12, trimmed.hasPrefix(prefix) {
                        return key
                    }
                }
            }
        }
        return nil
    }

    private static let languages = ["en", "ru", "de", "es", "fr", "pt", "zh-Hans"]

    private static var activeLocale: Locale {
        Locale(identifier: resolvedLanguage())
    }

    private static func resolvedLanguage() -> String {
        let raw = languageOverride
            ?? bundle.preferredLocalizations.first
            ?? Bundle.main.preferredLocalizations.first
            ?? Locale.preferredLanguages.first
            ?? "en"
        return canonical(raw)
    }

    private static func canonical(_ raw: String) -> String {
        let lower = raw.replacingOccurrences(of: "_", with: "-").lowercased()
        if lower.hasPrefix("zh") { return "zh-Hans" }
        let code = lower.split(separator: "-").first.map(String.init) ?? lower
        return languages.contains(code) ? code : "en"
    }

    private static func localizedBundle() -> Bundle {
        let code = resolvedLanguage()
        if let path = bundle.path(forResource: code, ofType: "lproj"),
           let localized = Bundle(path: path) {
            return localized
        }
        if let path = bundle.path(forResource: "en", ofType: "lproj"),
           let localized = Bundle(path: path) {
            return localized
        }
        return bundle
    }
}
