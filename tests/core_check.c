//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// core_check.c -- The kernel cases. Same ground the Mac checks cover, run against core/.
//
//===============================================================

#include "fl.h"
#include "fl_ip.h"
#include "fl_doc.h"
#include "fl_keys.h"
#include "fl_dns.h"
#include "fl_bypass.h"
#include "fl_uapi.h"
#include "fl_amnezia.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

static int s_failures = 0;

static int list_has(char list[][64], int n, const char *item);
static int contains_list(const fl_bypass_t *plan, const char *item);
static void put16p(unsigned char *p, int *n, unsigned int v);
static void put32p(unsigned char *p, int *n, unsigned int v);
size_t fairlead_qcompress(const unsigned char *src, size_t src_len, unsigned char *dst, size_t dst_cap);

/*
===============
expect
Count a failed check.
===============
*/
static void expect(int cond, const char *msg) {
	if (!cond) {
		fprintf(stderr, "%s\n", msg);
		s_failures++;
	}
}

/*
===============
expect_str
Compare two strings.
===============
*/
static void expect_str(const char *actual, const char *wanted, const char *msg) {
	if (!actual) {
		actual = "";
	}

	if (strcmp(actual, wanted) != 0) {
		fprintf(stderr, "%s: %s != %s\n", msg, actual, wanted);
		s_failures++;
	}
}

/*
===============
contains
True when the needle occurs.
===============
*/
static int contains(const char *hay, const char *needle) {
	return hay && needle && strstr(hay, needle) != NULL;
}

/*
===============
issue_has
True when the check list has this text.
===============
*/
static int issue_has(char issues[][192], int n, const char *bit) {
	int		i;

	for (i = 0; i < n; i++) {
		if (strstr(issues[i], bit)) {
			return 1;
		}
	}

	return 0;
}

/*
===============
main
Run the kernel checks.
===============
*/
int main(void) {
	fl_doc_t			doc;
	char				*rendered;
	char				value[256];
	char				priv[128];
	char				pub[128];
	fl_cidr_t			cidr;
	fl_ip_t				ip;
	const char			*rules[] = {"0.0.0.0/0", "203.0.113.10", "198.51.100.0/24"};
	const char			*learned[] = {"203.0.113.10", "192.0.2.5"};
	const char			*lan[] = {"192.168.31.0/24"};
	const char			*eps[] = {"203.0.113.10"};
	fl_bypass_t			plan;
	char				host[128];
	int					nnets;
	fl_device_status_t	devices[2];
	char				message[4096];
	const char			*endpoints[8];
	int					peers[4];
	fl_device_status_t	got;
	char				zeros[65];
	unsigned char		packet[128];
	int					plen;
	fl_dns_reply_t		reply;
	unsigned char		query[128];
	int					qlen;
	char				domain[256];
	int					ni;
	fl_route_snap_t		snap;
	char				share_json[4096];
	unsigned char		packed[8192];
	size_t				packed_n;
	char				token[8192];
	char				imported[8192];
	char				imported_name[128];
	int					irc;
	const char			*xray;
	WSADATA				wsa;
	int					index;
	char				again[128];
	const char			*srules[] = {"0.0.0.0/0", "198.51.100.0/24", "203.0.113.10", "192.168.31.0/24"};
	const char			*slearned[] = {"192.0.2.5"};
	int					i;
	int					found;
	int					link;
	char				getbuf[1024];
	char				z[65];
	char				o[65];
	int					saw;
	int					iface;
	static const char	tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int					tn;
	char				wrapped[9000];
	unsigned int		v;
	int					rem;
	int					k;

	const char *source =
		"# home\n"
		"[Interface]\n"
		"PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
		"Address = 10.8.0.2/32\n"
		"Jc = 4\n"
		"DNS = 1.1.1.1\n"
		"\n"
		"[Peer]\n"
		"PublicKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
		"AllowedIPs = 0.0.0.0/0, ::/0\n"
		"Endpoint = vpn.example.com:51820\n"
		"PersistentKeepalive = 25\n";
	char nets[8][64];
	const char *dump =
		"utun6\tpriv\tpub\t51820\toff\n"
		"utun6\tpeer\tnone\t203.0.113.8:51820\t0.0.0.0/0,::/0\t1700000000\t100\t40\t25\n";
	plen = 0;
	char issues[16][192];
	const char *awg_src =
		"[Interface]\n"
		"PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
		"Address = 10.8.1.3/32\n"
		"DNS = 1.1.1.1\n"
		"Jc = 4\n"
		"Jmin = 40\n"
		"Jmax = 70\n"
		"S1 = 15\n"
		"S2 = 18\n"
		"H1 = 100-200\n"
		"H2 = 201-300\n"
		"H3 = 301-400\n"
		"H4 = 401-500\n"
		"I1 = <b 0x0102><r 4>\n"
		"HeaderProtectionKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
		"RandomTrailers = true\n"
		"\n"
		"[Peer]\n"
		"PublicKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
		"PresharedKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
		"AllowedIPs = 0.0.0.0/0, ::/0\n"
		"Endpoint = 203.0.113.10:39743\n"
		"PersistentKeepalive = 20-30\n";
	const char *exact =
		"   route to: 192.168.31.0\n"
		"destination: 192.168.31.0\n"
		"       mask: 255.255.255.0\n"
		"  interface: en0\n";
	const char *broad =
		"   route to: 1.2.3.4\n"
		"destination: default\n"
		"       mask: default\n"
		"  interface: utun6\n";
	const char *host_route =
		"   route to: 192.168.31.1\n"
		"destination: 192.168.31.1\n"
		"  interface: en0\n";
	const char *ifconfig =
		"en0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500\n"
		"\tinet6 fe80::1%en0 prefixlen 64 secured scopeid 0x12\n"
		"\tinet 192.168.31.204 netmask 0xffffff00 broadcast 192.168.31.255\n"
		"\tinet6 fd75:8939:e9f8:4012:c4d:2836:7b37:1e58 prefixlen 64 autoconf secured\n"
		"\tstatus: active\n";
	xray = "{\"defaultContainer\":\"amnezia-xray\",\"containers\":[{\"container\":\"amnezia-xray\",\"xray\":{}}]}";

#ifdef _WIN32
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

	fl_doc_init(&doc);
	fl_doc_parse(&doc, source);
	fl_doc_value(&doc, fl_doc_interface(&doc), "Jc", value, (int)sizeof(value));
	expect_str(value, "4", "jc");
	rendered = fl_doc_render(&doc);
	expect(rendered && contains(rendered, "# home"), "comment");
	fl_doc_free(&doc);
	fl_doc_parse(&doc, rendered);
	fl_doc_value(&doc, fl_doc_interface(&doc), "Jc", value, (int)sizeof(value));
	expect_str(value, "4", "jc roundtrip");
	fl_doc_peers(&doc, peers, 4);
	fl_doc_value(&doc, peers[0], "Endpoint", value, (int)sizeof(value));
	expect_str(value, "vpn.example.com:51820", "endpoint");
	free(rendered);

	fl_doc_free(&doc);
	fl_doc_parse(&doc,
		"[Interface]\n"
		"PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
		"Address = 10.0.0.2/32\n"
		"DNS = 1.1.1.1\n");
	{
		index = fl_doc_ensure_interface(&doc);
		fl_doc_set(&doc, index, "Address", "10.8.0.5/32");
		fl_doc_addresses(&doc, value, (int)sizeof(value));
		expect_str(value, "10.8.0.5/32", "address");
		fl_doc_dns(&doc, value, (int)sizeof(value));
		expect_str(value, "1.1.1.1", "dns");
	}

	expect(fl_key_generate(priv, (int)sizeof(priv), pub, (int)sizeof(pub)), "generate");
	expect(fl_key_is(priv), "private key");
	{
		expect(fl_key_public(priv, again, (int)sizeof(again)), "public derive");
		expect_str(again, pub, "public key");
	}

	expect(!fl_key_is("not-a-key"), "reject junk key");

	expect(fl_cidr_parse("1.2.3.4", &cidr), "host parse");
	fl_cidr_format(&cidr, value, (int)sizeof(value));
	expect_str(value, "1.2.3.4/32", "host cidr");
	expect(fl_cidr_parse("192.168.31.50/24", &cidr), "net parse");
	fl_cidr_format(&cidr, value, (int)sizeof(value));
	expect_str(value, "192.168.31.0/24", "network cidr");
	expect(fl_ip_parse("192.168.31.1", &ip) && fl_cidr_contains(&cidr, &ip), "contains");
	expect(fl_ip_parse("10.0.0.1", &ip) && !fl_cidr_contains(&cidr, &ip), "excludes");
	expect(fl_cidr_parse("::1", &cidr), "v6 parse");
	fl_cidr_format(&cidr, value, (int)sizeof(value));
	expect_str(value, "::1/128", "v6");
	expect(!fl_cidr_parse("nope", &cidr), "bad cidr");

	fl_bypass_plan(0, rules, 3, learned, 2, lan, 1, 1, eps, 1, &plan);
	expect(!contains_list(&plan, "0.0.0.0/0"), "no default");
	expect(list_has(plan.routes, plan.route_count, "192.0.2.5/32"), "learned");
	expect(list_has(plan.routes, plan.route_count, "198.51.100.0/24"), "rule");
	expect(list_has(plan.routes, plan.route_count, "192.168.31.0/24"), "lan");
	expect(!list_has(plan.routes, plan.route_count, "203.0.113.10/32"), "skip endpoint");
	expect(plan.direct_count == 0, "exceptions have no separate direct list");
	expect(plan.warning_count > 0, "warnings");

	{
		fl_bypass_plan(1, srules, 4, slearned, 1, lan, 1, 1, eps, 1, &plan);
		expect(list_has(plan.routes, plan.route_count, "198.51.100.0/24"), "selected rule");
		expect(list_has(plan.routes, plan.route_count, "192.0.2.5/32"), "selected learned");
		expect(!list_has(plan.routes, plan.route_count, "203.0.113.10/32"), "selected skips endpoint");
		expect(!list_has(plan.routes, plan.route_count, "0.0.0.0/0"), "selected no default");
		expect(!list_has(plan.routes, plan.route_count, "192.168.31.0/24"), "selected lan stays off the tunnel");
		expect(list_has(plan.direct, plan.direct_count, "192.168.31.0/24"), "selected lan direct");
		expect(plan.warning_count > 0, "selected warnings");
	}

	expect(fl_endpoint_host("vpn.example.com:51820", host, (int)sizeof(host)), "host ok");
	expect_str(host, "vpn.example.com", "host");
	expect(fl_endpoint_host("[2001:db8::1]:51820", host, (int)sizeof(host)), "v6 host ok");
	expect_str(host, "2001:db8::1", "v6 host");
	expect(!fl_endpoint_host("(none)", host, (int)sizeof(host)), "none");
	expect(fl_endpoint_valid("vpn.example.com:51820"), "valid endpoint");
	expect(!fl_endpoint_valid("vpn.example.com"), "invalid endpoint");

	expect(fl_cidr_parse("192.168.31.0/24", &cidr), "exact parse");
	fl_route_snapshot(exact, &snap);
	expect(fl_route_exact(&cidr, &snap), "exact net");
	expect(fl_cidr_parse("1.2.3.4/32", &cidr), "broad parse");
	fl_route_snapshot(broad, &snap);
	expect(!fl_route_exact(&cidr, &snap), "not default");
	expect(fl_cidr_parse("192.168.31.1", &cidr), "host route parse");
	fl_route_snapshot(host_route, &snap);
	expect(fl_route_exact(&cidr, &snap), "exact host");
	fl_route_snapshot(exact, &snap);
	expect_str(snap.interface_name, "en0", "iface");

	nnets = fl_ifconfig_networks(ifconfig, nets, 8);
	expect(nnets >= 1 && list_has(nets, nnets, "192.168.31.0/24"), "lan v4");
	{
		found = 0;
		link = 0;

		for (i = 0; i < nnets; i++) {
			if (strncmp(nets[i], "fd75:8939:e9f8:4012:", 20) == 0 && strstr(nets[i], "/64")) {
				found = 1;
			}

			if (strncmp(nets[i], "fe80:", 5) == 0) {
				link = 1;
			}
		}

		expect(found, "lan v6");
		expect(!link, "skip link local");
	}

	expect(fl_dump_parse(dump, devices, 2) == 1, "devices");
	expect_str(devices[0].public_key, "pub", "pubkey");
	expect(devices[0].listen_port == 51820, "port");
	expect(devices[0].peers[0].rx == 100, "rx");
	expect(devices[0].peers[0].tx == 40, "tx");
	expect(devices[0].peers[0].handshake == 1700000000, "handshake");

	expect(fl_ip_parse("127.0.0.1", &ip) && !fl_ip_routable(&ip), "loopback");
	expect(fl_ip_parse("169.254.1.1", &ip) && !fl_ip_routable(&ip), "link local v4");
	expect(fl_ip_parse("224.0.0.1", &ip) && !fl_ip_routable(&ip), "multicast");
	expect(fl_ip_parse("1.1.1.1", &ip) && fl_ip_routable(&ip), "public");
	expect(fl_ip_parse("fe80::1", &ip) && !fl_ip_routable(&ip), "link local v6");

	fl_doc_free(&doc);
	fl_doc_parse(&doc,
		"[Interface]\n"
		"PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
		"Address = 10.8.0.2/32\n"
		"DNS = 1.1.1.1\n"
		"ListenPort = 51820\n"
		"\n"
		"[Peer]\n"
		"PublicKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
		"AllowedIPs = 0.0.0.0/0, ::/0\n"
		"Endpoint = 192.0.2.1:51820\n"
		"PersistentKeepalive = 25\n");
	memset(endpoints, 0, sizeof(endpoints));
	fl_doc_peers(&doc, peers, 4);
	endpoints[peers[0]] = "192.0.2.1:51820";
	expect(fl_uapi_set(&doc, endpoints, message, (int)sizeof(message)), "uapi set");
	memset(zeros, '0', 64);
	zeros[64] = '\0';
	expect(contains(message, "private_key=") && strstr(message, zeros), "uapi private");
	expect(!contains(message, "AAAA"), "uapi hides base64");
	expect(!contains(message, "jc="), "plain uapi skips amnezia");
	expect(contains(message, "endpoint=192.0.2.1:51820"), "uapi endpoint");
	expect(contains(message, "allowed_ip=0.0.0.0/0"), "uapi v4");
	expect(contains(message, "allowed_ip=::/0"), "uapi v6");
	expect(contains(message, "replace_peers=true"), "uapi replace");
	expect(fl_socket_endpoint("2001:db8::1", "vpn.example:51820", value, (int)sizeof(value)), "socket");
	expect_str(value, "[2001:db8::1]:51820", "v6 socket");

	{
		memset(z, '0', 64); z[64] = 0;
		memset(o, '1', 64); o[64] = 0;
		snprintf(getbuf, sizeof(getbuf),
			"listen_port=51820\n"
			"public_key=%s\n"
			"public_key=%s\n"
			"preshared_key=%s\n"
			"endpoint=192.0.2.1:51820\n"
			"last_handshake_time_sec=1700000000\n"
			"rx_bytes=20\n"
			"tx_bytes=10\n"
			"persistent_keepalive_interval=25\n"
			"allowed_ip=0.0.0.0/0\n"
			"allowed_ip=::/0\n"
			"errno=0\n", z, o, z);
		fl_uapi_parse_get(getbuf, "utun8", &got);
	}

	expect(got.listen_port == 51820, "uapi port");
	expect(got.peer_count == 1, "uapi peers");
	expect(got.peers[0].rx == 20, "uapi rx");
	expect(got.peers[0].tx == 10, "uapi tx");
	expect(got.peers[0].handshake == 1700000000, "uapi handshake");
	expect_str(got.peers[0].endpoint, "192.0.2.1:51820", "uapi peer endpoint");
	expect(contains(got.peers[0].allowed, "0.0.0.0/0"), "uapi allowed");
	expect(strcmp(got.public_key, got.peers[0].public_key) != 0, "device key differs");

	expect(fl_domain_normalize("Example.COM.", domain, (int)sizeof(domain)), "domain ok");
	expect_str(domain, "example.com", "domain");
	expect(!fl_domain_normalize("*.example.com", domain, (int)sizeof(domain)), "wildcard");
	expect(!fl_domain_normalize("https://example.com", domain, (int)sizeof(domain)), "url");
	expect(!fl_domain_normalize("192.0.2.1", domain, (int)sizeof(domain)), "domain rejects ip");
	expect(!fl_domain_normalize("com", domain, (int)sizeof(domain)), "single label");
	expect(fl_domain_normalize("пример.рф", domain, (int)sizeof(domain)), "idn ok");
	expect_str(domain, "xn--e1afmkfd.xn--p1ai", "idn");

	put16p(packet, &plen, 0x1234);
	put16p(packet, &plen, 0x8180);
	put16p(packet, &plen, 1);
	put16p(packet, &plen, 2);
	put16p(packet, &plen, 0);
	put16p(packet, &plen, 0);
	packet[plen++] = 7;
	memcpy(packet + plen, "example", 7); plen += 7;
	packet[plen++] = 3;
	memcpy(packet + plen, "com", 3); plen += 3;
	packet[plen++] = 0;
	put16p(packet, &plen, 1);
	put16p(packet, &plen, 1);
	packet[plen++] = 0xc0;
	packet[plen++] = 12;
	put16p(packet, &plen, 5);
	put16p(packet, &plen, 1);
	put32p(packet, &plen, 30);
	put16p(packet, &plen, 0);
	packet[plen++] = 0xc0;
	packet[plen++] = 12;
	put16p(packet, &plen, 1);
	put16p(packet, &plen, 1);
	put32p(packet, &plen, 60);
	put16p(packet, &plen, 4);
	packet[plen++] = 192; packet[plen++] = 0; packet[plen++] = 2; packet[plen++] = 1;
	expect(fl_dns_parse(packet, plen, 0x1234, &reply), "dns parse");
	expect(reply.rcode == 0, "dns rcode");
	expect(reply.saw_cname, "dns cname");
	expect(reply.answer_count >= 1, "dns answers");
	expect_str(reply.answers[0].address, "192.0.2.1", "dns address");
	expect(reply.answers[0].ttl == 60, "dns ttl");
	expect(!fl_dns_parse(packet, plen, 1, &reply), "dns id");
	expect(fl_dns_query(1, "*.example.com", FL_DNS_A, 1, query, (int)sizeof(query)) == 0, "dns query wildcard");
	qlen = fl_dns_query(7, "Example.com", FL_DNS_A, 1, query, (int)sizeof(query));
	expect(qlen > 0, "dns query");
	{
		saw = 0;

		for (i = 0; i < qlen; i++) {
			if (query[i] == 41) {
				saw = 1;
			}
		}

		expect(saw, "dns edns");
	}

	fl_doc_free(&doc);
	fl_doc_parse(&doc, awg_src);
	expect(fl_amnezia_is(&doc), "detect awg");
	ni = fl_config_issues(&doc, issues, 16);
	expect(issue_has(issues, ni, "Header protection"), "header protection needs padding");
	{
		iface = fl_doc_ensure_interface(&doc);
		fl_doc_set(&doc, iface, "S3", "12");
		fl_doc_set(&doc, iface, "S4", "12");
		ni = fl_config_issues(&doc, issues, 16);
		expect(ni == 0, "awg config");

		if (ni) {
			for (i = 0; i < ni; i++) {
				fprintf(stderr, "  issue: %s\n", issues[i]);
			}
		}

		memset(endpoints, 0, sizeof(endpoints));
		fl_doc_peers(&doc, peers, 4);
		endpoints[peers[0]] = "203.0.113.10:39743";
		expect(fl_uapi_set(&doc, endpoints, message, (int)sizeof(message)), "awg uapi");
		expect(contains(message, "jc=4"), "uapi jc");
		expect(contains(message, "jmin=40"), "uapi jmin");
		expect(contains(message, "h1=100-200"), "uapi h1");
		expect(contains(message, "i1=<b 0x0102><r 4>"), "uapi i1");
		expect(contains(message, "header_protection_key=") && strstr(message, zeros), "uapi header key");
		expect(contains(message, "random_trailers=true"), "uapi trailers");
		expect(contains(message, "persistent_keepalive_interval=20-30"), "uapi keepalive range");
		expect(strstr(message, "jc=") < strstr(message, "public_key="), "awg before peer");
		fl_doc_set(&doc, iface, "Jmin", "80");
		ni = fl_config_issues(&doc, issues, 16);
		expect(issue_has(issues, ni, "Jmin"), "jmin above jmax");
		fl_doc_set(&doc, iface, "Jmin", "40");
		fl_doc_set(&doc, iface, "H2", "");
		fl_doc_set(&doc, iface, "H3", "");
		fl_doc_set(&doc, iface, "H4", "");
		fl_doc_set(&doc, iface, "H1", "1-4");
		ni = fl_config_issues(&doc, issues, 16);
		expect(issue_has(issues, ni, "must not overlap"), "header overlap");
	}

	snprintf(share_json, sizeof(share_json),
		"{\"description\":\"Office\",\"hostName\":\"203.0.113.10\",\"dns1\":\"1.1.1.1\",\"dns2\":\"1.0.0.1\","
		"\"password\":\"should-not-leak\",\"userName\":\"root\",\"defaultContainer\":\"amnezia-awg\","
		"\"containers\":[{\"container\":\"amnezia-awg\",\"awg\":{\"port\":39743,\"last_config\":"
		"\"{\\\"client_priv_key\\\":\\\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\\\","
		"\\\"client_ip\\\":\\\"10.8.1.3\\\","
		"\\\"server_pub_key\\\":\\\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\\\","
		"\\\"Jc\\\":\\\"4\\\",\\\"Jmin\\\":\\\"40\\\",\\\"Jmax\\\":\\\"70\\\","
		"\\\"S1\\\":\\\"15\\\",\\\"S2\\\":\\\"18\\\","
		"\\\"H1\\\":\\\"100-200\\\",\\\"H2\\\":\\\"201-300\\\",\\\"H3\\\":\\\"301-400\\\",\\\"H4\\\":\\\"401-500\\\","
		"\\\"I1\\\":\\\"<b 0x0102><r 4>\\\","
		"\\\"allowed_ips\\\":[\\\"0.0.0.0/0\\\",\\\"::/0\\\"],"
		"\\\"persistent_keep_alive\\\":\\\"25\\\","
		"\\\"hostName\\\":\\\"203.0.113.10\\\",\\\"port\\\":39743}\"}}]}");
	packed_n = fairlead_qcompress((const unsigned char *)share_json, strlen(share_json), packed, sizeof(packed));
	expect(packed_n > 0, "qcompress");
	{
		/* base64url of the compressed blob, wrapped as vpn:// */
		tn = 0;

		for (i = 0; i < packed_n; i += 3) {
			v = (unsigned int)packed[i] << 16;
			rem = (int)(packed_n - i);

			if (rem > 1) {
				v |= (unsigned int)packed[i + 1] << 8;
			}

			if (rem > 2) {
				v |= packed[i + 2];
			}

			token[tn++] = tab[(v >> 18) & 63];
			token[tn++] = tab[(v >> 12) & 63];
			token[tn++] = rem > 1 ? tab[(v >> 6) & 63] : '=';
			token[tn++] = rem > 2 ? tab[v & 63] : '=';
		}

		token[tn] = 0;

		for (i = 0; token[i]; i++) {
			if (token[i] == '+') {
				token[i] = '-';
			} else if (token[i] == '/') {
				token[i] = '_';
			}
		}

		while (tn > 0 && token[tn - 1] == '=') {
			token[--tn] = 0;
		}

		snprintf(wrapped, sizeof(wrapped), "vpn://\n%s", token);
		irc = fl_import_normalize(wrapped, imported, (int)sizeof(imported), imported_name, (int)sizeof(imported_name));
		expect(irc == FL_IMPORT_WG, "vpn share");
		expect_str(imported_name, "Office", "share name");
		expect(contains(imported, "Jc = 4"), "share jc");
		expect(contains(imported, "I1 = <b 0x0102><r 4>"), "share i1");
		expect(contains(imported, "Address = 10.8.1.3/32"), "share address");
		expect(contains(imported, "Endpoint = 203.0.113.10:39743"), "share endpoint");
		expect(contains(imported, "DNS = 1.1.1.1, 1.0.0.1"), "share dns");
		expect(!contains(imported, "should-not-leak"), "share drops password");
		fl_doc_free(&doc);
		fl_doc_parse(&doc, imported);
		ni = fl_config_issues(&doc, issues, 16);
		expect(ni == 0, "share config");

		if (ni) {
			for (k = 0; k < ni; k++) {
				fprintf(stderr, "  share issue: %s\n", issues[k]);
			}

			fprintf(stderr, "%s\n", imported);
		}
	}

	irc = fl_import_normalize(xray, imported, (int)sizeof(imported), imported_name, (int)sizeof(imported_name));
	expect(irc == FL_IMPORT_UNSUPPORTED, "xray share");
	irc = fl_import_normalize("not a share", imported, (int)sizeof(imported), imported_name, (int)sizeof(imported_name));
	expect(irc == FL_IMPORT_WG, "plain kind");
	expect_str(imported, "not a share", "plain text stays");

	fl_doc_free(&doc);

	if (s_failures == 0) {
		printf("ok\n");
		return 0;
	}

	fprintf(stderr, "%d failed\n", s_failures);
	return 1;
}

/*
===============
list_has
True when the list contains text.
===============
*/
static int list_has(char list[][64], int n, const char *item) {
	int		i;

	for (i = 0; i < n; i++) {
		if (strcmp(list[i], item) == 0) {
			return 1;
		}
	}

	return 0;
}

/*
===============
contains_list
True when every needle is in the list.
===============
*/
static int contains_list(const fl_bypass_t *plan, const char *item) {
	return list_has((char (*)[64])plan->routes, plan->route_count, item);
}

/*
===============
put16p
Test helper. Big-endian 16 bits.
===============
*/
static void put16p(unsigned char *p, int *n, unsigned int v) {
	p[(*n)++] = (unsigned char)((v >> 8) & 0xff);
	p[(*n)++] = (unsigned char)(v & 0xff);
}

/*
===============
put32p
Test helper. Big-endian 32 bits.
===============
*/
static void put32p(unsigned char *p, int *n, unsigned int v) {
	p[(*n)++] = (unsigned char)((v >> 24) & 0xff);
	p[(*n)++] = (unsigned char)((v >> 16) & 0xff);
	p[(*n)++] = (unsigned char)((v >> 8) & 0xff);
	p[(*n)++] = (unsigned char)(v & 0xff);
}

size_t fairlead_qcompress(const unsigned char *src, size_t src_len, unsigned char *dst, size_t dst_cap);
