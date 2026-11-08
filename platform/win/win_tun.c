//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// win_tun.c -- One userspace tunnel on Windows. The helper owns the adapter, the routes, and DNS.
//
//===============================================================

#include "win_tun.h"

#include "../../core/fl.h"
#include "../../core/fl_doc.h"
#include "../../core/fl_ip.h"
#include "../../core/fl_uapi.h"
#include "../../core/fl_amnezia.h"
#include "../../core/fl_bypass.h"
#include "../../core/fl_keys.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

typedef DWORD (WINAPI *set_dns_fn)(GUID *iface, const void *settings);

typedef struct dns_settings_s {
	ULONG Version;
	ULONGLONG Flags;
	wchar_t *Domain;
	wchar_t *NameServer;
	wchar_t *SearchList;
	ULONG RegistrationEnabled;
	ULONG RegisterAdapterName;
	ULONG EnableLLMNR;
	ULONG QueryAdapterName;
	wchar_t *ProfileNameServer;

} dns_settings_t;

#define DNS_VER 1
#define DNS_F_NAMESERVER 0x2

typedef struct route_rec_s {
	char cidr[64];
	int via_tunnel;

} route_rec_t;

typedef struct session_s {
	int active;
	char slug[32];
	char iface[64];
	char config_path[MAX_PATH];
	PROCESS_INFORMATION proc;
	int proc_open;
	NET_LUID tun_luid;
	NET_LUID phys_luid;
	int have_tun;
	int have_phys;
	char gateway_v4[64];
	char gateway_iface[128];
	char phys_ip[64];

	char lan[8][64];
	int lan_count;
	char endpoints[4][64];
	int endpoint_count;
	route_rec_t routes[80];
	int route_count;
	char owned[48][64];
	int owned_count;
	char dns_backup[8][64];
	int dns_backup_count;
	GUID phys_guid;
	int dns_set;
	char dns_servers[4][64];
	int dns_server_count;
	int capture;
	char binary[32];
	char warnings[12][256];
	int warning_count;
} session_t;

static session_t s_session;
static CRITICAL_SECTION s_lock;
static int s_lock_ready;

/*
===============
lock
The tunnel lock. The first caller creates it.
===============
*/
static void lock(void) {
	if (!s_lock_ready) {
		InitializeCriticalSection(&s_lock);
		s_lock_ready = 1;
	}

	EnterCriticalSection(&s_lock);
}

/*
===============
unlock
Leave the tunnel lock.
===============
*/
static void unlock(void) {
	LeaveCriticalSection(&s_lock);
}

/*
===============
log_line
One line in ProgramData\Fairlead\helper.log.
===============
*/
static void log_line(const char *text) {
	char	path[MAX_PATH];
	FILE	*f;
	DWORD	n;

	n = GetEnvironmentVariableA("ProgramData", path, MAX_PATH);

	if (n == 0 || n >= MAX_PATH) {
		return;
	}

	strncat(path, "\\Fairlead", MAX_PATH - strlen(path) - 1);
	CreateDirectoryA(path, NULL);
	strncat(path, "\\helper.log", MAX_PATH - strlen(path) - 1);
	f = fopen(path, "a");

	if (!f) {
		return;
	}

	fprintf(f, "%s\n", text);
	fclose(f);
}

/*
===============
data_path
ProgramData\Fairlead\leaf.
===============
*/
static int data_path(char *path, int cap, const char *leaf) {
	DWORD	n;

	n = GetEnvironmentVariableA("ProgramData", path, (DWORD)cap);

	if (n == 0 || (int)n >= cap - 32) {
		return 0;
	}

	strncat(path, "\\Fairlead", (size_t)cap - strlen(path) - 1);
	CreateDirectoryA(path, NULL);
	strncat(path, "\\", (size_t)cap - strlen(path) - 1);
	strncat(path, leaf, (size_t)cap - strlen(path) - 1);
	return 1;
}

/*
===============
guid_text
Canonical GUID text.
===============
*/
static void guid_text(const GUID *id, char *out, int cap) {
	snprintf(out, (size_t)cap, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		id->Data1, id->Data2, id->Data3,
		id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3],
		id->Data4[4], id->Data4[5], id->Data4[6], id->Data4[7]);
}

/*
===============
guid_read
Parse a GUID string.
===============
*/
static int guid_read(const char *text, GUID *id) {
	unsigned	d1;
	unsigned	d2;
	unsigned	d3;
	unsigned	b[8];
	int			i;

	if (sscanf(text, "{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}",
		&d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]) != 11) {
		return 0;
	}

	id->Data1 = d1;
	id->Data2 = (unsigned short)d2;
	id->Data3 = (unsigned short)d3;

	for (i = 0; i < 8; i++) {
		id->Data4[i] = (unsigned char)b[i];
	}

	return 1;
}

/*
===============
save_session
Remember the live adapter so a restarted helper can tear it down.
===============
*/
static void save_session(void) {
	char	path[MAX_PATH];
	char	guid[80];
	FILE	*f;
	int		i;

	if (!data_path(path, MAX_PATH, "session.txt")) {
		return;
	}

	f = fopen(path, "wb");

	if (!f) {
		return;
	}

	fprintf(f, "pid %lu\n", s_session.proc_open ? (unsigned long)s_session.proc.dwProcessId : 0ul);
	fprintf(f, "exe %s\n", s_session.binary);
	fprintf(f, "iface %s\n", s_session.iface);
	guid_text(&s_session.phys_guid, guid, (int)sizeof(guid));
	fprintf(f, "guid %s\n", guid);
	fprintf(f, "dns %d\n", s_session.dns_set);

	for (i = 0; i < s_session.dns_backup_count; i++) {
		fprintf(f, "backup %s\n", s_session.dns_backup[i]);
	}

	for (i = 0; i < s_session.route_count; i++) {
		fprintf(f, "route %d %s\n", s_session.routes[i].via_tunnel, s_session.routes[i].cidr);
	}

	fclose(f);
}

/*
===============
clear_session_file
Forget the saved adapter.
===============
*/
static void clear_session_file(void) {
	char	path[MAX_PATH];

	if (data_path(path, MAX_PATH, "session.txt")) {
		DeleteFileA(path);
	}
}

/*
===============
file_exists
True when path is a file.
===============
*/
static int file_exists(const char *path) {
	DWORD	a;

	a = GetFileAttributesA(path);
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/*
===============
copy_result
Fill a status struct from the live session.
===============
*/
static void copy_result(win_tun_result_t *out) {
	int		i;

	memset(out, 0, sizeof(*out));
	out->ok = s_session.active;
	snprintf(out->iface, sizeof(out->iface), "%s", s_session.iface);
	snprintf(out->gateway_v4, sizeof(out->gateway_v4), "%s", s_session.gateway_v4);
	snprintf(out->gateway_iface, sizeof(out->gateway_iface), "%s", s_session.gateway_iface);
	out->lan_count = s_session.lan_count;

	for (i = 0; i < s_session.lan_count && i < 8; i++) {
		snprintf(out->lan[i], 64, "%s", s_session.lan[i]);
	}

	out->endpoint_count = s_session.endpoint_count;

	for (i = 0; i < s_session.endpoint_count && i < 4; i++) {
		snprintf(out->endpoints[i], 64, "%s", s_session.endpoints[i]);
	}

	out->owned_count = s_session.owned_count;

	for (i = 0; i < s_session.owned_count && i < 48; i++) {
		snprintf(out->owned[i], 64, "%s", s_session.owned[i]);
	}

	out->warning_count = s_session.warning_count;

	if (out->warning_count > 12) {
		out->warning_count = 12;
	}

	for (i = 0; i < out->warning_count; i++) {
		snprintf(out->warnings[i], 256, "%s", s_session.warnings[i]);
	}
}

/*
===============
find_default
The physical default route: LUID, gateway, name, and local address.
===============
*/
static int find_default(NET_LUID *luid, char *gw, int gw_cap, char *name, int name_cap, char *local_ip, int local_cap, GUID *guid) {
	ULONG				i;
	ULONG				best;
	int					found;
	MIB_IPFORWARD_ROW2	*row;
	MIB_IPFORWARD_ROW2	*r;
	char				ip[64];
	wchar_t				alias[256];
	ULONG				size;
	struct sockaddr_in	*sin;
	ULONG				prefix;

	PMIB_IPFORWARD_TABLE2 table = NULL;
	best = 0xffffffffu;
	found = 0;
	row = NULL;

	if (GetIpForwardTable2(AF_INET, &table) != NO_ERROR) {
		return 0;
	}

	for (i = 0; i < table->NumEntries; i++) {
		r = &table->Table[i];

		if (r->DestinationPrefix.PrefixLength != 0) {
			continue;
		}

		if (r->NextHop.si_family != AF_INET) {
			continue;
		}

		if (r->NextHop.Ipv4.sin_addr.S_un.S_addr == 0) {
			continue;
		}

		if (r->Metric < best) {
			best = r->Metric;
			row = r;
			found = 1;
		}
	}

	if (found) {
		*luid = row->InterfaceLuid;
		inet_ntop(AF_INET, &row->NextHop.Ipv4.sin_addr, ip, sizeof(ip));
		snprintf(gw, (size_t)gw_cap, "%s", ip);
		ConvertInterfaceLuidToGuid(luid, guid);
		{
			if (ConvertInterfaceLuidToAlias(luid, alias, 256) == NO_ERROR && name && name_cap > 0) {
				WideCharToMultiByte(CP_UTF8, 0, alias, -1, name, name_cap, NULL, NULL);
			}
		}

		if (local_ip && local_cap) {
			local_ip[0] = '\0';
		}

		{
			size = 0;
			IP_ADAPTER_ADDRESSES *addrs = NULL;

			GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, NULL, &size);
			addrs = (IP_ADAPTER_ADDRESSES *)malloc(size);

			if (addrs && GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, addrs, &size) == NO_ERROR) {
				IP_ADAPTER_ADDRESSES *a;

				for (a = addrs; a; a = a->Next) {
					if (memcmp(&a->Luid, luid, sizeof(*luid)) != 0) {
						continue;
					}

					if (a->FirstUnicastAddress) {
						sin = (struct sockaddr_in *)a->FirstUnicastAddress->Address.lpSockaddr;
						inet_ntop(AF_INET, &sin->sin_addr, local_ip, (size_t)local_cap);
					}

					{
						IP_ADAPTER_UNICAST_ADDRESS *u;

						s_session.lan_count = 0;

						for (u = a->FirstUnicastAddress; u && s_session.lan_count < 8; u = u->Next) {
							sin = (struct sockaddr_in *)u->Address.lpSockaddr;
							fl_ip_t ip;
							fl_cidr_t cidr;
							char text[64];
							prefix = u->OnLinkPrefixLength;

							if (sin->sin_family != AF_INET) {
								continue;
							}

							inet_ntop(AF_INET, &sin->sin_addr, text, sizeof(text));

							if (!fl_ip_parse(text, &ip)) {
								continue;
							}

							if (prefix == 0 || prefix >= 32) {
								continue;
							}

							fl_ip_mask(&ip, (int)prefix);
							cidr.ip = ip;
							cidr.prefix = (int)prefix;
							fl_cidr_format(&cidr, text, (int)sizeof(text));
							snprintf(s_session.lan[s_session.lan_count], 64, "%s", text);
							s_session.lan_count++;
						}
					}
				}
			}

			free(addrs);
		}
	}

	FreeMibTable(table);
	return found;
}

/*
===============
resolve_host
One numeric address for a host. IPv4 wins when both exist.
===============
*/
static int resolve_host(const char *host, char *out, int cap) {
	struct addrinfo			hints;
	struct addrinfo			*info;
	struct addrinfo			*it;
	char					v4[64];
	char					v6[64];
	fl_ip_t					ip;
	struct sockaddr_in		*sin4;
	struct sockaddr_in6		*sin6;

	info = NULL;

	if (fl_ip_parse(host, &ip)) {
		fl_ip_format(&ip, out, cap);
		return 1;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_family = AF_UNSPEC;

	if (getaddrinfo(host, NULL, &hints, &info) != 0) {
		return 0;
	}

	v4[0] = v6[0] = '\0';

	for (it = info; it; it = it->ai_next) {
		if (it->ai_family == AF_INET && !v4[0]) {
			sin4 = (struct sockaddr_in *)it->ai_addr;
			inet_ntop(AF_INET, &sin4->sin_addr, v4, sizeof(v4));
		} else if (it->ai_family == AF_INET6 && !v6[0]) {
			sin6 = (struct sockaddr_in6 *)it->ai_addr;
			inet_ntop(AF_INET6, &sin6->sin6_addr, v6, sizeof(v6));
		}
	}

	freeaddrinfo(info);

	if (v4[0]) {
		snprintf(out, (size_t)cap, "%s", v4);
		return 1;
	}

	if (v6[0]) {
		snprintf(out, (size_t)cap, "%s", v6);
		return 1;
	}

	return 0;
}

/*
===============
adapter_luid
LUID of an adapter alias.
===============
*/
static int adapter_luid(const char *name, NET_LUID *luid) {
	wchar_t	wide[256];

	MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 256);
	return ConvertInterfaceAliasToLuid(wide, luid) == NO_ERROR;
}

/*
===============
ip_ready
True when this alias has an IPv4 interface.
===============
*/
static int ip_ready(const wchar_t *alias, NET_LUID *luid) {
	MIB_IPINTERFACE_ROW	row;

	if (ConvertInterfaceAliasToLuid(alias, luid) != NO_ERROR) {
		return 0;
	}

	InitializeIpInterfaceEntry(&row);
	row.InterfaceLuid = *luid;
	row.Family = AF_INET;
	return GetIpInterfaceEntry(&row) == NO_ERROR;
}

/*
===============
find_tun
The live adapter for this tunnel. A leftover copy is named with a trailing number.
===============
*/
static int find_tun(const char *name, NET_LUID *luid) {
	wchar_t	want[256];
	wchar_t	alt[256];
	int		n;

	if (!MultiByteToWideChar(CP_UTF8, 0, name, -1, want, 256)) {
		return 0;
	}

	if (ip_ready(want, luid)) {
		return 1;
	}

	for (n = 1; n <= 8; n++) {
		_snwprintf(alt, 256, L"%s %d", want, n);

		if (ip_ready(alt, luid)) {
			return 1;
		}
	}

	return 0;
}

/*
===============
wait_adapter
Wait until the userspace adapter has a LUID.
===============
*/
static int wait_adapter(const char *name, NET_LUID *luid) {
	int		i;

	for (i = 0; i < 80; i++) {
		if (adapter_luid(name, luid) || find_tun(name, luid)) {
			return 1;
		}

		Sleep(100);
	}

	return 0;
}

/*
===============
uapi_exchange
Send one userspace message and read the reply.
===============
*/
static int uapi_exchange(const char *iface, const char *message, char *reply, int reply_cap) {
	wchar_t	path[512];
	wchar_t	wiface[128];
	HANDLE	pipe;
	DWORD	wrote;
	DWORD	got;

	wrote = 0;
	got = 0;

	MultiByteToWideChar(CP_UTF8, 0, iface, -1, wiface, 128);
	_snwprintf(path, 512, L"\\\\.\\pipe\\ProtectedPrefix\\Administrators\\AmneziaWG\\%s", wiface);
	pipe = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (pipe == INVALID_HANDLE_VALUE) {
		return 0;
	}

	if (!WriteFile(pipe, message, (DWORD)strlen(message), &wrote, NULL)) {
		CloseHandle(pipe);
		return 0;
	}

	if (reply && reply_cap > 0) {
		if (!ReadFile(pipe, reply, (DWORD)reply_cap - 1, &got, NULL)) {
			got = 0;
		}

		reply[got] = '\0';
	}

	CloseHandle(pipe);
	return 1;
}

/*
===============
spawn_go
Start amneziawg-go on an interface name. Plain WireGuard uses it too.
===============
*/
static int spawn_go(const char *exe_dir, const char *binary_name, const char *iface, PROCESS_INFORMATION *pi) {
	char				binary[MAX_PATH];
	char				cmd[1024];
	char				log_path[MAX_PATH];
	SECURITY_ATTRIBUTES	sa;
	HANDLE				logf;
	DWORD				n;
	char				msg[192];

	STARTUPINFOA si;

	snprintf(binary, sizeof(binary), "%s\\%s", exe_dir, binary_name);

	if (!file_exists(binary)) {
		return 0;
	}

	n = GetEnvironmentVariableA("ProgramData", log_path, MAX_PATH);

	if (n == 0) {
		snprintf(log_path, sizeof(log_path), "C:\\ProgramData");
	}

	strncat(log_path, "\\Fairlead", sizeof(log_path) - strlen(log_path) - 1);
	CreateDirectoryA(log_path, NULL);
	strncat(log_path, "\\amneziawg-go.log", sizeof(log_path) - strlen(log_path) - 1);
	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	logf = CreateFileA(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	/* The Windows daemon takes only the interface name. -f is the Unix flag and makes it exit. */
	si.hStdInput = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
	si.hStdOutput = logf == INVALID_HANDLE_VALUE ? CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL) : logf;
	si.hStdError = si.hStdOutput;
	snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", binary, iface);
	memset(pi, 0, sizeof(*pi));

	if (!CreateProcessA(binary, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, exe_dir, &si, pi)) {
		snprintf(msg, sizeof(msg), "fairlead: could not start %s (%lu)", binary_name, (unsigned long)GetLastError());
		log_line(msg);

		if (si.hStdInput != INVALID_HANDLE_VALUE) {
			CloseHandle(si.hStdInput);
		}

		if (logf != INVALID_HANDLE_VALUE) {
			CloseHandle(logf);
		} else if (si.hStdOutput != INVALID_HANDLE_VALUE) {
			CloseHandle(si.hStdOutput);
		}

		return 0;
	}

	if (si.hStdInput != INVALID_HANDLE_VALUE) {
		CloseHandle(si.hStdInput);
	}

	if (logf != INVALID_HANDLE_VALUE) {
		CloseHandle(logf);
	} else if (si.hStdOutput != INVALID_HANDLE_VALUE) {
		CloseHandle(si.hStdOutput);
	}

	return 1;
}

/*
===============
stop_proc
Stop the userspace daemon.
===============
*/
static void stop_proc(void) {
	if (!s_session.proc_open) {
		return;
	}

	TerminateProcess(s_session.proc.hProcess, 0);
	WaitForSingleObject(s_session.proc.hProcess, 3000);
	CloseHandle(s_session.proc.hProcess);
	CloseHandle(s_session.proc.hThread);
	s_session.proc_open = 0;
	memset(&s_session.proc, 0, sizeof(s_session.proc));
}

/*
===============
add_address
Assign a CIDR on the adapter. The host bits are kept. The LUID is read again each try, because Wintun may replace a leftover adapter after the daemon starts.
===============
*/
static int add_address(const char *iface, const char *cidr_text) {
	fl_cidr_t					cidr;
	MIB_UNICASTIPADDRESS_ROW	row;
	char						ip[64];
	char						raw[128];
	char						*slash;
	int							attempt;
	DWORD						err;
	char						msg[192];

	if (!fl_cidr_parse(cidr_text, &cidr)) {
		return 0;
	}

	/* The adapter address is the host, not the masked network. */
	{
		snprintf(raw, sizeof(raw), "%s", cidr_text);
		fl_trim(raw);
		slash = strchr(raw, '/');

		if (slash) {
			*slash = '\0';
		}

		snprintf(ip, sizeof(ip), "%s", raw);
	}

	{
		err = ERROR_NOT_FOUND;

		ADDRESS_FAMILY family = cidr.ip.v6 ? AF_INET6 : AF_INET;

		for (attempt = 0; attempt < 80; attempt++) {
			NET_LUID luid;
			MIB_IPINTERFACE_ROW iface_row;
			NET_IFINDEX index = 0;

			if (!find_tun(iface, &luid)) {
				Sleep(100);
				continue;
			}

			if (family == AF_INET6) {
				InitializeIpInterfaceEntry(&iface_row);
				iface_row.InterfaceLuid = luid;
				iface_row.Family = AF_INET6;

				if (GetIpInterfaceEntry(&iface_row) != NO_ERROR) {
					Sleep(100);
					continue;
				}
			}

			if (ConvertInterfaceLuidToIndex(&luid, &index) != NO_ERROR) {
				Sleep(100);
				continue;
			}

			s_session.tun_luid = luid;
			s_session.have_tun = 1;
			InitializeUnicastIpAddressEntry(&row);
			row.InterfaceLuid = luid;
			row.InterfaceIndex = index;
			row.PrefixOrigin = IpPrefixOriginManual;
			row.SuffixOrigin = IpSuffixOriginManual;
			row.ValidLifetime = 0xffffffff;
			row.PreferredLifetime = 0xffffffff;
			row.DadState = IpDadStatePreferred;
			row.OnLinkPrefixLength = (UINT8)cidr.prefix;

			if (family == AF_INET) {
				row.Address.Ipv4.sin_family = AF_INET;

				if (inet_pton(AF_INET, ip, &row.Address.Ipv4.sin_addr) != 1) {
					return 0;
				}
			} else {
				row.Address.Ipv6.sin6_family = AF_INET6;

				if (inet_pton(AF_INET6, ip, &row.Address.Ipv6.sin6_addr) != 1) {
					return 0;
				}
			}

			err = CreateUnicastIpAddressEntry(&row);

			if (err == NO_ERROR || err == ERROR_OBJECT_ALREADY_EXISTS) {
				return 1;
			}

			if (err != ERROR_NOT_FOUND) {
				break;
			}

			Sleep(100);
		}

		{
			snprintf(msg, sizeof(msg), "fairlead: address %s failed (%lu)", ip, (unsigned long)err);
			log_line(msg);
		}

		return 0;
	}
}

/*
===============
set_mtu
Set the adapter MTU.
===============
*/
static int set_mtu(NET_LUID luid, int mtu) {
	MIB_IPINTERFACE_ROW	row;

	InitializeIpInterfaceEntry(&row);
	row.InterfaceLuid = luid;
	row.Family = AF_INET;

	if (GetIpInterfaceEntry(&row) != NO_ERROR) {
		return 0;
	}

	row.NlMtu = (ULONG)mtu;
	row.UseAutomaticMetric = FALSE;
	row.Metric = 5;
	row.SitePrefixLength = 0;
	return SetIpInterfaceEntry(&row) == NO_ERROR;
}

/*
===============
route_row
Fill a route row for cidr via gateway, or on-link.
===============
*/
static int route_row(NET_LUID luid, const char *cidr_text, const char *gateway, MIB_IPFORWARD_ROW2 *row) {
	fl_cidr_t	cidr;
	char		dest[64];

	if (!fl_cidr_parse(cidr_text, &cidr) || cidr.ip.v6) {
		return 0;
	}

	fl_ip_format(&cidr.ip, dest, (int)sizeof(dest));
	InitializeIpForwardEntry(row);
	row->InterfaceLuid = luid;
	row->DestinationPrefix.Prefix.si_family = AF_INET;
	row->DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;

	if (inet_pton(AF_INET, dest, &row->DestinationPrefix.Prefix.Ipv4.sin_addr) != 1) {
		return 0;
	}

	row->DestinationPrefix.PrefixLength = (UINT8)cidr.prefix;
	row->NextHop.si_family = AF_INET;
	row->NextHop.Ipv4.sin_family = AF_INET;

	if (gateway && gateway[0]) {
		if (inet_pton(AF_INET, gateway, &row->NextHop.Ipv4.sin_addr) != 1) {
			return 0;
		}
	}

	row->Metric = 5;
	return 1;
}

/*
===============
add_route
Install a route and remember it.
===============
*/
static int add_route(const char *cidr_text, int via_tunnel) {
	MIB_IPFORWARD_ROW2	row;
	NET_LUID			luid;
	const char			*gw;
	int					i;

	gw = NULL;

	if (!via_tunnel) {
		if (!s_session.have_phys || !s_session.gateway_v4[0]) {
			return 0;
		}

		luid = s_session.phys_luid;
		gw = s_session.gateway_v4;
	} else {
		if (!s_session.have_tun) {
			return 0;
		}

		luid = s_session.tun_luid;
	}

	if (!route_row(luid, cidr_text, gw, &row)) {
		return 0;
	}

	if (CreateIpForwardEntry2(&row) != NO_ERROR && GetLastError() != ERROR_OBJECT_ALREADY_EXISTS) {
		return 0;
	}

	for (i = 0; i < s_session.route_count; i++) {
		if (strcmp(s_session.routes[i].cidr, cidr_text) == 0 && s_session.routes[i].via_tunnel == via_tunnel) {
			return 1;
		}
	}

	if (s_session.route_count < 80) {
		snprintf(s_session.routes[s_session.route_count].cidr, 64, "%s", cidr_text);
		s_session.routes[s_session.route_count].via_tunnel = via_tunnel;
		s_session.route_count++;
	}

	return 1;
}

/*
===============
del_route_at
Delete one remembered route and close the gap.
===============
*/
static void del_route_at(int index) {
	MIB_IPFORWARD_ROW2	row;
	NET_LUID			luid;
	const char			*gw;

	luid = s_session.routes[index].via_tunnel ? s_session.tun_luid : s_session.phys_luid;
	gw = s_session.routes[index].via_tunnel ? NULL : s_session.gateway_v4;

	if (route_row(luid, s_session.routes[index].cidr, gw, &row)) {
		DeleteIpForwardEntry2(&row);
	}

	memmove(&s_session.routes[index], &s_session.routes[index + 1], (size_t)(s_session.route_count - index - 1) * sizeof(s_session.routes[0]));
	s_session.route_count--;
}

/*
===============
delete_matching
Delete remembered routes with this CIDR.
===============
*/
static void delete_matching(const char *cidr, int via_tunnel) {
	int		i;

	for (i = 0; i < s_session.route_count; ) {
		if (strcmp(s_session.routes[i].cidr, cidr) == 0 && s_session.routes[i].via_tunnel == via_tunnel) {
			del_route_at(i);
		} else {
			i++;
		}
	}
}

/*
===============
is_half
One of the four /1 networks a default route is split into.
===============
*/
static int is_half(const char *dest) {
	return strcmp(dest, "0.0.0.0/1") == 0 || strcmp(dest, "128.0.0.0/1") == 0
		|| strcmp(dest, "::/1") == 0 || strcmp(dest, "8000::/1") == 0;
}

/*
===============
sync_capture
Capture-all installs the /1 halves. The other mode routes only the tunnel DNS.
===============
*/
static void sync_capture(const fl_doc_t *doc, int capture) {
	int		n;
	int		i;

	char families[16][8];
	char dests[16][64];
	n = fl_tunnel_destinations(doc, families, dests, 16);
	s_session.capture = capture;

	if (capture) {
		for (i = 0; i < n; i++) {
			if (!is_half(dests[i])) {
				continue;
			}

			if (strcmp(families[i], "inet") != 0) {
				continue;
			}

			add_route(dests[i], 1);
		}
	} else {
		for (i = 0; i < s_session.route_count; ) {
			if (s_session.routes[i].via_tunnel && is_half(s_session.routes[i].cidr)) {
				del_route_at(i);
			} else {
				i++;
			}
		}

		for (i = 0; i < s_session.dns_server_count; i++) {
			char dest[80];

			if (strchr(s_session.dns_servers[i], ':')) {
				continue;
			}

			snprintf(dest, sizeof(dest), "%s/32", s_session.dns_servers[i]);
			add_route(dest, 1);
		}
	}
}

/*
===============
apply_dns
Point the physical adapter at the tunnel DNS and remember the old servers.
===============
*/
static void apply_dns(void) {
	set_dns_fn		fn;
	wchar_t			joined[512];
	dns_settings_t	settings;
	HMODULE			mod;
	int				i;
	ULONG			size;

	IP_ADAPTER_ADDRESSES *addrs = NULL;
	size = 0;

	if (s_session.dns_server_count == 0 || !s_session.have_phys) {
		return;
	}

	joined[0] = 0;

	for (i = 0; i < s_session.dns_server_count; i++) {
		wchar_t one[64];

		MultiByteToWideChar(CP_UTF8, 0, s_session.dns_servers[i], -1, one, 64);

		if (joined[0]) {
			wcscat(joined, L" ");
		}

		wcscat(joined, one);
	}

	GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, NULL, &size);
	addrs = (IP_ADAPTER_ADDRESSES *)malloc(size);
	s_session.dns_backup_count = 0;

	if (addrs && GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, addrs, &size) == NO_ERROR) {
		IP_ADAPTER_ADDRESSES *a;

		for (a = addrs; a; a = a->Next) {
			IP_ADAPTER_DNS_SERVER_ADDRESS *d;

			if (memcmp(&a->Luid, &s_session.phys_luid, sizeof(s_session.phys_luid)) != 0) {
				continue;
			}

			for (d = a->FirstDnsServerAddress; d && s_session.dns_backup_count < 8; d = d->Next) {
				char ip[64];

				if (d->Address.lpSockaddr->sa_family == AF_INET) {
					inet_ntop(AF_INET, &((struct sockaddr_in *)d->Address.lpSockaddr)->sin_addr, ip, sizeof(ip));
				} else if (d->Address.lpSockaddr->sa_family == AF_INET6) {
					inet_ntop(AF_INET6, &((struct sockaddr_in6 *)d->Address.lpSockaddr)->sin6_addr, ip, sizeof(ip));
				} else {
					continue;
				}

				snprintf(s_session.dns_backup[s_session.dns_backup_count], 64, "%s", ip);
				s_session.dns_backup_count++;
			}
		}
	}

	free(addrs);
	mod = LoadLibraryA("iphlpapi.dll");
	fn = mod ? (set_dns_fn)GetProcAddress(mod, "SetInterfaceDnsSettings") : NULL;

	if (!fn) {
		return;
	}

	memset(&settings, 0, sizeof(settings));
	settings.Version = DNS_VER;
	settings.Flags = DNS_F_NAMESERVER;
	settings.NameServer = joined;

	if (fn(&s_session.phys_guid, &settings) == NO_ERROR) {
		s_session.dns_set = 1;
	}
}

/*
===============
restore_dns
Put the physical DNS servers back.
===============
*/
static void restore_dns(void) {
	set_dns_fn		fn;
	HMODULE			mod;
	wchar_t			joined[512];
	dns_settings_t	settings;
	int				i;

	if (!s_session.dns_set) {
		return;
	}

	joined[0] = 0;

	for (i = 0; i < s_session.dns_backup_count; i++) {
		wchar_t one[64];

		MultiByteToWideChar(CP_UTF8, 0, s_session.dns_backup[i], -1, one, 64);

		if (joined[0]) {
			wcscat(joined, L" ");
		}

		wcscat(joined, one);
	}

	mod = LoadLibraryA("iphlpapi.dll");
	fn = mod ? (set_dns_fn)GetProcAddress(mod, "SetInterfaceDnsSettings") : NULL;

	if (!fn) {
		return;
	}

	memset(&settings, 0, sizeof(settings));
	settings.Version = DNS_VER;
	settings.Flags = DNS_F_NAMESERVER;
	settings.NameServer = joined;
	fn(&s_session.phys_guid, &settings);
	s_session.dns_set = 0;
}

/*
===============
clear_routes
Delete every route this session added.
===============
*/
static void clear_routes(void) {
	while (s_session.route_count > 0) {
		del_route_at(0);
	}
}

/*
===============
mtu_of
MTU from the config, or 1420.
===============
*/
static int mtu_of(const fl_doc_t *doc) {
	int		iface;
	char	value[32];
	int		mtu;

	iface = fl_doc_interface(doc);
	mtu = 0;

	if (iface < 0) {
		return 1420;
	}

	fl_doc_value(doc, iface, "MTU", value, (int)sizeof(value));

	if (value[0]) {
		mtu = atoi(value);

		if (mtu >= 1280) {
			return mtu;
		}
	}

	return 1420;
}

/*
===============
fill_dns
DNS servers from the Interface section.
===============
*/
static void fill_dns(const fl_doc_t *doc) {
	char	dns[512];
	int		n;
	int		i;

	char items[8][256];
	s_session.dns_server_count = 0;
	fl_doc_dns(doc, dns, (int)sizeof(dns));
	n = fl_split_commas(dns, items, 8);

	for (i = 0; i < n && s_session.dns_server_count < 4; i++) {
		fl_ip_t ip;

		if (!fl_ip_parse(items[i], &ip)) {
			continue;
		}

		fl_ip_format(&ip, s_session.dns_servers[s_session.dns_server_count], 64);
		s_session.dns_server_count++;
	}
}

/*
===============
remember_owned
A CIDR the plan says this session owns.
===============
*/
static void remember_owned(const char *cidr) {
	int		i;

	for (i = 0; i < s_session.owned_count; i++) {
		if (strcmp(s_session.owned[i], cidr) == 0) {
			return;
		}
	}

	if (s_session.owned_count < 48) {
		snprintf(s_session.owned[s_session.owned_count], 64, "%s", cidr);
		s_session.owned_count++;
	}
}

/*
===============
apply_plan
Install the bypass plan on top of the current routes.
===============
*/
static void apply_plan(const char **cidrs, int cidr_count, int exclude_lan, int selected) {
	fl_bypass_t	plan;
	const char	*eps[4];
	const char	*lan[8];
	int			i;
	int			lan_count;
	int			nt;
	int			nd;
	int			k;
	int			keep;

	char desired_tunnel[48][64];
	char desired_direct[48][64];
	nt = 0;
	nd = 0;

	for (i = 0; i < s_session.endpoint_count && i < 4; i++) {
		eps[i] = s_session.endpoints[i];
	}

	/* s_session.lan is a block of strings, not an array of pointers. */
	lan_count = s_session.lan_count;

	if (lan_count > 8) {
		lan_count = 8;
	}

	for (i = 0; i < lan_count; i++) {
		lan[i] = s_session.lan[i];
	}

	fl_bypass_plan(selected, cidrs, cidr_count, NULL, 0, lan, lan_count, exclude_lan, eps, s_session.endpoint_count, &plan);
	s_session.warning_count = plan.warning_count;

	if (s_session.warning_count > 12) {
		s_session.warning_count = 12;
	}

	for (i = 0; i < s_session.warning_count; i++) {
		snprintf(s_session.warnings[i], 256, "%s", plan.warnings[i]);
	}

	if (selected) {
		nt = plan.route_count;

		for (i = 0; i < nt && i < 48; i++) {
			snprintf(desired_tunnel[i], 64, "%s", plan.routes[i]);
		}

		nd = plan.direct_count;

		for (i = 0; i < nd && i < 48; i++) {
			snprintf(desired_direct[i], 64, "%s", plan.direct[i]);
		}
	} else {
		nd = plan.route_count;

		for (i = 0; i < nd && i < 48; i++) {
			snprintf(desired_direct[i], 64, "%s", plan.routes[i]);
		}
	}

	for (i = 0; i < s_session.owned_count; ) {
		keep = 0;

		for (k = 0; k < nt; k++) {
			if (strcmp(s_session.owned[i], desired_tunnel[k]) == 0) {
				keep = 1;
			}
		}

		for (k = 0; k < nd; k++) {
			if (strcmp(s_session.owned[i], desired_direct[k]) == 0) {
				keep = 1;
			}
		}

		if (!keep) {
			delete_matching(s_session.owned[i], 0);
			delete_matching(s_session.owned[i], 1);
			memmove(&s_session.owned[i], &s_session.owned[i + 1], (size_t)(s_session.owned_count - i - 1) * 64);
			s_session.owned_count--;
		} else {
			i++;
		}
	}

	for (i = 0; i < nt; i++) {
		if (add_route(desired_tunnel[i], 1)) {
			remember_owned(desired_tunnel[i]);
		}
	}

	for (i = 0; i < nd; i++) {
		if (add_route(desired_direct[i], 0)) {
			remember_owned(desired_direct[i]);
		}
	}
}

/*
===============
read_stats
Handshake and byte counters from the userspace daemon.
===============
*/
static void read_stats(win_tun_result_t *out) {
	char				reply[4096];
	fl_device_status_t	dev;
	char				msg[] = "get=1\n\n";

	if (!s_session.active) {
		return;
	}

	if (!uapi_exchange(s_session.iface, msg, reply, (int)sizeof(reply))) {
		return;
	}

	fl_uapi_parse_get(reply, s_session.iface, &dev);

	if (dev.peer_count > 0) {
		out->rx = dev.peers[0].rx;
		out->tx = dev.peers[0].tx;
		out->handshake = dev.peers[0].handshake;
		snprintf(out->live_endpoint, sizeof(out->live_endpoint), "%s", dev.peers[0].endpoint);
	}

	snprintf(out->public_key, sizeof(out->public_key), "%s", dev.public_key);
}

/*
===============
win_tun_active
True while a tunnel is up.
===============
*/
int win_tun_active(void) {
	return s_session.active;
}

/*
===============
win_tun_status
Copy the live counters and the last error.
===============
*/
void win_tun_status(win_tun_result_t *out) {
	lock();
	copy_result(out);
	read_stats(out);
	out->ok = s_session.active;
	unlock();
}

/*
===============
session_down
Drop routes, DNS, the daemon, and the saved session.
===============
*/
static void session_down(void) {
	restore_dns();
	clear_routes();
	stop_proc();
	clear_session_file();
	s_session.active = 0;
	s_session.have_tun = 0;
	s_session.owned_count = 0;
	s_session.iface[0] = '\0';
}

/*
===============
win_tun_down
Bring the tunnel down.
===============
*/
int win_tun_down(win_tun_result_t *out) {
	lock();
	session_down();

	if (out) {
		memset(out, 0, sizeof(*out));
		out->ok = 1;
	}

	unlock();
	log_line("fairlead: tunnel down");
	return 1;
}

/*
===============
win_tun_reconcile
On helper start, drop an adapter a previous process left behind.
===============
*/
void win_tun_reconcile(void) {
	char			path[MAX_PATH];
	char			line[256];
	FILE			*f;
	unsigned long	pid;
	char			iface[64];
	GUID			guid;
	int				have_guid;
	int				dns;
	int				backup_count;
	int				via[80];
	int				route_count;
	WSADATA			wsa;
	int				flag;
	char			cidr[64];
	char			backups[8][64];
	char			routes[80][64];
	int				i;
	HANDLE			proc;
	char			image[MAX_PATH];
	DWORD			n;

	pid = 0;
	have_guid = 0;
	dns = 0;
	backup_count = 0;
	route_count = 0;

	iface[0] = '\0';
	memset(&guid, 0, sizeof(guid));

	if (!data_path(path, MAX_PATH, "session.txt")) {
		return;
	}

	f = fopen(path, "rb");

	if (!f) {
		return;
	}

	while (fgets(line, (int)sizeof(line), f)) {
		fl_trim(line);

		if (sscanf(line, "pid %lu", &pid) == 1) {
			continue;
		}

		if (strncmp(line, "iface ", 6) == 0) {
			snprintf(iface, sizeof(iface), "%s", line + 6);
		} else if (strncmp(line, "guid ", 5) == 0) {
			have_guid = guid_read(line + 5, &guid);
		} else if (sscanf(line, "dns %d", &dns) == 1) {
			continue;
		} else if (strncmp(line, "backup ", 7) == 0 && backup_count < 8) {
			snprintf(backups[backup_count], 64, "%s", line + 7);
			backup_count++;
		} else if (route_count < 80 && strncmp(line, "route ", 6) == 0) {
			flag = 0;

			if (sscanf(line + 6, "%d %63s", &flag, cidr) == 2) {
				via[route_count] = flag;
				snprintf(routes[route_count], 64, "%s", cidr);
				route_count++;
			}
		}
	}

	fclose(f);

	WSAStartup(MAKEWORD(2, 2), &wsa);
	lock();
	memset(&s_session, 0, sizeof(s_session));

	if (have_guid) {
		s_session.phys_guid = guid;
	}

	s_session.dns_backup_count = backup_count;
	{
		for (i = 0; i < backup_count; i++) {
			snprintf(s_session.dns_backup[i], 64, "%s", backups[i]);
		}
	}

	s_session.dns_set = dns && have_guid;
	restore_dns();

	if (find_default(&s_session.phys_luid, s_session.gateway_v4, (int)sizeof(s_session.gateway_v4), s_session.gateway_iface, (int)sizeof(s_session.gateway_iface), s_session.phys_ip, (int)sizeof(s_session.phys_ip), &s_session.phys_guid)) {
		s_session.have_phys = 1;

		for (i = 0; i < route_count; i++) {
			if (via[i]) {
				continue;
			}

			snprintf(s_session.routes[s_session.route_count].cidr, 64, "%s", routes[i]);
			s_session.routes[s_session.route_count].via_tunnel = 0;
			s_session.route_count++;
		}

		clear_routes();
	}

	if (iface[0] && adapter_luid(iface, &s_session.tun_luid)) {
		s_session.have_tun = 1;

		for (i = 0; i < route_count; i++) {
			if (!via[i]) {
				continue;
			}

			snprintf(s_session.routes[s_session.route_count].cidr, 64, "%s", routes[i]);
			s_session.routes[s_session.route_count].via_tunnel = 1;
			s_session.route_count++;
		}

		clear_routes();
	}

	unlock();

	if (pid > 4) {
		proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, (DWORD)pid);

		if (proc) {
			n = MAX_PATH;

			if (QueryFullProcessImageNameA(proc, 0, image, &n)) {
				/* wireguard-go.exe is a daemon left by an older build. */

				if (strstr(image, "wireguard-go.exe") || strstr(image, "amneziawg-go.exe")) {
					TerminateProcess(proc, 0);
					WaitForSingleObject(proc, 3000);
					log_line("fairlead: stopped a leftover tunnel process");
				}
			}

			CloseHandle(proc);
		}
	}

	clear_session_file();
}

/*
===============
win_tun_bypass
Replace the route plan on the running tunnel.
===============
*/
int win_tun_bypass(const char **cidrs, int cidr_count, int exclude_lan, int selected, win_tun_result_t *out) {
	fl_doc_t	doc;
	char		*text;
	FILE		*f;
	long		n;

	text = NULL;

	lock();

	if (!s_session.active) {
		unlock();

		if (out) {
			memset(out, 0, sizeof(*out));
			snprintf(out->error, sizeof(out->error), "The tunnel is not running.");
		}

		return 0;
	}

	f = fopen(s_session.config_path, "rb");

	if (f) {
		fseek(f, 0, SEEK_END);
		n = ftell(f);
		fseek(f, 0, SEEK_SET);

		if (n > 0 && n < 256000) {
			text = (char *)malloc((size_t)n + 1);

			if (text) {
				fread(text, 1, (size_t)n, f);
				text[n] = '\0';
			}
		}

		fclose(f);
	}

	fl_doc_init(&doc);

	if (text) {
		fl_doc_parse(&doc, text);
	}

	sync_capture(&doc, !selected);
	apply_plan(cidrs, cidr_count, exclude_lan, selected);
	fl_doc_free(&doc);
	free(text);

	if (out) {
		copy_result(out);
		read_stats(out);
		out->ok = 1;
	}

	save_session();
	unlock();
	return 1;
}

/*
===============
win_tun_up
Bring a tunnel up from a config and a CIDR list.
===============
*/
int win_tun_up(const char *slug, const char *config, const char **cidrs, int cidr_count, int exclude_lan, int selected, const char *exe_dir, win_tun_result_t *out) {
	fl_doc_t	doc;
	int			ni;
	char		path[MAX_PATH];
	FILE		*f;
	const char	*binary;
	char		*rendered;
	char		uapi[8192];
	const char	*endpoints[32];
	int			peers[8];
	int			peer_count;
	int			i;
	char		reply[1024];
	char		addrs[512];
	int			naddr;
	WSADATA		wsa;
	char		full[MAX_PATH];
	int			attempt;
	int			ready;

	char issues[8][192];
	char items[8][256];

	memset(out, 0, sizeof(*out));
	WSAStartup(MAKEWORD(2, 2), &wsa);
	lock();

	if (s_session.active) {
		session_down();
	}

	memset(&s_session, 0, sizeof(s_session));
	fl_doc_init(&doc);
	fl_doc_parse(&doc, config);
	ni = fl_config_issues(&doc, issues, 8);

	if (ni > 0) {
		snprintf(out->error, sizeof(out->error), "%s", issues[0]);
		fl_doc_free(&doc);
		unlock();
		return 0;
	}

	binary = "amneziawg-go.exe";
	snprintf(s_session.binary, sizeof(s_session.binary), "%s", binary);
	{
		snprintf(full, sizeof(full), "%s\\%s", exe_dir, binary);

		if (!file_exists(full)) {
			snprintf(out->error, sizeof(out->error), "%s is not included in this copy of Fairlead.", binary);
			fl_doc_free(&doc);
			unlock();
			return 0;
		}
	}

	if (!find_default(&s_session.phys_luid, s_session.gateway_v4, (int)sizeof(s_session.gateway_v4), s_session.gateway_iface, (int)sizeof(s_session.gateway_iface), s_session.phys_ip, (int)sizeof(s_session.phys_ip), &s_session.phys_guid)) {
		snprintf(out->error, sizeof(out->error), "No gateway for the endpoint.");
		fl_doc_free(&doc);
		unlock();
		return 0;
	}

	s_session.have_phys = 1;
	snprintf(s_session.slug, sizeof(s_session.slug), "%s", slug);
	snprintf(s_session.iface, sizeof(s_session.iface), "%s", slug);
	GetEnvironmentVariableA("ProgramData", path, MAX_PATH);
	strncat(path, "\\Fairlead", sizeof(path) - strlen(path) - 1);
	CreateDirectoryA(path, NULL);
	snprintf(s_session.config_path, sizeof(s_session.config_path), "%s\\%s.conf", path, slug);
	rendered = fl_doc_render(&doc);
	f = fopen(s_session.config_path, "wb");

	if (!f || !rendered) {
		snprintf(out->error, sizeof(out->error), "Could not write the config.");
		free(rendered);

		if (f) {
			fclose(f);
		}

		fl_doc_free(&doc);
		unlock();
		return 0;
	}

	fputs(rendered, f);
	fclose(f);
	free(rendered);

	memset(endpoints, 0, sizeof(endpoints));
	peer_count = fl_doc_peers(&doc, peers, 8);
	s_session.endpoint_count = 0;

	for (i = 0; i < peer_count; i++) {
		char endpoint[256];
		char host[256];
		char ip[64];
		char socket[128];

		fl_doc_value(&doc, peers[i], "Endpoint", endpoint, (int)sizeof(endpoint));

		if (!fl_endpoint_host(endpoint, host, (int)sizeof(host))) {
			continue;
		}

		if (!resolve_host(host, ip, (int)sizeof(ip))) {
			snprintf(out->error, sizeof(out->error), "Could not resolve the endpoint.");
			fl_doc_free(&doc);
			unlock();
			return 0;
		}

		if (!fl_socket_endpoint(ip, endpoint, socket, (int)sizeof(socket))) {
			snprintf(out->error, sizeof(out->error), "Could not resolve the endpoint.");
			fl_doc_free(&doc);
			unlock();
			return 0;
		}

		endpoints[peers[i]] = _strdup(socket);

		if (s_session.endpoint_count < 4) {
			snprintf(s_session.endpoints[s_session.endpoint_count], 64, "%s", ip);
			s_session.endpoint_count++;
		}
	}

	if (!fl_uapi_set(&doc, endpoints, uapi, (int)sizeof(uapi))) {
		snprintf(out->error, sizeof(out->error), "The config is damaged.");

		for (i = 0; i < 32; i++) {
			free((void *)endpoints[i]);
		}

		fl_doc_free(&doc);
		unlock();
		return 0;
	}

	if (!spawn_go(exe_dir, binary, s_session.iface, &s_session.proc)) {
		snprintf(out->error, sizeof(out->error), "%s did not create an interface.", binary);

		for (i = 0; i < 32; i++) {
			free((void *)endpoints[i]);
		}

		fl_doc_free(&doc);
		unlock();
		return 0;
	}

	s_session.proc_open = 1;

	if (!wait_adapter(s_session.iface, &s_session.tun_luid)) {
		snprintf(out->error, sizeof(out->error), "%s did not create an interface.", binary);
		session_down();

		for (i = 0; i < 32; i++) {
			free((void *)endpoints[i]);
		}

		fl_doc_free(&doc);
		unlock();
		return 0;
	}

	s_session.have_tun = 1;
	{
		ready = 0;

		for (attempt = 0; attempt < 40; attempt++) {
			reply[0] = '\0';

			if (uapi_exchange(s_session.iface, uapi, reply, (int)sizeof(reply)) && strstr(reply, "errno=0")) {
				ready = 1;
				break;
			}

			Sleep(50);
		}

		if (!ready) {
			snprintf(out->error, sizeof(out->error), "%s did not create an interface.", binary);
			session_down();

			for (i = 0; i < 32; i++) {
				free((void *)endpoints[i]);
			}

			fl_doc_free(&doc);
			unlock();
			return 0;
		}
	}

	fl_doc_addresses(&doc, addrs, (int)sizeof(addrs));
	naddr = fl_split_commas(addrs, items, 8);

	for (i = 0; i < naddr; i++) {
		if (!add_address(s_session.iface, items[i])) {
			snprintf(out->error, sizeof(out->error), "Could not assign %s.", items[i]);
			session_down();

			for (i = 0; i < 32; i++) {
				free((void *)endpoints[i]);
			}

			fl_doc_free(&doc);
			unlock();
			return 0;
		}
	}

	set_mtu(s_session.tun_luid, mtu_of(&doc));
	fill_dns(&doc);
	sync_capture(&doc, !selected);

	for (i = 0; i < s_session.endpoint_count; i++) {
		char dest[80];

		snprintf(dest, sizeof(dest), "%s/32", s_session.endpoints[i]);
		add_route(dest, 0);
	}

	apply_dns();
	apply_plan(cidrs, cidr_count, exclude_lan, !selected ? 0 : 1);
	s_session.active = 1;
	copy_result(out);
	read_stats(out);
	out->ok = 1;
	save_session();

	for (i = 0; i < 32; i++) {
		free((void *)endpoints[i]);
	}

	fl_doc_free(&doc);
	unlock();
	log_line("fairlead: tunnel up");
	return 1;
}
