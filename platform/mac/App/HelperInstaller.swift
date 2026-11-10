//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// HelperInstaller.swift -- Install and replace the privileged helper.
//
//===============================================================

import CryptoKit
import Foundation
import FairleadCore

enum HelperInstaller {
    static let stampPath = "/usr/local/libexec/fairlead-helper.stamp"

/*
===============
needsUpdate
The helper left running by launchd can be older than the app. Ad-hoc signing rewrites the signature, so the comparison ignores it and does not treat a re-sign as a new helper.
===============
*/
    static func needsUpdate() -> Bool {
        guard let expected = bundledFingerprint() else { return false }
        guard let installed = installedFingerprint() else { return true }
        return installed != expected
    }

/*
===============
bundledFingerprint
===============
*/
    static func bundledFingerprint() -> String? {
        guard let url = binaryURL(), let data = try? Data(contentsOf: url) else { return nil }
        return contentFingerprint(data)
    }

/*
===============
installedFingerprint
===============
*/
    private static func installedFingerprint() -> String? {
        guard let data = try? Data(contentsOf: URL(fileURLWithPath: FairleadPaths.helperBinary)) else { return nil }
        return contentFingerprint(data)
    }

/*
===============
contentFingerprint
Hash of the Mach-O with the code signature and its size fields removed. Re-signing then keeps the same fingerprint.
===============
*/
    private static func contentFingerprint(_ data: Data) -> String {
        var bytes = [UInt8](data)
        guard bytes.count > 32, readU32(bytes, 0) == 0xFEEDFACF else { return sha256Hex(data) }
        let count = Int(readU32(bytes, 16))
        var offset = 32
        var signature: (start: Int, length: Int)?
        for _ in 0..<count {
            guard offset + 8 <= bytes.count else { break }
            let command = readU32(bytes, offset)
            let size = Int(readU32(bytes, offset + 4))
            guard size >= 8, offset + size <= bytes.count else { break }
            if command == 0x19, offset + 56 <= bytes.count, segmentName(bytes, offset + 8) == "__LINKEDIT" {
                for index in 0..<8 { bytes[offset + 48 + index] = 0 }
            }

            if command == 0x1d, offset + 16 <= bytes.count {
                let start = Int(readU32(bytes, offset + 8))
                let length = Int(readU32(bytes, offset + 12))
                if start >= 0, length > 0, start + length <= bytes.count {
                    signature = (start, length)
                }

                for index in 0..<8 { bytes[offset + 8 + index] = 0 }
            }

            offset += size
        }

        if let signature {
            bytes.removeSubrange(signature.start..<(signature.start + signature.length))
        }

        return sha256Hex(Data(bytes))
    }

/*
===============
readU32
===============
*/
    private static func readU32(_ bytes: [UInt8], _ offset: Int) -> UInt32 {
        UInt32(bytes[offset])
            | UInt32(bytes[offset + 1]) << 8
            | UInt32(bytes[offset + 2]) << 16
            | UInt32(bytes[offset + 3]) << 24
    }

/*
===============
segmentName
===============
*/
    private static func segmentName(_ bytes: [UInt8], _ offset: Int) -> String {
        let raw = bytes[offset..<(offset + 16)].prefix { $0 != 0 }
        return String(decoding: raw, as: UTF8.self)
    }

/*
===============
sha256Hex
===============
*/
    private static func sha256Hex(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", UInt($0)) }.joined()
    }

/*
===============
binaryURL
===============
*/
    static func binaryURL() -> URL? {
        let candidates = [
            Bundle.main.executableURL?.deletingLastPathComponent().appendingPathComponent("FairleadHelper"),
            URL(fileURLWithPath: CommandLine.arguments[0])
                .resolvingSymlinksInPath()
                .deletingLastPathComponent()
                .appendingPathComponent("FairleadHelper"),
        ]
        return candidates.compactMap { $0 }.first { FileManager.default.isExecutableFile(atPath: $0.path) }
    }

/*
===============
install
Copies the bundled helper into launchd. Asks for the administrator password.
After this succeeds once, launchd keeps the helper up (KeepAlive + RunAtLoad),
so Fairlead must not ask again on ordinary connect, disconnect, or reboot.
===============
*/
    static func install() async -> String? {
        guard let binary = binaryURL() else {
            return FairleadL10n.text("FairleadHelper is missing next to Fairlead. Build the app with scripts/build-app.sh.")
        }

        guard let fingerprint = bundledFingerprint() else {
            return FairleadL10n.text("FairleadHelper is missing next to Fairlead. Build the app with scripts/build-app.sh.")
        }
        let script = """
        #!/bin/bash
        set -euo pipefail
        SRC=\(shellQuote(binary.path))
        USER_NAME=\(shellQuote(NSUserName()))
        PLIST=/Library/LaunchDaemons/com.fairlead.helper.plist
        LABEL=com.fairlead.helper
        LOG=/var/log/fairlead-helper.log
        mkdir -p /usr/local/libexec /var/run/fairlead
        # The password dialog's process is killed by taskgated if it execs this
        # ad-hoc binary (SIGKILL, "Code Signature Invalid"). launchd starts it
        # outside that session, which is allowed.
        launchctl bootout system/"$LABEL" >/dev/null 2>&1 || true
        launchctl bootout system/com.harbor.helper >/dev/null 2>&1 || true
        rm -f /Library/LaunchDaemons/com.harbor.helper.plist
        if [[ -f /var/run/harbor/helper.pid ]]; then
          old="$(cat /var/run/harbor/helper.pid || true)"
          if [[ -n "$old" ]]; then
            kill "$old" 2>/dev/null || true
          fi
        fi
        killall harbor-helper 2>/dev/null || true
        if [[ -f /var/run/fairlead/helper.pid ]]; then
          old="$(cat /var/run/fairlead/helper.pid || true)"
          if [[ -n "$old" ]]; then
            kill "$old" 2>/dev/null || true
          fi
        fi
        killall fairlead-helper 2>/dev/null || true
        sleep 0.3
        killall -9 fairlead-helper harbor-helper 2>/dev/null || true
        cp "$SRC" /usr/local/libexec/fairlead-helper
        chmod 755 /usr/local/libexec/fairlead-helper
        # codesign prints "replacing existing signature" on stderr. osascript treats
        # any stderr from a failing command as the error text, so keep it in the log.
        if ! codesign --force --sign - --identifier "$LABEL" /usr/local/libexec/fairlead-helper >>"$LOG" 2>&1; then
          echo \(shellQuote(FairleadL10n.text("Could not sign the helper."))) >&2
          tail -n 20 "$LOG" >&2 || true
          exit 1
        fi
        # amneziawg-go carries plain WireGuard too. Drop the old wireguard-go helper copy.
        rm -f /usr/local/libexec/fairlead-wireguard-go
        AWG_SRC="$(dirname "$SRC")/amneziawg-go"
        if [[ -x "$AWG_SRC" ]]; then
          cp "$AWG_SRC" /usr/local/libexec/fairlead-amneziawg-go
          chmod 755 /usr/local/libexec/fairlead-amneziawg-go
        else
          echo \(shellQuote(FairleadL10n.text("amneziawg-go is not included in this copy of Fairlead."))) >&2
          exit 1
        fi
        printf '%s\n' \(shellQuote(fingerprint)) > \(shellQuote(stampPath))
        chmod 644 \(shellQuote(stampPath))
        RES="$(cd "$(dirname "$SRC")/../Resources" && pwd)"
        LOC=/usr/local/libexec/FairleadLocalizations.bundle
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
          <key>CFBundleIdentifier</key><string>com.fairlead.localizations</string>
          <key>CFBundleDevelopmentRegion</key><string>en</string>
          <key>CFBundleName</key><string>FairleadLocalizations</string>
        </dict></plist>
        PLIST
        chown -R root:wheel "$LOC"
        chmod 755 "$LOC" "$LOC/Contents" "$LOC/Contents/Resources"
        rm -f /var/run/fairlead/fairlead.sock
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
            <string>/usr/local/libexec/fairlead-helper</string>
            <string>--user</string>
            <string>$ESCAPED</string>
          </array>
          <key>RunAtLoad</key>
          <true/>
          <key>KeepAlive</key>
          <true/>
          <key>ThrottleInterval</key>
          <integer>2</integer>
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
          echo \(shellQuote(FairleadL10n.text("launchd could not start the helper."))) >&2
          tail -n 20 "$LOG" >&2 || true
          exit 1
        fi
        for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60; do
          if [[ -S /var/run/fairlead/fairlead.sock ]]; then
            exit 0
          fi
          sleep 0.1
        done
        echo \(shellQuote(FairleadL10n.text("The helper did not open a socket. Last log lines:"))) >&2
        tail -n 40 "$LOG" >&2 || true
        exit 1
        """
        return await runShell(script)
    }

/*
===============
isInstalled
===============
*/
    static func isInstalled() -> Bool {
        FileManager.default.isExecutableFile(atPath: FairleadPaths.helperBinary)
            && FileManager.default.isReadableFile(atPath: "/Library/LaunchDaemons/com.fairlead.helper.plist")
    }

/*
===============
plistHasKeepAlive
Older installs lack KeepAlive, so a crash left the helper dead until a password kickstart.
===============
*/
    static func plistHasKeepAlive() -> Bool {
        guard let data = try? Data(contentsOf: URL(fileURLWithPath: "/Library/LaunchDaemons/com.fairlead.helper.plist")),
              let text = String(data: data, encoding: .utf8) else {
            return false
        }
        return text.contains("<key>KeepAlive</key>")
    }

/*
===============
runShell
===============
*/
    private static func runShell(_ script: String) async -> String? {
        let file = FileManager.default.temporaryDirectory.appendingPathComponent("fairlead-install-\(UUID().uuidString).sh")
        do {
            try script.write(to: file, atomically: true, encoding: .utf8)
            try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: file.path)
        } catch {
            return FairleadL10n.text("Could not prepare the installation.")
        }

        defer { try? FileManager.default.removeItem(at: file) }

        let escaped = file.path.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        let source = "do shell script \"/bin/bash \\\"\(escaped)\\\"\" with administrator privileges"
        return await runOSA(source)
    }

/*
===============
runOSA
===============
*/
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
                return FairleadL10n.text("The system did not confirm that the helper started.")
            }

            return text
        }.value
    }

/*
===============
shellQuote
===============
*/
    private static func shellQuote(_ value: String) -> String {
        "'" + value.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }
}
