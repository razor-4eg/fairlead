import AppKit
import FairleadCore
import SwiftUI

struct EditorSheet: View {
    @Environment(\.dismiss) private var dismiss
    @State private var name: String
    @State private var document: WGDocument
    @State private var raw: String
    @State private var showRaw: Bool
    @State private var revealKey = false
    var onSave: (Tunnel) -> Void
    private let original: Tunnel

    init(tunnel: Tunnel, onSave: @escaping (Tunnel) -> Void) {
        original = tunnel
        _name = State(initialValue: tunnel.name)
        _document = State(initialValue: WGDocument.parse(tunnel.configText))
        _raw = State(initialValue: tunnel.configText)
        _showRaw = State(initialValue: false)
        self.onSave = onSave
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Config")
                    .font(.system(size: 18, weight: .medium))
                    .foregroundStyle(Theme.text)
                Spacer()
                Picker("", selection: Binding(
                    get: { showRaw },
                    set: { switchMode($0) }
                )) {
                    Text("Fields").tag(false)
                    Text("Text").tag(true)
                }
                .pickerStyle(.segmented)
                .frame(width: 180)
            }
            .padding(.bottom, 16)

            if showRaw {
                TextEditor(text: $raw)
                    .font(.system(size: 12, design: .monospaced))
                    .foregroundStyle(Theme.text)
                    .scrollContentBackground(.hidden)
                    .padding(10)
                    .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Theme.line))
            } else {
                ScrollView {
                    VStack(alignment: .leading, spacing: 18) {
                        field("Name", text: $name, placeholder: "Home")
                        interfaceFields
                        ForEach(document.peerIndexes, id: \.self) { index in
                            peerFields(index)
                        }
                        Button("Add peer") { _ = document.addPeer() }
                            .buttonStyle(.plain)
                            .font(.system(size: 13, weight: .medium))
                            .foregroundStyle(Theme.accent)
                    }
                }
            }

            if configHasHooks {
                Text("PreUp, PostUp, PreDown, and PostDown commands in the config are not run.")
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.muted)
                    .padding(.top, 12)
            }

            if !issues.isEmpty {
                VStack(alignment: .leading, spacing: 4) {
                    ForEach(issues) { issue in
                        Text(issue.message)
                            .font(.system(size: 12))
                            .foregroundStyle(Theme.danger)
                    }
                }
                .padding(.top, 12)
            }

            HStack {
                Button("Copy") { copyConfig() }
                    .buttonStyle(.plain)
                    .font(.system(size: 13))
                    .foregroundStyle(Theme.muted)
                Spacer()
                FairleadButton(title: "Cancel") { dismiss() }
                FairleadButton(title: "Save", prominent: issues.isEmpty && !name.trimmingCharacters(in: .whitespaces).isEmpty) {
                    save()
                }
                .disabled(issues.isEmpty == false || name.trimmingCharacters(in: .whitespaces).isEmpty)
                .opacity(issues.isEmpty && !name.trimmingCharacters(in: .whitespaces).isEmpty ? 1 : 0.45)
            }
            .padding(.top, 16)
        }
        .padding(24)
        .frame(width: 720, height: 640)
        .background(Theme.bg)
    }

    private var issues: [ConfigIssue] {
        ConfigCheck.issues(in: currentDocument)
    }

    private var currentDocument: WGDocument {
        showRaw ? WGDocument.parse(raw) : document
    }

    private var configHasHooks: Bool {
        let text = (showRaw ? raw : document.render()).lowercased()
        return ["preup", "postup", "predown", "postdown"].contains { text.contains($0) }
    }

    private var interfaceFields: some View {
        VStack(alignment: .leading, spacing: 10) {
            sectionTitle("Interface")
            keyField
            if let derived = WGKeys.publicKey(forPrivateKey: interfaceValue("PrivateKey")), !derived.isEmpty {
                Text(FairleadL10n.format("Public key  %@", derived))
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(Theme.muted)
                    .textSelection(.enabled)
            }
            field("Address", text: interfaceBinding("Address"), placeholder: "10.8.0.2/32")
            field("DNS", text: interfaceBinding("DNS"), placeholder: "1.1.1.1")
            HStack(spacing: 10) {
                field("MTU", text: interfaceBinding("MTU"), placeholder: "auto")
                field("ListenPort", text: interfaceBinding("ListenPort"), placeholder: "auto")
            }
        }
    }

    private var keyField: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("PrivateKey")
                .font(.system(size: 11, weight: .medium))
                .foregroundStyle(Theme.muted)
            HStack(spacing: 8) {
                Group {
                    if revealKey {
                        TextField("key", text: interfaceBinding("PrivateKey"))
                    } else {
                        SecureField("key", text: interfaceBinding("PrivateKey"))
                    }
                }
                .quietField()
                Button(FairleadL10n.text(revealKey ? "Hide" : "Show")) { revealKey.toggle() }
                    .buttonStyle(.plain)
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.muted)
                Button("New key") { regenerate() }
                    .buttonStyle(.plain)
                    .font(.system(size: 12, weight: .medium))
                    .foregroundStyle(Theme.accent)
            }
        }
    }

    private func peerFields(_ index: Int) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                sectionTitle(FairleadL10n.format("Peer %d", document.peerIndexes.firstIndex(of: index).map { $0 + 1 } ?? 1))
                Spacer()
                if document.peerIndexes.count > 1 {
                    Button("Remove peer") { document.removeSection(at: index) }
                        .buttonStyle(.plain)
                        .font(.system(size: 12))
                        .foregroundStyle(Theme.danger)
                }
            }
            field("PublicKey", text: peerBinding(index, "PublicKey"), placeholder: "server public key")
            field("PresharedKey", text: peerBinding(index, "PresharedKey"), placeholder: "optional")
            field("Endpoint", text: peerBinding(index, "Endpoint"), placeholder: "vpn.example.com:51820")
            field("AllowedIPs", text: peerBinding(index, "AllowedIPs"), placeholder: "0.0.0.0/0, ::/0")
            field("PersistentKeepalive", text: peerBinding(index, "PersistentKeepalive"), placeholder: "25")
        }
    }

    private func sectionTitle(_ text: String) -> some View {
        Text(text.uppercased())
            .font(.system(size: 10, weight: .semibold))
            .tracking(1)
            .foregroundStyle(Theme.faint)
    }

    private func field(_ title: LocalizedStringKey, text: Binding<String>, placeholder: LocalizedStringKey) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.system(size: 11, weight: .medium))
                .foregroundStyle(Theme.muted)
            TextField(placeholder, text: text)
                .quietField()
        }
    }

    private func interfaceValue(_ key: String) -> String {
        guard let index = document.interfaceIndex else { return "" }
        return document.value(section: index, key: key)
    }

    private func interfaceBinding(_ key: String) -> Binding<String> {
        Binding(
            get: {
                guard let index = document.interfaceIndex else { return "" }
                switch key {
                case "Address": return document.interfaceAddresses
                case "DNS": return document.interfaceDNS
                case "MTU": return document.interfaceMTU
                case "ListenPort": return document.interfaceListenPort
                default: return document.value(section: index, key: key)
                }
            },
            set: { newValue in
                let index = document.ensureInterface()
                document.setValue(section: index, key: key, value: newValue)
            }
        )
    }

    private func peerBinding(_ index: Int, _ key: String) -> Binding<String> {
        Binding(
            get: {
                switch key {
                case "PublicKey": return document.peerPublicKey(index)
                case "PresharedKey": return document.peerPreshared(index)
                case "Endpoint": return document.peerEndpoint(index)
                case "AllowedIPs": return document.peerAllowedIPs(index)
                case "PersistentKeepalive": return document.peerKeepalive(index)
                default: return document.value(section: index, key: key)
                }
            },
            set: { newValue in
                guard document.sections.indices.contains(index) else { return }
                document.setValue(section: index, key: key, value: newValue)
            }
        )
    }

    private func switchMode(_ rawMode: Bool) {
        if rawMode {
            raw = document.render()
            showRaw = true
        } else {
            document = WGDocument.parse(raw)
            showRaw = false
        }
    }

    private func regenerate() {
        let keys = WGKeys.generate()
        let index = document.ensureInterface()
        document.setValue(section: index, key: "PrivateKey", value: keys.privateKey)
        revealKey = true
    }

    private func copyConfig() {
        let text = showRaw ? raw : document.render()
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    private func save() {
        var tunnel = original
        tunnel.name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        tunnel.configText = showRaw ? WGDocument.parse(raw).render() : document.render()
        onSave(tunnel)
        dismiss()
    }
}

