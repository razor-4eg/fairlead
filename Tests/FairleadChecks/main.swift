import Foundation
import FairleadCore

var failures: [String] = []

func expect(_ condition: Bool, _ message: String) {
    if !condition { failures.append(message) }
}

func expectEqual<T: Equatable>(_ actual: T, _ expected: T, _ message: String) {
    if actual != expected { failures.append("\(message): \(actual) != \(expected)") }
}

let source = """
# home
[Interface]
PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
Address = 10.8.0.2/32
Jc = 4
DNS = 1.1.1.1

[Peer]
PublicKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
AllowedIPs = 0.0.0.0/0, ::/0
Endpoint = vpn.example.com:51820
PersistentKeepalive = 25
"""
let document = WGDocument.parse(source)
expectEqual(document.values(section: document.interfaceIndex ?? -1, key: "Jc"), ["4"], "jc")
let rendered = document.render()
let again = WGDocument.parse(rendered)
expectEqual(again.values(section: again.interfaceIndex ?? -1, key: "Jc"), ["4"], "jc roundtrip")
expect(rendered.contains("# home"), "comment")
expectEqual(again.peerEndpoint(again.peerIndexes[0]), "vpn.example.com:51820", "endpoint")

var edited = WGDocument.parse("""
[Interface]
PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
Address = 10.0.0.2/32
DNS = 1.1.1.1
""")
let index = edited.ensureInterface()
edited.setValue(section: index, key: "Address", value: "10.8.0.5/32")
expectEqual(edited.interfaceAddresses, "10.8.0.5/32", "address")
expectEqual(edited.interfaceDNS, "1.1.1.1", "dns")

let pair = WGKeys.generate()
expect(WGKeys.isKey(pair.privateKey), "private key")
expectEqual(WGKeys.publicKey(forPrivateKey: pair.privateKey), pair.publicKey, "public key")
expect(!WGKeys.isKey("not-a-key"), "reject junk key")

expectEqual(CIDR.parse("1.2.3.4")?.string, "1.2.3.4/32", "host cidr")
let net = CIDR.parse("192.168.31.50/24")
expectEqual(net?.string, "192.168.31.0/24", "network cidr")
expect(net?.contains(IPAddress.parse("192.168.31.1")!) == true, "contains")
expect(net?.contains(IPAddress.parse("10.0.0.1")!) == false, "excludes")
expectEqual(CIDR.parse("::1")?.string, "::1/128", "v6")
expect(CIDR.parse("nope") == nil, "bad cidr")

let planned = BypassPlanner.routes(
    BypassInput(
        rules: ["0.0.0.0/0", "203.0.113.10", "198.51.100.0/24"],
        learned: ["203.0.113.10", "192.0.2.5"],
        lan: ["192.168.31.0/24"],
        excludeLAN: true,
        endpoints: ["203.0.113.10"]
    )
)
expect(!planned.routes.contains("0.0.0.0/0"), "no default")
expect(!planned.routes.contains("203.0.113.10/32"), "skip endpoint")
expect(planned.routes.contains("192.0.2.5/32"), "learned")
expect(planned.routes.contains("198.51.100.0/24"), "rule")
expect(planned.routes.contains("192.168.31.0/24"), "lan")
expect(!planned.warnings.isEmpty, "warnings")

expectEqual(EndpointParse.host(from: "vpn.example.com:51820"), "vpn.example.com", "host")
expectEqual(EndpointParse.host(from: "[2001:db8::1]:51820"), "2001:db8::1", "v6 host")
expect(EndpointParse.host(from: "(none)") == nil, "none")
expect(ConfigCheck.validEndpoint("vpn.example.com:51820"), "valid endpoint")
expect(!ConfigCheck.validEndpoint("vpn.example.com"), "invalid endpoint")

let exact = """
   route to: 192.168.31.0
destination: 192.168.31.0
       mask: 255.255.255.0
  interface: en0
"""
let broad = """
   route to: 1.2.3.4
destination: default
       mask: default
  interface: utun6
"""
let hostRoute = """
   route to: 192.168.31.1
destination: 192.168.31.1
  interface: en0
"""
expect(RouteText.isExact(CIDR.parse("192.168.31.0/24")!, snapshot: RouteText.snapshot(from: exact)), "exact net")
expect(!RouteText.isExact(CIDR.parse("1.2.3.4/32")!, snapshot: RouteText.snapshot(from: broad)), "not default")
expect(RouteText.isExact(CIDR.parse("192.168.31.1")!, snapshot: RouteText.snapshot(from: hostRoute)), "exact host")
expectEqual(RouteText.parseRouteGet(exact).interface, "en0", "iface")

let ifconfig = """
en0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500
\tinet6 fe80::1%en0 prefixlen 64 secured scopeid 0x12
\tinet 192.168.31.204 netmask 0xffffff00 broadcast 192.168.31.255
\tinet6 fd75:8939:e9f8:4012:c4d:2836:7b37:1e58 prefixlen 64 autoconf secured
\tstatus: active
"""
let networks = RouteText.networks(fromIfconfig: ifconfig)
expect(networks.contains("192.168.31.0/24"), "lan v4")
expect(networks.contains { $0.hasPrefix("fd75:8939:e9f8:4012:") && $0.hasSuffix("/64") }, "lan v6")
expect(!networks.contains { $0.hasPrefix("fe80:") }, "skip link local")

let dump = """
utun6\tpriv\tpub\t51820\toff
utun6\tpeer\tnone\t203.0.113.8:51820\t0.0.0.0/0,::/0\t1700000000\t100\t40\t25
"""
let devices = WGDump.parse(dump)
expectEqual(devices.count, 1, "devices")
expectEqual(devices[0].publicKey, "pub", "pubkey")
expectEqual(devices[0].listenPort, 51820, "port")
expectEqual(devices[0].peers[0].rx, UInt64(100), "rx")
expectEqual(devices[0].peers[0].tx, UInt64(40), "tx")
expectEqual(devices[0].peers[0].handshake, 1_700_000_000, "handshake")

expect(IPAddress.parse("127.0.0.1")?.isRoutableDestination == false, "loopback")
expect(IPAddress.parse("169.254.1.1")?.isRoutableDestination == false, "link local v4")
expect(IPAddress.parse("224.0.0.1")?.isRoutableDestination == false, "multicast")
expect(IPAddress.parse("1.1.1.1")?.isRoutableDestination == true, "public")
expect(IPAddress.parse("fe80::1")?.isRoutableDestination == false, "link local v6")

let uapiDoc = WGDocument.parse("""
[Interface]
PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
Address = 10.8.0.2/32
DNS = 1.1.1.1
ListenPort = 51820

[Peer]
PublicKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
AllowedIPs = 0.0.0.0/0, ::/0
Endpoint = 192.0.2.1:51820
PersistentKeepalive = 25
""")
let peerIndex = uapiDoc.peerIndexes[0]
let message = WireGuardUAPI.setMessage(document: uapiDoc, endpoints: [peerIndex: "192.0.2.1:51820"]) ?? ""
expect(message.contains("private_key=" + String(repeating: "0", count: 64)), "uapi private")
expect(!message.contains("AAAA"), "uapi hides base64")
expect(message.contains("endpoint=192.0.2.1:51820"), "uapi endpoint")
expect(message.contains("allowed_ip=0.0.0.0/0"), "uapi v4")
expect(message.contains("allowed_ip=::/0"), "uapi v6")
expect(message.contains("replace_peers=true"), "uapi replace")
expectEqual(WireGuardUAPI.socketEndpoint(ip: "2001:db8::1", original: "vpn.example:51820"), "[2001:db8::1]:51820", "v6 socket")

let got = WireGuardUAPI.parseGet("""
listen_port=51820
public_key=\(String(repeating: "0", count: 64))
public_key=\(String(repeating: "1", count: 64))
preshared_key=\(String(repeating: "0", count: 64))
endpoint=192.0.2.1:51820
last_handshake_time_sec=1700000000
rx_bytes=20
tx_bytes=10
persistent_keepalive_interval=25
allowed_ip=0.0.0.0/0
allowed_ip=::/0
errno=0
""", interface: "utun8")
expectEqual(got.listenPort, 51820, "uapi port")
expectEqual(got.peers.count, 1, "uapi peers")
expectEqual(got.peers[0].rx, UInt64(20), "uapi rx")
expectEqual(got.peers[0].tx, UInt64(10), "uapi tx")
expectEqual(got.peers[0].handshake, 1_700_000_000, "uapi handshake")
expectEqual(got.peers[0].endpoint, "192.0.2.1:51820", "uapi peer endpoint")
expect(got.peers[0].allowedIPs.contains("0.0.0.0/0"), "uapi allowed")
expect(got.publicKey != got.peers[0].publicKey, "device key differs")

expectEqual(DomainName.normalize("Example.COM."), "example.com", "domain")
expect(DomainName.normalize("*.example.com") == nil, "wildcard")
expect(DomainName.normalize("https://example.com") == nil, "url")
expect(DomainName.normalize("192.0.2.1") == nil, "domain rejects ip")
expect(DomainName.normalize("com") == nil, "single label")
expectEqual(DomainName.normalize("пример.рф"), "xn--e1afmkfd.xn--p1ai", "idn")

var packet = Data()
func u16(_ value: UInt16) -> [UInt8] { [UInt8(value >> 8), UInt8(value & 0xff)] }
func u32(_ value: UInt32) -> [UInt8] {
    [UInt8((value >> 24) & 0xff), UInt8((value >> 16) & 0xff), UInt8((value >> 8) & 0xff), UInt8(value & 0xff)]
}
packet.append(contentsOf: u16(0x1234))
packet.append(contentsOf: u16(0x8180))
packet.append(contentsOf: u16(1))
packet.append(contentsOf: u16(2))
packet.append(contentsOf: u16(0))
packet.append(contentsOf: u16(0))
packet.append(7)
packet.append(contentsOf: Array("example".utf8))
packet.append(3)
packet.append(contentsOf: Array("com".utf8))
packet.append(0)
packet.append(contentsOf: u16(1))
packet.append(contentsOf: u16(1))
packet.append(0xC0)
packet.append(12)
packet.append(contentsOf: u16(5))
packet.append(contentsOf: u16(1))
packet.append(contentsOf: u32(30))
packet.append(contentsOf: u16(0))
packet.append(0xC0)
packet.append(12)
packet.append(contentsOf: u16(1))
packet.append(contentsOf: u16(1))
packet.append(contentsOf: u32(60))
packet.append(contentsOf: u16(4))
packet.append(contentsOf: [192, 0, 2, 1])
let reply = DNSCodec.parse(packet, id: 0x1234)
expectEqual(reply?.rcode, 0, "dns rcode")
expectEqual(reply?.sawCNAME, true, "dns cname")
expectEqual(reply?.answers.first?.address, "192.0.2.1", "dns address")
expectEqual(reply?.answers.first?.ttl, UInt32(60), "dns ttl")
expect(DNSCodec.parse(packet, id: 1) == nil, "dns id")
expect(DNSCodec.query(id: 1, name: "*.example.com", type: DNSCodec.typeA) == nil, "dns query wildcard")
let query = DNSCodec.query(id: 7, name: "Example.com", type: DNSCodec.typeA)
expect(query?.contains(41) == true, "dns edns")

let dnsText = """
resolver #1
  nameserver[0] : 192.0.2.53
  nameserver[1] : 2001:db8::53
  if_index : 14 (en0)
resolver #2
  domain   : local
  options  : mdns
  nameserver[0] : 192.0.2.1
"""
let snap = ScutilDNS.parse(dnsText)
expectEqual(snap.servers, ["192.0.2.53", "2001:db8::53"], "scutil servers")
expectEqual(snap.interface, "en0", "scutil iface")
expect(ScutilDNS.parse("resolver #1\n  nameserver[0] : 1.1.1.1\n  if_index : 20 (utun7)\n").interface == nil, "skip utun")

if let live = DomainLookup.resolve(name: "example.com", servers: ["1.1.1.1"], interface: nil) {
    let addresses = (live.v4 ?? []) + (live.v6 ?? [])
    expect(!addresses.isEmpty, "live example.com")
    expect(addresses.allSatisfy { IPAddress.parse($0) != nil }, "live addresses")
    expect(live.ttl >= 30 && live.ttl <= 3600, "live ttl")
}

if failures.isEmpty {
    print("ok")
} else {
    for failure in failures { fputs(failure + "\n", stderr) }
    exit(1)
}
