import FairleadC
import Foundation

private final class ResultBox<V>: @unchecked Sendable { var value: V? }

/// Small helpers for crossing into the shared C kernel. Nothing here knows about WireGuard.
enum CBridge {
    /// Runs `body` with a zeroed buffer and returns what it wrote as a string.
    static func string(capacity: Int, _ body: (UnsafeMutablePointer<CChar>, Int32) -> Void) -> String {
        var buffer = [CChar](repeating: 0, count: capacity)
        buffer.withUnsafeMutableBufferPointer { pointer in
            body(pointer.baseAddress!, Int32(capacity))
        }
        return buffer.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
    }

    /// Like `string`, but the body reports success. A failure returns nil.
    static func optionalString(capacity: Int, _ body: (UnsafeMutablePointer<CChar>, Int32) -> Int32) -> String? {
        var buffer = [CChar](repeating: 0, count: capacity)
        let ok = buffer.withUnsafeMutableBufferPointer { pointer in
            body(pointer.baseAddress!, Int32(capacity))
        }
        guard ok != 0 else { return nil }
        return buffer.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
    }

    /// Reads a fixed `char[N]` member (imported as a tuple) as a string.
    static func text<T>(_ field: T) -> String {
        var copy = field
        return withUnsafeBytes(of: &copy) { raw in
            raw.bindMemory(to: CChar.self).baseAddress.map { String(cString: $0) } ?? ""
        }
    }

    /// Writes a string into a fixed `char[N]` member, truncating and terminating.
    static func fill<T>(_ field: inout T, _ value: String) {
        withUnsafeMutableBytes(of: &field) { raw in
            guard raw.count > 0 else { return }
            raw.initializeMemory(as: UInt8.self, repeating: 0)
            let bytes = Array(value.utf8.prefix(raw.count - 1))
            for (index, byte) in bytes.enumerated() {
                raw[index] = byte
            }
        }
    }

    /// Views a tuple-backed `T field[N]` member as an array of `T`.
    static func elements<Field, Element>(_ field: Field, as type: Element.Type, count: Int) -> [Element] {
        var copy = field
        return withUnsafeBytes(of: &copy) { raw in
            Array(raw.bindMemory(to: Element.self).prefix(count))
        }
    }

    /// Hands out `char [][N]` storage for C calls that fill an array of fixed-width strings.
    static func rows<R>(count: Int, width: Int, _ body: (UnsafeMutableRawPointer) -> R) -> (R, [String]) {
        var storage = [CChar](repeating: 0, count: count * width)
        let result = storage.withUnsafeMutableBytes { body($0.baseAddress!) }
        var lines: [String] = []
        for index in 0..<count {
            let start = index * width
            lines.append(storage.withUnsafeBufferPointer { String(cString: $0.baseAddress! + start) })
        }
        return (result, lines)
    }

    static func rebind<T, R>(_ raw: UnsafeMutableRawPointer, _ body: (UnsafeMutablePointer<T>) -> R) -> R {
        body(raw.assumingMemoryBound(to: T.self))
    }

    /// A `const char **` for the length of `body`.
    static func withCStrings<R>(
        _ strings: [String],
        _ body: (UnsafeMutablePointer<UnsafePointer<CChar>?>, Int32) -> R
    ) -> R {
        let owned: [UnsafeMutablePointer<CChar>] = strings.map { strdup($0)! }
        defer { owned.forEach { free($0) } }
        var pointers: [UnsafePointer<CChar>?] = owned.map { UnsafePointer($0) }
        pointers.append(nil)
        return pointers.withUnsafeMutableBufferPointer { buffer in
            body(buffer.baseAddress!, Int32(strings.count))
        }
    }

    /// Copies a malloc'd C string and frees it.
    static func take(_ pointer: UnsafeMutablePointer<CChar>?) -> String? {
        guard let pointer else { return nil }
        defer { free(pointer) }
        return String(cString: pointer)
    }

    /// The C kernel keeps large scratch buffers on the stack. Secondary threads only get 512 KB,
    /// so heavy calls run on a thread that has room.
    static func onLargeStack<R>(_ work: @escaping () -> R) -> R {
        if Thread.isMainThread { return work() }
        let box = ResultBox<R>()
        let done = DispatchSemaphore(value: 0)
        let thread = Thread {
            box.value = work()
            done.signal()
        }
        thread.stackSize = 16 << 20
        thread.start()
        done.wait()
        return box.value!
    }

    /// Matches a C message such as "Skipped invalid address 1.2.3.x." against `%@` templates and
    /// returns it in the app language. Unknown messages come back as they are.
    static func localize(_ message: String, templates: [String]) -> String {
        for template in templates {
            let pieces = template.components(separatedBy: "%@")
            guard pieces.count == 2 else { continue }
            let prefix = pieces[0]
            let suffix = pieces[1]
            guard message.hasPrefix(prefix), message.hasSuffix(suffix),
                  message.count >= prefix.count + suffix.count else { continue }
            let start = message.index(message.startIndex, offsetBy: prefix.count)
            let end = message.index(message.endIndex, offsetBy: -suffix.count)
            guard start <= end else { continue }
            var argument = String(message[start..<end])
            let local = "Local network "
            if argument.hasPrefix(local) {
                argument = FairleadL10n.format("Local network %@", String(argument.dropFirst(local.count)))
            }
            return FairleadL10n.format(template, argument)
        }
        return FairleadL10n.text(message)
    }
}
