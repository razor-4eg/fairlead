import AppKit
import FairleadCore
import SwiftUI

enum Theme {
    static let bg = Color(red: 0.043, green: 0.059, blue: 0.055)
    static let sidebar = Color(red: 0.031, green: 0.043, blue: 0.039)
    static let elevated = Color(red: 0.086, green: 0.110, blue: 0.098)
    static let text = Color(red: 0.910, green: 0.933, blue: 0.910)
    static let muted = Color(red: 0.576, green: 0.627, blue: 0.588)
    static let faint = Color(red: 0.369, green: 0.420, blue: 0.384)
    static let accent = Color(red: 0.486, green: 0.859, blue: 0.714)
    static let ink = Color(red: 0.063, green: 0.129, blue: 0.102)
    static let danger = Color(red: 0.882, green: 0.353, blue: 0.271)
    static let line = Color.white.opacity(0.08)

    static let nsBackground = NSColor(srgbRed: 0.043, green: 0.059, blue: 0.055, alpha: 1)
}

struct WindowTuner: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        TunerView()
    }

    func updateNSView(_ nsView: NSView, context: Context) {}

    final class TunerView: NSView {
        private var closeProxy: CloseProxy?
        private var styling = false

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            styleWindow()
        }

        override func layout() {
            super.layout()
            styleWindow()
        }

        private func styleWindow() {
            guard !styling, let window else { return }
            styling = true
            defer { styling = false }
            window.titlebarAppearsTransparent = true
            window.titleVisibility = .hidden
            window.styleMask.insert(.fullSizeContentView)
            window.isMovableByWindowBackground = true
            window.backgroundColor = Theme.nsBackground
            window.minSize = NSSize(width: 980, height: 660)
            guard !(window.delegate is CloseProxy) else { return }
            let proxy = CloseProxy(original: window.delegate)
            closeProxy = proxy
            window.delegate = proxy
        }
    }
}

/// Hides the window instead of destroying it, so the menu bar can open it again.
final class CloseProxy: NSObject, NSWindowDelegate {
    weak var original: NSWindowDelegate?

    init(original: NSWindowDelegate?) {
        self.original = original
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if FairleadDelegate.terminating {
            return original?.windowShouldClose?(sender) ?? true
        }
        sender.orderOut(nil)
        let anotherVisible = NSApp.windows.contains { $0 !== sender && $0.isVisible && $0.canBecomeMain }
        if !anotherVisible {
            NSApp.setActivationPolicy(.accessory)
        }
        return false
    }

    override func responds(to aSelector: Selector!) -> Bool {
        if super.responds(to: aSelector) { return true }
        return original?.responds(to: aSelector) ?? false
    }

    override func forwardingTarget(for aSelector: Selector!) -> Any? {
        if let original, original.responds(to: aSelector) {
            return original
        }
        return super.forwardingTarget(for: aSelector)
    }
}

enum MenuBarMark {
    private static let idle = draw(connected: false)
    private static let connected = draw(connected: true)

    static func image(connected: Bool) -> NSImage {
        connected ? Self.connected : idle
    }

    private static func draw(connected: Bool) -> NSImage {
        let side: CGFloat = 18
        let image = NSImage(size: NSSize(width: side, height: side), flipped: false) { rect in
            NSColor.black.set()
            let weight: CGFloat = connected ? 1.7 : 1.25
            let oval = NSBezierPath(ovalIn: rect.insetBy(dx: weight / 2 + 2.2, dy: weight / 2 + 2.2))
            oval.lineWidth = weight
            oval.lineCapStyle = .round
            oval.stroke()
            let dot: CGFloat = connected ? 4.4 : 3.2
            let center = NSPoint(x: rect.midX + 2.5, y: rect.midY + 0.7)
            NSBezierPath(
                ovalIn: NSRect(x: center.x - dot / 2, y: center.y - dot / 2, width: dot, height: dot)
            ).fill()
            return true
        }
        image.isTemplate = true
        return image
    }
}

struct Mark: View {
    var size: CGFloat = 18

    var body: some View {
        ZStack {
            Circle()
                .stroke(Theme.accent, lineWidth: max(1.25, size * 0.08))
            Circle()
                .fill(Theme.accent)
                .frame(width: size * 0.22, height: size * 0.22)
                .offset(x: size * 0.22, y: -size * 0.08)
        }
        .frame(width: size, height: size)
    }
}

struct QuietField: ViewModifier {
    func body(content: Content) -> some View {
        content
            .textFieldStyle(.plain)
            .font(.system(size: 13))
            .foregroundStyle(Theme.text)
            .padding(.horizontal, 10)
            .padding(.vertical, 8)
            .background(Color.white.opacity(0.04), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .strokeBorder(Theme.line)
            )
    }
}

extension View {
    func quietField() -> some View { modifier(QuietField()) }
}

struct FairleadButton: View {
    var title: LocalizedStringKey
    var systemImage: String?
    var prominent = false
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 6) {
                if let systemImage {
                    Image(systemName: systemImage)
                        .font(.system(size: 11, weight: .semibold))
                }
                Text(title)
                    .font(.system(size: 13, weight: .medium))
            }
            .foregroundStyle(prominent ? Theme.ink : Theme.text)
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .background(prominent ? Theme.accent : Color.white.opacity(0.06), in: Capsule())
            .overlay(Capsule().strokeBorder(prominent ? Color.clear : Theme.line))
        }
        .buttonStyle(.plain)
    }
}

enum Format {
    static func bytes(_ value: UInt64) -> String {
        let units = ["B", "KB", "MB", "GB", "TB"].map { FairleadL10n.text($0) }
        var amount = Double(value)
        var index = 0
        while amount >= 1024, index < units.count - 1 {
            amount /= 1024
            index += 1
        }
        if index == 0 { return "\(value) \(units[0])" }
        let formatted = String(format: "%.\(amount >= 10 ? 0 : 1)f", locale: Locale.current, amount)
        return "\(formatted) \(units[index])"
    }

    static func rate(_ bytesPerSecond: Double) -> String {
        bytes(UInt64(max(0, bytesPerSecond))) + FairleadL10n.text("/s")
    }

    static func ago(_ date: Date?, now: Date) -> String {
        guard let date else { return FairleadL10n.text("none") }
        let seconds = max(0, Int(now.timeIntervalSince(date)))
        if seconds < 5 { return FairleadL10n.text("just now") }
        if seconds < 60 { return FairleadL10n.format("%d s ago", seconds) }
        if seconds < 3600 { return FairleadL10n.format("%d min ago", seconds / 60) }
        if seconds < 86_400 { return FairleadL10n.format("%d h ago", seconds / 3600) }
        return FairleadL10n.text("a while ago")
    }

    static func host(from endpoint: String) -> String {
        guard !endpoint.isEmpty, endpoint != "(none)" else { return FairleadL10n.text("no endpoint") }
        return endpoint
    }
}
