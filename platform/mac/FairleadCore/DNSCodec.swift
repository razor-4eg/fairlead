import FairleadC
import Foundation

/// Builds and reads the small DNS messages used to look up a direct domain.
public enum DNSCodec {
    public static let typeA: UInt16 = 1
    public static let typeAAAA: UInt16 = 28

    public struct Record: Equatable, Sendable {
        public var address: String
        public var ttl: UInt32

        public init(address: String, ttl: UInt32) {
            self.address = address
            self.ttl = ttl
        }
    }

    public struct Reply: Equatable, Sendable {
        public var rcode: Int
        public var truncated: Bool
        public var answers: [Record]
        public var additional: [Record]
        public var sawCNAME: Bool

        public init(rcode: Int, truncated: Bool, answers: [Record], additional: [Record], sawCNAME: Bool) {
            self.rcode = rcode
            self.truncated = truncated
            self.answers = answers
            self.additional = additional
            self.sawCNAME = sawCNAME
        }
    }

    public static func query(id: UInt16, name: String, type: UInt16, edns: Bool = true) -> Data? {
        var buffer = [UInt8](repeating: 0, count: 512)
        let size = dns_query(id, name, type, edns ? 1 : 0, &buffer, Int32(buffer.count))
        guard size > 0 else { return nil }
        return Data(buffer.prefix(Int(size)))
    }

    public static func parse(_ data: Data, id: UInt16) -> Reply? {
        var reply = dns_reply_t()
        let ok = data.withUnsafeBytes { raw -> Int32 in
            let base = raw.bindMemory(to: UInt8.self).baseAddress
            return dns_parse(base, Int32(raw.count), id, &reply)
        }
        guard ok != 0 else { return nil }
        return Reply(
            rcode: Int(reply.rcode),
            truncated: reply.truncated != 0,
            answers: records(reply.answers, count: Int(reply.answer_count)),
            additional: records(reply.additional, count: Int(reply.additional_count)),
            sawCNAME: reply.saw_cname != 0
        )
    }

    private static func records<T>(_ field: T, count: Int) -> [Record] {
        CBridge.elements(field, as: dns_rec_t.self, count: min(count, 32)).map {
            Record(address: CBridge.text($0.address), ttl: $0.ttl)
        }
    }
}
