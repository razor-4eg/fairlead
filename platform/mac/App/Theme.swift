//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// Theme.swift -- Shared colors, the menu-bar mark, and the form controls.
//
//===============================================================

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

/// Shared springs — rubber for thumbs, tighter for press feedback.
enum Motion {
    /// Sliding pill / switch knob: overshoot like a soft rubber band.
    static let rubber = Animation.spring(response: 0.42, dampingFraction: 0.68, blendDuration: 0)
    /// Short settle for color and opacity that ride along the rubber move.
    static let soft = Animation.spring(response: 0.36, dampingFraction: 0.78, blendDuration: 0)
    /// Press-in / release on rows and buttons (Windows uses ~0.94 scale + spring).
    static let press = Animation.spring(response: 0.28, dampingFraction: 0.55, blendDuration: 0)
    /// Amnezia badge and other one-shot slide-ins.
    static let slideIn = Animation.spring(response: 0.48, dampingFraction: 0.78, blendDuration: 0)
}

/// Scale press like the Windows Composition spring on power / list rows.
struct FairleadPressStyle: ButtonStyle {
    var pressedScale: CGFloat = 0.96

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? pressedScale : 1)
            .animation(Motion.press, value: configuration.isPressed)
    }
}

struct WindowTuner: NSViewRepresentable {
/*
===============
makeNSView
===============
*/
    func makeNSView(context: Context) -> NSView {
        TunerView()
    }

/*
===============
updateNSView
===============
*/
    func updateNSView(_ nsView: NSView, context: Context) {}

    final class TunerView: NSView {
        private var closeProxy: CloseProxy?
        private var styling = false

/*
===============
viewDidMoveToWindow
===============
*/
        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            styleWindow()
        }

/*
===============
layout
===============
*/
        override func layout() {
            super.layout()
            styleWindow()
        }

/*
===============
styleWindow
===============
*/
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

/*
===============
init
===============
*/
    init(original: NSWindowDelegate?) {
        self.original = original
    }

/*
===============
windowShouldClose
===============
*/
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

/*
===============
responds
===============
*/
    override func responds(to aSelector: Selector!) -> Bool {
        if super.responds(to: aSelector) { return true }
        return original?.responds(to: aSelector) ?? false
    }

/*
===============
forwardingTarget
===============
*/
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

/*
===============
image
===============
*/
    static func image(connected: Bool) -> NSImage {
        connected ? Self.connected : idle
    }

/*
===============
draw
===============
*/
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
/*
===============
body
===============
*/
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
/*
===============
quietField
===============
*/
    func quietField() -> some View { modifier(QuietField()) }
}

struct FairleadChoice<Value: Hashable>: View {
    var selection: Value
    /// English catalog keys; resolved through FairleadL10n so the in-app language applies.
    var options: [(value: Value, title: String)]
    var select: (Value) -> Void
    @Namespace private var thumb

    var body: some View {
        HStack(spacing: 3) {
            ForEach(Array(options.enumerated()), id: \.offset) { _, option in
                let on = option.value == selection
                Button {
                    withAnimation(Motion.rubber) {
                        select(option.value)
                    }
                } label: {
                    Text(FairleadL10n.text(option.title))
                        .font(.system(size: 12, weight: .medium))
                        .foregroundStyle(on ? Theme.ink : Theme.muted)
                        .padding(.horizontal, 14)
                        .padding(.vertical, 6)
                        .background {
                            if on {
                                Capsule()
                                    .fill(Theme.accent)
                                    .matchedGeometryEffect(id: "thumb", in: thumb)
                            }
                        }
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
                .focusEffectDisabled()
            }
        }
        .padding(3)
        .background(Color.white.opacity(0.04), in: Capsule())
        .overlay(Capsule().strokeBorder(Theme.line))
        .animation(Motion.soft, value: selection)
    }
}

struct FairleadSwitch: View {
    @Binding var isOn: Bool
    var label: String? = nil
    /// Local knob position so the slide survives parent view rebuilds.
    @State private var knob = false

    var body: some View {
        Button {
            let next = !isOn
            withAnimation(Motion.rubber) {
                knob = next
            }
            isOn = next
        } label: {
            HStack(spacing: 8) {
                if let label {
                    Text(FairleadL10n.text(label))
                        .font(.system(size: 12, weight: .medium))
                        .foregroundStyle(Theme.muted)
                }

                ZStack(alignment: .leading) {
                    Capsule()
                        .fill(knob ? Theme.accent : Color.white.opacity(0.08))
                        .overlay(Capsule().strokeBorder(knob ? Color.clear : Theme.line))
                    Circle()
                        .fill(knob ? Theme.ink : Theme.muted)
                        .frame(width: 14, height: 14)
                        .shadow(color: .black.opacity(knob ? 0.18 : 0), radius: 1.5, y: 1)
                        .offset(x: knob ? 18 : 4)
                }
                .frame(width: 36, height: 20)
            }
        }
        .buttonStyle(.plain)
        .focusEffectDisabled()
        .onAppear { knob = isOn }
        .onChange(of: isOn) { _, value in
            guard knob != value else { return }
            withAnimation(Motion.rubber) {
                knob = value
            }
        }
    }
}

struct FairleadSelect: View {
    var selection: String
    var options: [(value: String, title: String)]
    var select: (String) -> Void
    @State private var open = false

    var body: some View {
        Button {
            open.toggle()
        } label: {
            HStack(spacing: 8) {
                Text(currentTitle)
                    .font(.system(size: 13))
                    .foregroundStyle(Theme.text)
                    .lineLimit(1)
                Spacer(minLength: 12)
                Image(systemName: "chevron.up.chevron.down")
                    .font(.system(size: 9, weight: .semibold))
                    .foregroundStyle(Theme.muted)
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .frame(width: 260, alignment: .leading)
            .background(Color.white.opacity(0.04), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .strokeBorder(open ? Theme.accent.opacity(0.7) : Theme.line)
            )
        }
        .buttonStyle(.plain)
        .focusEffectDisabled()
        .popover(isPresented: $open, arrowEdge: .bottom) {
            ScrollView {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(options, id: \.value) { option in
                        FairleadSelectRow(title: option.title, on: option.value == selection) {
                            select(option.value)
                            open = false
                        }
                    }
                }
                .padding(6)
            }
            .frame(width: 260)
            .frame(maxHeight: 280)
            .fixedSize(horizontal: false, vertical: true)
            .background(Theme.elevated)
            .presentationBackground(Theme.elevated)
            .presentationCornerRadius(12)
        }
    }

    private var currentTitle: String {
        options.first { $0.value == selection }?.title ?? ""
    }
}

private struct FairleadSelectRow: View {
    var title: String
    var on: Bool
    var action: () -> Void
    @State private var hovered = false

    var body: some View {
        Button(action: action) {
            HStack(spacing: 8) {
                Text(title)
                    .font(.system(size: 13))
                    .foregroundStyle(on ? Theme.ink : Theme.text)
                    .lineLimit(1)
                Spacer(minLength: 8)
                if on {
                    Image(systemName: "checkmark")
                        .font(.system(size: 10, weight: .bold))
                        .foregroundStyle(Theme.ink)
                }
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 7)
            .background(
                RoundedRectangle(cornerRadius: 7, style: .continuous)
                    .fill(on ? Theme.accent : (hovered ? Color.white.opacity(0.05) : Color.clear))
            )
        }
        .buttonStyle(.plain)
        .focusEffectDisabled()
        .onHover { hovered = $0 }
    }
}

struct FairleadButton: View {
    /// English catalog key; resolved through FairleadL10n so the in-app language applies.
    var title: String
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

                Text(FairleadL10n.text(title))
                    .font(.system(size: 13, weight: .medium))
            }
            .foregroundStyle(prominent ? Theme.ink : Theme.text)
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .background(prominent ? Theme.accent : Color.white.opacity(0.06), in: Capsule())
            .overlay(Capsule().strokeBorder(prominent ? Color.clear : Theme.line))
        }
        .buttonStyle(FairleadPressStyle(pressedScale: 0.95))
        .focusEffectDisabled()
    }
}

enum Format {
/*
===============
bytes
===============
*/
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

/*
===============
rate
===============
*/
    static func rate(_ bytesPerSecond: Double) -> String {
        let capped = min(max(0, bytesPerSecond), 10 * 1024 * 1024 * 1024)
        return bytes(UInt64(capped)) + FairleadL10n.text("/s")
    }

/*
===============
ago
===============
*/
    static func ago(_ date: Date?, now: Date) -> String {
        guard let date else { return FairleadL10n.text("none") }
        let seconds = max(0, Int(now.timeIntervalSince(date)))
        if seconds < 5 { return FairleadL10n.text("just now") }
        if seconds < 60 { return FairleadL10n.format("%d s ago", seconds) }
        if seconds < 3600 { return FairleadL10n.format("%d min ago", seconds / 60) }
        if seconds < 86_400 { return FairleadL10n.format("%d h ago", seconds / 3600) }
        return FairleadL10n.text("a while ago")
    }

/*
===============
host
===============
*/
    static func host(from endpoint: String) -> String {
        guard !endpoint.isEmpty, endpoint != "(none)" else { return FairleadL10n.text("no endpoint") }
        return endpoint
    }
}
