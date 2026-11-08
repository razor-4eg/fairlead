//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// main.swift -- Checks for the Mac copy of the kernel.
//
//===============================================================

import Foundation
import FairleadCore

var failures: [String] = []

/*
===============
expect
===============
*/
func expect(_ condition: Bool, _ message: String) {
    if !condition { failures.append(message) }
}

/*
===============
expectEqual
===============
*/
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
expect(planned.direct.isEmpty, "exceptions have no separate direct list")
expect(!planned.warnings.isEmpty, "warnings")

let selected = BypassPlanner.routes(
    BypassInput(
        rules: ["0.0.0.0/0", "198.51.100.0/24", "203.0.113.10", "192.168.31.0/24"],
        learned: ["192.0.2.5"],
        lan: ["192.168.31.0/24"],
        excludeLAN: true,
        endpoints: ["203.0.113.10"],
        policy: .selected
    )
)
expect(selected.routes.contains("198.51.100.0/24"), "selected rule")
expect(selected.routes.contains("192.0.2.5/32"), "selected learned")
expect(!selected.routes.contains("203.0.113.10/32"), "selected skips endpoint")
expect(!selected.routes.contains("0.0.0.0/0"), "selected no default")
expect(!selected.routes.contains("192.168.31.0/24"), "selected lan stays off the tunnel")
expect(selected.direct.contains("192.168.31.0/24"), "selected lan direct")
expect(!selected.warnings.isEmpty, "selected warnings")

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
expect(!message.contains("jc="), "plain uapi skips amnezia")
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

/*
===============
u16
===============
*/
func u16(_ value: UInt16) -> [UInt8] { [UInt8(value >> 8), UInt8(value & 0xff)] }

/*
===============
u32
===============
*/
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

let awg = WGDocument.parse("""
[Interface]
PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
Address = 10.8.1.3/32
DNS = 1.1.1.1
Jc = 4
Jmin = 40
Jmax = 70
S1 = 15
S2 = 18
H1 = 100-200
H2 = 201-300
H3 = 301-400
H4 = 401-500
I1 = <b 0x0102><r 4>
HeaderProtectionKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
RandomTrailers = true

[Peer]
PublicKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
PresharedKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
AllowedIPs = 0.0.0.0/0, ::/0
Endpoint = 203.0.113.10:39743
PersistentKeepalive = 20-30
""")
expect(Amnezia.isAmnezia(awg), "detect awg")
let awgIssues = ConfigCheck.issues(in: awg).map(\.message)
expect(awgIssues.contains { $0.contains("Header protection") }, "header protection needs padding \(awgIssues)")
var protected = awg
let protectedIface = protected.ensureInterface()
protected.setValue(section: protectedIface, key: "S3", value: "12")
protected.setValue(section: protectedIface, key: "S4", value: "12")
expect(ConfigCheck.issues(in: protected).isEmpty, "awg config \(ConfigCheck.issues(in: protected).map(\.message))")
let awgMessage = WireGuardUAPI.setMessage(document: protected, endpoints: [protected.peerIndexes[0]: "203.0.113.10:39743"]) ?? ""
expect(awgMessage.contains("jc=4"), "uapi jc")
expect(awgMessage.contains("jmin=40"), "uapi jmin")
expect(awgMessage.contains("h1=100-200"), "uapi h1")
expect(awgMessage.contains("i1=<b 0x0102><r 4>"), "uapi i1")
expect(awgMessage.contains("header_protection_key=" + String(repeating: "0", count: 64)), "uapi header key")
expect(awgMessage.contains("random_trailers=true"), "uapi trailers")
expect(awgMessage.contains("persistent_keepalive_interval=20-30"), "uapi keepalive range")
expect(awgMessage.range(of: "jc=")!.lowerBound < awgMessage.range(of: "public_key=")!.lowerBound, "awg before peer")
var backwards = protected
backwards.setValue(section: protectedIface, key: "Jmin", value: "80")
expect(ConfigCheck.issues(in: backwards).contains { $0.message.contains("Jmin") }, "jmin above jmax")
var overlap = protected
overlap.setValue(section: protectedIface, key: "H2", value: "")
overlap.setValue(section: protectedIface, key: "H3", value: "")
overlap.setValue(section: protectedIface, key: "H4", value: "")
overlap.setValue(section: protectedIface, key: "H1", value: "1-4")
expect(ConfigCheck.issues(in: overlap).contains { $0.message.contains("must not overlap") }, "header overlap")

let last: [String: Any] = [
    "client_priv_key": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
    "client_ip": "10.8.1.3",
    "server_pub_key": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
    "Jc": "4",
    "Jmin": "40",
    "Jmax": "70",
    "S1": "15",
    "S2": "18",
    "H1": "100-200",
    "H2": "201-300",
    "H3": "301-400",
    "H4": "401-500",
    "I1": "<b 0x0102><r 4>",
    "allowed_ips": ["0.0.0.0/0", "::/0"],
    "persistent_keep_alive": "25",
    "hostName": "203.0.113.10",
    "port": 39743,
]
let lastData = try! JSONSerialization.data(withJSONObject: last)
let root: [String: Any] = [
    "description": "Office",
    "hostName": "203.0.113.10",
    "dns1": "1.1.1.1",
    "dns2": "1.0.0.1",
    "password": "should-not-leak",
    "userName": "root",
    "defaultContainer": "amnezia-awg",
    "containers": [[
        "container": "amnezia-awg",
        "awg": [
            "port": 39743,
            "last_config": String(data: lastData, encoding: .utf8)!,
        ],
    ]],
]
let shareData = try! JSONSerialization.data(withJSONObject: root)
let packed = QCompress.compress(shareData)!
var token = packed.base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_")
while token.hasSuffix("=") { token.removeLast() }
if case .wireGuard(let imported, let importedName) = ConfigImport.normalize("vpn://\n\(token)") {
    expectEqual(importedName, "Office", "share name")
    expect(imported.contains("Jc = 4"), "share jc")
    expect(imported.contains("I1 = <b 0x0102><r 4>"), "share i1")
    expect(imported.contains("Address = 10.8.1.3/32"), "share address")
    expect(imported.contains("Endpoint = 203.0.113.10:39743"), "share endpoint")
    expect(imported.contains("DNS = 1.1.1.1, 1.0.0.1"), "share dns")
    expect(!imported.contains("should-not-leak"), "share drops password")
    expect(!imported.contains("server_priv_key"), "share drops server key")
    expect(ConfigCheck.issues(in: WGDocument.parse(imported)).isEmpty, "share config \(ConfigCheck.issues(in: WGDocument.parse(imported)).map(\.message))")
} else {
    expect(false, "vpn share")
}

let xray = #"{"defaultContainer":"amnezia-xray","containers":[{"container":"amnezia-xray","xray":{}}]}"#
expect(ConfigImport.normalize(xray) == .unsupported, "xray share")
expect(ConfigImport.normalize("not a share") == .wireGuard("not a share", name: nil), "plain text stays")

if failures.isEmpty {
    print("ok")
} else {
    for failure in failures { fputs(failure + "\n", stderr) }
    exit(1)
}
