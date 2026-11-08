import Darwin
import Foundation

public enum SocketFrameError: Error, CustomStringConvertible {
    case disconnected
    case failed(String)
    case tooLarge

    public var description: String {
        switch self {
        case .disconnected: return FairleadL10n.text("The connection to the helper closed.")
        case .failed(let message): return message
        case .tooLarge: return FairleadL10n.text("The helper response is too large.")
        }
    }
}

public enum SocketFrame {
    public static func write(_ fd: Int32, _ payload: Data) throws {
        var length = UInt32(payload.count).bigEndian
        try writeAll(fd, Data(bytes: &length, count: 4))
        try writeAll(fd, payload)
    }

    public static func read(_ fd: Int32) throws -> Data {
        let header = try readExact(fd, 4)
        let length = header.withUnsafeBytes { raw in
            UInt32(bigEndian: raw.load(as: UInt32.self))
        }
        guard length <= RPCFrame.maxBytes else { throw SocketFrameError.tooLarge }
        if length == 0 { return Data() }
        return try readExact(fd, Int(length))
    }

    public static func setTimeout(_ fd: Int32, seconds: Int) {
        var timeout = timeval(tv_sec: seconds, tv_usec: 0)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
    }

    public static func connect(path: String) throws -> Int32 {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { throw SocketFrameError.failed(FairleadL10n.text("Could not open the socket.")) }
        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        guard setUnixPath(path, into: &addr) else {
            close(fd)
            throw SocketFrameError.failed(FairleadL10n.text("The socket path is too long."))
        }
        let size = socklen_t(MemoryLayout<sockaddr_un>.size)
        let rc = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                Darwin.connect(fd, sa, size)
            }
        }
        guard rc == 0 else {
            close(fd)
            throw SocketFrameError.failed(FairleadL10n.text("The helper is not running."))
        }
        return fd
    }

    public static func bind(path: String) throws -> Int32 {
        unlink(path)
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { throw SocketFrameError.failed(FairleadL10n.text("Could not create the socket.")) }
        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        guard setUnixPath(path, into: &addr) else {
            close(fd)
            throw SocketFrameError.failed(FairleadL10n.text("The socket path is too long."))
        }
        let size = socklen_t(MemoryLayout<sockaddr_un>.size)
        let rc = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                Darwin.bind(fd, sa, size)
            }
        }
        guard rc == 0 else {
            let message = String(cString: strerror(errno))
            close(fd)
            throw SocketFrameError.failed(FairleadL10n.format("Could not bind the socket: %@.", message))
        }
        guard listen(fd, 8) == 0 else {
            close(fd)
            throw SocketFrameError.failed(FairleadL10n.text("Could not listen on the socket."))
        }
        return fd
    }

    public static func setUnixPath(_ path: String, into addr: inout sockaddr_un) -> Bool {
        let bytes = Array(path.utf8)
        guard bytes.count < 104 else { return false }
        withUnsafeMutableBytes(of: &addr.sun_path) { raw in
            guard let base = raw.baseAddress else { return }
            memset(base, 0, raw.count)
            bytes.withUnsafeBytes { src in
                guard let srcBase = src.baseAddress else { return }
                memcpy(base, srcBase, bytes.count)
            }
        }
        return true
    }

    private static func writeAll(_ fd: Int32, _ data: Data) throws {
        try data.withUnsafeBytes { raw in
            guard let base = raw.baseAddress else { return }
            var sent = 0
            while sent < data.count {
                let rc = Darwin.write(fd, base.advanced(by: sent), data.count - sent)
                if rc == 0 { throw SocketFrameError.disconnected }
                if rc < 0 {
                    if errno == EINTR { continue }
                    throw SocketFrameError.failed(String(cString: strerror(errno)))
                }
                sent += rc
            }
        }
    }

    private static func readExact(_ fd: Int32, _ count: Int) throws -> Data {
        var data = Data(count: count)
        var got = 0
        try data.withUnsafeMutableBytes { raw in
            guard let base = raw.baseAddress else { return }
            while got < count {
                let rc = Darwin.read(fd, base.advanced(by: got), count - got)
                if rc == 0 { throw SocketFrameError.disconnected }
                if rc < 0 {
                    if errno == EINTR { continue }
                    throw SocketFrameError.failed(String(cString: strerror(errno)))
                }
                got += rc
            }
        }
        return data
    }
}

public enum RPCClient {
    public static func call(_ request: RPCRequest, timeout: Int = 20) throws -> RPCResponse {
        let fd = try SocketFrame.connect(path: FairleadPaths.socket)
        defer { close(fd) }
        SocketFrame.setTimeout(fd, seconds: timeout)
        let payload = try RPCFrame.encode(request)
        try SocketFrame.write(fd, payload)
        let response = try SocketFrame.read(fd)
        return try RPCFrame.decode(RPCResponse.self, from: response)
    }
}
