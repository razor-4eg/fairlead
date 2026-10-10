import CryptoKit
import Foundation
import HarborCore

enum HelperInstaller {
    static let stampPath = "/usr/local/libexec/harbor-helper.stamp"

    /// The helper left running by launchd can be older than the app. Codesign changes the installed bytes, so the installer records a hash of the copy inside the app.
    static func needsUpdate() -> Bool {
        guard let expected = bundledFingerprint() else { return false }
        let installed = (try? String(contentsOfFile: stampPath, encoding: .utf8))?
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return installed != expected
    }

    static func bundledFingerprint() -> String? {
        guard let url = binaryURL() else { return nil }
        guard let data = try? Data(contentsOf: url) else { return nil }
        return SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    static func binaryURL() -> URL? {
        let candidates = [
            Bundle.main.executableURL?.deletingLastPathComponent().appendingPathComponent("HarborHelper"),
            URL(fileURLWithPath: CommandLine.arguments[0])
                .resolvingSymlinksInPath()
                .deletingLastPathComponent()
                .appendingPathComponent("HarborHelper"),
        ]
        return candidates.compactMap { $0 }.first { FileManager.default.isExecutableFile(atPath: $0.path) }
    }

    static func install() async -> String? {
        guard let binary = binaryURL() else {
            return HarborL10n.text("HarborHelper is missing next to Harbor. Build the app with scripts/build-app.sh.")
        }
        let script = """
        #!/bin/bash
        set -euo pipefail
        SRC=\(shellQuote(binary.path))
        USER_NAME=\(shellQuote(NSUserName()))
        PLIST=/Library/LaunchDaemons/com.harbor.helper.plist
        LABEL=com.harbor.helper
        LOG=/var/log/harbor-helper.log
        mkdir -p /usr/local/libexec /var/run/harbor
        # The password dialog's process is killed by taskgated if it execs this
        # ad-hoc binary (SIGKILL, "Code Signature Invalid"). launchd starts it
        # outside that session, which is allowed.
        launchctl bootout system/"$LABEL" >/dev/null 2>&1 || true
        if [[ -f /var/run/harbor/helper.pid ]]; then
          old="$(cat /var/run/harbor/helper.pid || true)"
          if [[ -n "$old" ]]; then
            kill "$old" 2>/dev/null || true
          fi
        fi
        killall harbor-helper 2>/dev/null || true
        sleep 0.3
        killall -9 harbor-helper 2>/dev/null || true
        cp "$SRC" /usr/local/libexec/harbor-helper
        chmod 755 /usr/local/libexec/harbor-helper
        codesign --force --sign - --identifier "$LABEL" /usr/local/libexec/harbor-helper
        shasum -a 256 "$SRC" | awk '{print $1}' > /usr/local/libexec/harbor-helper.stamp
        chmod 644 /usr/local/libexec/harbor-helper.stamp
        RES="$(cd "$(dirname "$SRC")/../Resources" && pwd)"
        LOC=/usr/local/libexec/HarborLocalizations.bundle
        rm -rf "$LOC"
        mkdir -p "$LOC/Contents/Resources"
        for loc in "$RES"/*.lproj; do
          if [[ -d "$loc" ]]; then cp -R "$loc" "$LOC/Contents/Resources/"; fi
        done
        cat > "$LOC/Contents/Info.plist" <<'PLIST'
        <?xml version="1.0" encoding="UTF-8"?>
        <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
        <plist version="1.0"><dict>
          <key>CFBundlePackageType</key><string>BNDL</string>
          <key>CFBundleIdentifier</key><string>com.harbor.localizations</string>
          <key>CFBundleDevelopmentRegion</key><string>en</string>
          <key>CFBundleName</key><string>HarborLocalizations</string>
        </dict></plist>
        PLIST
        chown -R root:wheel "$LOC"
        chmod 755 "$LOC" "$LOC/Contents" "$LOC/Contents/Resources"
        rm -f /var/run/harbor/harbor.sock
        ESCAPED="$USER_NAME"
        ESCAPED="${ESCAPED//&/&amp;}"
        ESCAPED="${ESCAPED//</&lt;}"
        ESCAPED="${ESCAPED//>/&gt;}"
        cat > "$PLIST" <<EOF
        <?xml version="1.0" encoding="UTF-8"?>
        <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
        <plist version="1.0">
        <dict>
          <key>Label</key>
          <string>$LABEL</string>
          <key>ProgramArguments</key>
          <array>
            <string>/usr/local/libexec/harbor-helper</string>
            <string>--user</string>
            <string>$ESCAPED</string>
          </array>
          <key>RunAtLoad</key>
          <true/>
          <key>StandardOutPath</key>
          <string>$LOG</string>
          <key>StandardErrorPath</key>
          <string>$LOG</string>
        </dict>
        </plist>
        EOF
        chown root:wheel "$PLIST"
        chmod 644 "$PLIST"
        echo "---- $(date) ----" >> "$LOG"
        launchctl enable system/"$LABEL" >/dev/null 2>&1 || true
        if ! launchctl bootstrap system "$PLIST" >>"$LOG" 2>&1; then
          echo \(shellQuote(HarborL10n.text("launchd could not start the helper."))) >&2
          tail -n 20 "$LOG" >&2 || true
          exit 1
        fi
        for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40; do
          if [[ -S /var/run/harbor/harbor.sock ]]; then
            exit 0
          fi
          sleep 0.15
        done
        echo \(shellQuote(HarborL10n.text("The helper did not open a socket. Last log lines:"))) >&2
        tail -n 20 "$LOG" >&2 || true
        exit 1
        """
        let file = FileManager.default.temporaryDirectory.appendingPathComponent("harbor-install-\(UUID().uuidString).sh")
        do {
            try script.write(to: file, atomically: true, encoding: .utf8)
            try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: file.path)
        } catch {
            return HarborL10n.text("Could not prepare the installation.")
        }
        defer { try? FileManager.default.removeItem(at: file) }

        let escaped = file.path.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        let source = "do shell script \"/bin/bash \\\"\(escaped)\\\"\" with administrator privileges"
        return await runOSA(source)
    }

    static func brewInstall(brew: String) async -> String {
        await Task.detached(priority: .utility) {
            let process = Process()
            process.executableURL = URL(fileURLWithPath: brew)
            process.arguments = ["install", "wireguard-tools"]
            let pipe = Pipe()
            process.standardOutput = pipe
            process.standardError = pipe
            do {
                try process.run()
            } catch {
                return error.localizedDescription
            }
            process.waitUntilExit()
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            let text = String(data: data, encoding: .utf8) ?? ""
            if process.terminationStatus == 0 {
                return text.isEmpty ? HarborL10n.text("wireguard-tools installed.") : text
            }
            return text.isEmpty ? HarborL10n.format("brew exited with code %d.", Int(process.terminationStatus)) : text
        }.value
    }

    private static func runOSA(_ source: String) async -> String? {
        await Task.detached(priority: .userInitiated) {
            let process = Process()
            process.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
            process.arguments = ["-e", source]
            let pipe = Pipe()
            process.standardError = pipe
            process.standardOutput = pipe
            do {
                try process.run()
            } catch {
                return error.localizedDescription
            }
            process.waitUntilExit()
            if process.terminationStatus == 0 { return nil }
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            let text = String(data: data, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines)
            if text?.isEmpty != false {
                return HarborL10n.text("The system did not confirm that the helper started.")
            }
            return text
        }.value
    }

    private static func shellQuote(_ value: String) -> String {
        "'" + value.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }
}
