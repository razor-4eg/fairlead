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
        guard let normalized = DomainName.normalize(name) else { return nil }
        var data = Data()
        append16(&data, id)
        append16(&data, 0x0100)
        append16(&data, 1)
        append16(&data, 0)
        append16(&data, 0)
        append16(&data, edns ? 1 : 0)
        guard appendName(normalized, to: &data) else { return nil }
        append16(&data, type)
        append16(&data, 1)
        if edns {
            data.append(0)
            append16(&data, 41)
            append16(&data, 1232)
            append32(&data, 0)
            append16(&data, 0)
        }
        return data
    }

    public static func parse(_ data: Data, id: UInt16) -> Reply? {
        guard data.count >= 12, read16(data, 0) == id else { return nil }
        guard let flags = read16(data, 2), flags & 0x8000 != 0 else { return nil }
        guard let questions = read16(data, 4),
              let answerCount = read16(data, 6),
              let authority = read16(data, 8),
              let additionalCount = read16(data, 10) else { return nil }
        if questions > 8 || answerCount > 64 || authority > 64 || additionalCount > 64 { return nil }
        var offset = 12
        for _ in 0..<Int(questions) {
            guard skipName(data, &offset), skip(data, &offset, 4) else { return nil }
        }
        guard let answers = readSection(data, &offset, Int(answerCount)) else { return nil }
        guard skipSection(data, &offset, Int(authority)) else { return nil }
        guard let extra = readSection(data, &offset, Int(additionalCount)) else { return nil }
        return Reply(
            rcode: Int(flags & 0x000F),
            truncated: flags & 0x0200 != 0,
            answers: answers.records,
            additional: extra.records,
            sawCNAME: answers.cname
        )
    }

    private struct Section {
        var records: [Record]
        var cname: Bool
    }

    private static func readSection(_ data: Data, _ offset: inout Int, _ count: Int) -> Section? {
        var records: [Record] = []
        var cname = false
        for _ in 0..<count {
            guard skipName(data, &offset),
                  let type = read16(data, offset) else { return nil }
            offset += 2
            guard read16(data, offset) != nil else { return nil }
            offset += 2
            guard let ttl = read32(data, offset) else { return nil }
            offset += 4
            guard let length = read16(data, offset) else { return nil }
            offset += 2
            let size = Int(length)
            guard offset + size <= data.count else { return nil }
            let rdata = data.subdata(in: offset..<(offset + size))
            offset += size
            if type == 5 { cname = true }
            if let address = address(type: type, rdata: rdata) {
                records.append(Record(address: address, ttl: ttl))
            }
        }
        return Section(records: records, cname: cname)
    }

    private static func skipSection(_ data: Data, _ offset: inout Int, _ count: Int) -> Bool {
        readSection(data, &offset, count) != nil
    }

    private static func address(type: UInt16, rdata: Data) -> String? {
        if type == typeA, rdata.count == 4 {
            return IPAddress(family: .v4, bytes: Array(rdata)).string
        }
        if type == typeAAAA, rdata.count == 16 {
            return IPAddress(family: .v6, bytes: Array(rdata)).string
        }
        return nil
    }

    private static func appendName(_ name: String, to data: inout Data) -> Bool {
        for label in name.split(separator: ".") {
            let bytes = Array(label.utf8)
            if bytes.isEmpty || bytes.count > 63 { return false }
            data.append(UInt8(bytes.count))
            data.append(contentsOf: bytes)
        }
        data.append(0)
        return true
    }

    private static func skipName(_ data: Data, _ offset: inout Int) -> Bool {
        var cursor = offset
        var jumped = false
        var hops = 0
        while cursor < data.count {
            let length = Int(data[cursor])
            if length == 0 {
                if !jumped { offset = cursor + 1 }
                return true
            }
            if length & 0xC0 == 0xC0 {
                guard cursor + 1 < data.count else { return false }
                let pointer = ((length & 0x3F) << 8) | Int(data[cursor + 1])
                guard pointer < data.count, hops < 8 else { return false }
                if !jumped { offset = cursor + 2 }
                cursor = pointer
                jumped = true
                hops += 1
                continue
            }
            guard length <= 63, cursor + 1 + length <= data.count else { return false }
            cursor += 1 + length
            if !jumped { offset = cursor }
        }
        return false
    }

    private static func skip(_ data: Data, _ offset: inout Int, _ count: Int) -> Bool {
        guard offset + count <= data.count else { return false }
        offset += count
        return true
    }

    private static func read16(_ data: Data, _ offset: Int) -> UInt16? {
        guard offset + 2 <= data.count else { return nil }
        return (UInt16(data[offset]) << 8) | UInt16(data[offset + 1])
    }

    private static func read32(_ data: Data, _ offset: Int) -> UInt32? {
        guard offset + 4 <= data.count else { return nil }
        return (UInt32(data[offset]) << 24) | (UInt32(data[offset + 1]) << 16)
            | (UInt32(data[offset + 2]) << 8) | UInt32(data[offset + 3])
    }

    private static func append16(_ data: inout Data, _ value: UInt16) {
        data.append(UInt8(value >> 8))
        data.append(UInt8(value & 0xff))
    }

    private static func append32(_ data: inout Data, _ value: UInt32) {
        data.append(UInt8((value >> 24) & 0xff))
        data.append(UInt8((value >> 16) & 0xff))
        data.append(UInt8((value >> 8) & 0xff))
        data.append(UInt8(value & 0xff))
    }
}
