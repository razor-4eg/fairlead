//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// win_tun.c -- One mihomo process on Windows. mihomo owns the TUN adapter, routes, DNS, WireGuard, and process rules.
//
//===============================================================

#include "win_tun.h"

#include "../../core/base.h"
#include "../../core/net.h"
#include "../../core/tunnel.h"
#include "../../core/mihomo.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CONTROLLER_HOST L"127.0.0.1"
#define CONTROLLER_PORT 18984
#define CONTROLLER_ADDR "127.0.0.1:18984"

#define PROBE_EVERY_SEC 15
#define PROBE_RETRY_SEC 1
#define PROBE_STALE_SEC 45
#define PING_EVERY_SEC 5
#define PROBE_FAIL_RESTART 6
#define PROBE_MAX_RECOVER 3

typedef struct session_s {
	int active;
	char slug[32];
	char exe[MAX_PATH];
	char workdir[MAX_PATH];
	char config_path[MAX_PATH];
	char log_path[MAX_PATH];
	char pid_path[MAX_PATH];
	char secret[72];
	char endpoint[160];
	char endpoint_ip[64];	/* Resolved before TUN is up. Ping uses this only — no DNS after connect. */
	char *wg;
	PROCESS_INFORMATION proc;
	int proc_open;
	time_t last_probe;
	time_t last_ok;
	time_t last_ping_at;
	time_t recover_until;	/* Grace after a respawn — don't burn fails while TUN is coming up. */
	ULONGLONG traffic_tick;	/* GetTickCount64 — last counter sample. */
	unsigned long long traffic_rx;
	unsigned long long traffic_tx;
	unsigned long long down_bps;
	unsigned long long up_bps;
	int last_delay_ms;
	int last_ping_ms;
	int probe_fails;
	int recoveries;
} session_t;

static session_t s_session;

static int tunnel_path_ready(void);

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

	if (n == 0 || n >= MAX_PATH - 32) {
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
add_warning
===============
*/
static void add_warning(win_tun_result_t *out, const char *text) {
	if (!out || out->warning_count >= 12) {
		return;
	}

	snprintf(out->warnings[out->warning_count++], sizeof(out->warnings[0]), "%s", text);
}

/*
===============
file_exists
===============
*/
static int file_exists(const char *path) {
	DWORD	attr;

	attr = GetFileAttributesA(path);
	return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

/*
===============
run_dir
ProgramData\Fairlead\run, created on demand.
===============
*/
static int run_dir(char *path, int cap) {
	DWORD	n;

	n = GetEnvironmentVariableA("ProgramData", path, (DWORD)cap);

	if (n == 0 || (int)n >= cap - 32) {
		return 0;
	}

	strncat(path, "\\Fairlead", (size_t)cap - strlen(path) - 1);
	CreateDirectoryA(path, NULL);
	strncat(path, "\\run", (size_t)cap - strlen(path) - 1);
	CreateDirectoryA(path, NULL);
	return 1;
}

/*
===============
make_secret
Random hex for the controller. The helper token file stays untouched.
===============
*/
static void make_secret(char *out, int cap) {
	unsigned char	raw[16];
	int				i;

	if (BCryptGenRandom(NULL, raw, (ULONG)sizeof(raw), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
		for (i = 0; i < (int)sizeof(raw); i++) {
			raw[i] = (unsigned char)(rand() & 0xff);
		}
	}

	for (i = 0; i < (int)sizeof(raw) && i * 2 + 2 < cap; i++) {
		snprintf(out + i * 2, (size_t)(cap - i * 2), "%02x", raw[i]);
	}
}

/*
===============
api
One controller request. reply may be NULL. Returns 1 when a response came back; status is its HTTP code.
timeout_ms covers send and receive (WinHTTP).
===============
*/
static int api(const char *method, const char *path, const char *body, char *reply, int reply_cap, int *status, int timeout_ms) {
	HINTERNET	ses;
	HINTERNET	con;
	HINTERNET	req;
	wchar_t		wmethod[8];
	wchar_t		wpath[512];
	wchar_t		hdr[256];
	wchar_t		wsecret[80];
	DWORD		code;
	DWORD		size;
	DWORD		got;
	DWORD		blen;
	int			len;
	int			ok;
	int			t;

	ok = 0;
	code = 0;
	len = 0;
	blen = body ? (DWORD)strlen(body) : 0;
	t = timeout_ms > 0 ? timeout_ms : 3000;

	if (reply && reply_cap > 0) {
		reply[0] = '\0';
	}

	MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod, 8);
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
	MultiByteToWideChar(CP_UTF8, 0, s_session.secret, -1, wsecret, 80);
	swprintf(hdr, 256, L"Authorization: Bearer %ls\r\nContent-Type: application/json\r\n", wsecret);

	ses = WinHttpOpen(L"Fairlead", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);

	if (!ses) {
		return 0;
	}

	WinHttpSetTimeouts(ses, t, t, t, t);
	con = WinHttpConnect(ses, CONTROLLER_HOST, CONTROLLER_PORT, 0);
	req = con ? WinHttpOpenRequest(con, wmethod, wpath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : NULL;

	if (req && WinHttpSendRequest(req, hdr, (DWORD)-1, body ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA, blen, blen, 0) &&
		WinHttpReceiveResponse(req, NULL)) {
		size = sizeof(code);
		WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
		ok = 1;

		while (reply && len < reply_cap - 1) {
			got = 0;

			if (!WinHttpReadData(req, reply + len, (DWORD)(reply_cap - 1 - len), &got) || got == 0) {
				break;
			}

			len += (int)got;
			reply[len] = '\0';
		}

		if (reply) {
			reply[len] = '\0';
		}
	}

	if (status) {
		*status = (int)code;
	}

	if (req) {
		WinHttpCloseHandle(req);
	}

	if (con) {
		WinHttpCloseHandle(con);
	}

	WinHttpCloseHandle(ses);
	return ok;
}

/*
===============
read_log
The tail of mihomo.log, up to cap - 1 bytes.
===============
*/
static int read_log(char *buf, int cap) {
	FILE	*f;
	long	size;
	long	from;
	int		n;

	buf[0] = '\0';
	f = fopen(s_session.log_path, "rb");

	if (!f) {
		return 0;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	from = size > cap - 1 ? size - (cap - 1) : 0;
	fseek(f, from, SEEK_SET);
	n = (int)fread(buf, 1, (size_t)(cap - 1), f);
	fclose(f);
	buf[n > 0 ? n : 0] = '\0';
	return n;
}

/*
===============
last_line
The last non-empty line of the log, for an error message.
===============
*/
static void last_line(char *out, int cap) {
	char	buf[4096];
	int		n;
	char	*start;

	out[0] = '\0';
	n = read_log(buf, (int)sizeof(buf));

	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
		buf[--n] = '\0';
	}

	if (n <= 0) {
		return;
	}

	start = strrchr(buf, '\n');
	snprintf(out, (size_t)cap, "%s", start ? start + 1 : buf);
}

/*
===============
is_running
===============
*/
static int is_running(void) {
	return s_session.proc_open && WaitForSingleObject(s_session.proc.hProcess, 0) == WAIT_TIMEOUT;
}

/*
===============
write_pid
Remember the child so a later helper can find an orphan.
===============
*/
static void write_pid(void) {
	FILE	*f;

	f = fopen(s_session.pid_path, "wb");

	if (!f) {
		return;
	}

	fprintf(f, "%lu\n", (unsigned long)s_session.proc.dwProcessId);
	fclose(f);
}

/*
===============
controller_up
True when something still answers on the external-controller port.
===============
*/
static int controller_up(void) {
	HINTERNET	ses;
	HINTERNET	con;
	HINTERNET	req;
	int			ok;

	ses = WinHttpOpen(L"Fairlead", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);

	if (!ses) {
		return 0;
	}

	WinHttpSetTimeouts(ses, 200, 200, 200, 200);
	con = WinHttpConnect(ses, CONTROLLER_HOST, CONTROLLER_PORT, 0);
	req = con ? WinHttpOpenRequest(con, L"GET", L"/version", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : NULL;
	ok = req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
		WinHttpReceiveResponse(req, NULL);

	if (req) {
		WinHttpCloseHandle(req);
	}

	if (con) {
		WinHttpCloseHandle(con);
	}

	WinHttpCloseHandle(ses);
	return ok;
}

/*
===============
wait_controller_gone
After a kill, wait until the old controller releases the port so the next spawn can bind.
===============
*/
static void wait_controller_gone(int max_ms) {
	int	waited;

	for (waited = 0; waited < max_ms; waited += 100) {
		if (!controller_up()) {
			return;
		}

		Sleep(100);
	}
}

static void close_connections(void);

/*
===============
stop_proc
Turn the TUN off through the controller so mihomo drops the adapter and routes, then end the process.
===============
*/
static void stop_proc(void) {
	char	reply[128];
	int		status;

	if (!s_session.proc_open) {
		wait_controller_gone(2000);
		return;
	}

	if (is_running()) {
		/* Drop live flows before tearing the TUN down so Chrome can reopen on the real path. */
		close_connections();
		api("PATCH", "/configs", "{\"tun\":{\"enable\":false}}", reply, (int)sizeof(reply), &status, 1000);
		WaitForSingleObject(s_session.proc.hProcess, 500);
	}

	if (is_running()) {
		TerminateProcess(s_session.proc.hProcess, 0);
	}

	WaitForSingleObject(s_session.proc.hProcess, 2000);
	CloseHandle(s_session.proc.hProcess);
	CloseHandle(s_session.proc.hThread);
	s_session.proc_open = 0;
	memset(&s_session.proc, 0, sizeof(s_session.proc));
	DeleteFileA(s_session.pid_path);
	wait_controller_gone(3000);
}

/*
===============
spawn_mihomo
mihomo.exe -d workdir -f config.yaml, hidden. Output goes to mihomo.log.
===============
*/
static int spawn_mihomo(void) {
	char				cmd[1024];
	SECURITY_ATTRIBUTES	sa;
	HANDLE				logf;
	HANDLE				nul;
	STARTUPINFOA		si;
	char				msg[192];

	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	logf = CreateFileA(s_session.log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = nul;
	si.hStdOutput = logf == INVALID_HANDLE_VALUE ? nul : logf;
	si.hStdError = si.hStdOutput;
	snprintf(cmd, sizeof(cmd), "\"%s\" -d \"%s\" -f \"%s\"", s_session.exe, s_session.workdir, s_session.config_path);
	memset(&s_session.proc, 0, sizeof(s_session.proc));

	if (!CreateProcessA(s_session.exe, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, NULL, s_session.workdir, &si, &s_session.proc)) {
		snprintf(msg, sizeof(msg), "fairlead: could not start mihomo (%lu)", (unsigned long)GetLastError());
		log_line(msg);

		if (nul != INVALID_HANDLE_VALUE) {
			CloseHandle(nul);
		}

		if (logf != INVALID_HANDLE_VALUE) {
			CloseHandle(logf);
		}

		return 0;
	}

	if (nul != INVALID_HANDLE_VALUE) {
		CloseHandle(nul);
	}

	if (logf != INVALID_HANDLE_VALUE) {
		CloseHandle(logf);
	}

	s_session.proc_open = 1;
	write_pid();
	return 1;
}

/*
===============
tun_enabled
True when GET /configs reports tun.enable. WireGuard can answer delay probes with TUN off.
===============
*/
static int tun_enabled(void) {
	char	reply[2048];
	int		status;
	char	*tun;
	char	*enable;

	status = 0;

	if (!api("GET", "/configs", NULL, reply, (int)sizeof(reply), &status, 1500) || status != 200) {
		return 0;
	}

	tun = strstr(reply, "\"tun\"");

	if (!tun) {
		return 0;
	}

	enable = strstr(tun, "\"enable\"");

	if (!enable || enable > tun + 400) {
		return 0;
	}

	enable += 8;

	while (*enable == ' ' || *enable == '\t' || *enable == ':') {
		enable++;
	}

	return strncmp(enable, "true", 4) == 0;
}

/*
===============
wait_ready
Wait for the controller, then for the system path into Meta/Wintun.
===============
*/
static int wait_ready(char *err, int err_cap) {
	char	reply[256];
	char	line[320];
	char	logbuf[8192];
	int		status;
	int		i;

	for (i = 0; i < 60; i++) {
		if (!is_running()) {
			last_line(line, (int)sizeof(line));
			snprintf(err, (size_t)err_cap, "The tunnel exited. %s", line);
			return 0;
		}

		status = 0;

		if (api("GET", "/version", NULL, reply, (int)sizeof(reply), &status, 1000) && status == 200) {
			break;
		}

		Sleep(250);
	}

	if (i >= 60) {
		snprintf(err, (size_t)err_cap, "The tunnel did not answer on %s.", CONTROLLER_ADDR);
		return 0;
	}

	/* mihomo retries TUN for ~45s; then wait until system traffic enters Meta/Wintun. */
	for (i = 0; i < 100; i++) {
		if (!is_running()) {
			last_line(line, (int)sizeof(line));
			snprintf(err, (size_t)err_cap, "The tunnel exited. %s", line);
			return 0;
		}

		if (tunnel_path_ready()) {
			return 1;
		}

		read_log(logbuf, (int)sizeof(logbuf));

		if (strstr(logbuf, "TUN listening error")) {
			last_line(line, (int)sizeof(line));
			snprintf(err, (size_t)err_cap, "The tunnel could not start the network adapter. %s", line);
			return 0;
		}

		Sleep(500);
	}

	if (tun_enabled()) {
		snprintf(err, (size_t)err_cap,
			"The tunnel adapter is up, but system routes still point at the regular network.");
		return 0;
	}

	snprintf(err, (size_t)err_cap, "The tunnel started without a network adapter.");
	return 0;
}

/*
===============
normalize_cidr
A bare address becomes /32 or /128. mihomo's IP-CIDR rule needs the prefix.
===============
*/
static int normalize_cidr(const char *text, char *out, int cap) {
	cidr_t	parsed;
	char	tmp[64];

	if (!text || !text[0] || strlen(text) >= 56) {
		return 0;
	}

	snprintf(tmp, sizeof(tmp), "%s", text);

	if (!strchr(tmp, '/')) {
		strcat(tmp, strchr(tmp, ':') ? "/128" : "/32");
	}

	if (!cidr_parse(tmp, &parsed)) {
		return 0;
	}

	snprintf(out, (size_t)cap, "%s", tmp);
	return 1;
}

/*
===============
write_config
Build config.yaml from the WireGuard text and the rules. Fills err on failure.
===============
*/
static int write_config(const char *wg_text, const char **cidrs, int cidr_count, const char **domains, int domain_count,
	const char **apps, int app_count, int exclude_lan, int block_quic, int selected, char *err, int err_cap) {
	mihomo_rules_t	rules;
	char			(*norm)[64];
	const char		**ptrs;
	int				n;
	int				i;
	char			*yaml;
	FILE			*f;
	size_t			len;
	size_t			wrote;

	norm = (char (*)[64])calloc(cidr_count > 0 ? (size_t)cidr_count : 1, 64);
	ptrs = (const char **)calloc(cidr_count > 0 ? (size_t)cidr_count : 1, sizeof(*ptrs));

	if (!norm || !ptrs) {
		free(norm);
		free(ptrs);
		snprintf(err, (size_t)err_cap, "Not enough memory.");
		return 0;
	}

	n = 0;

	for (i = 0; i < cidr_count; i++) {
		if (normalize_cidr(cidrs[i], norm[n], 64)) {
			ptrs[n] = norm[n];
			n++;
		}
	}

	memset(&rules, 0, sizeof(rules));
	rules.cidrs = ptrs;
	rules.cidr_count = n;
	rules.domains = domains;
	rules.domain_count = domain_count;
	rules.apps = apps;
	rules.app_count = app_count;
	yaml = mihomo_config(wg_text, selected, exclude_lan, block_quic, &rules, CONTROLLER_ADDR, s_session.secret, err, err_cap);
	free(norm);
	free(ptrs);

	if (!yaml) {
		if (err[0] == '\0') {
			snprintf(err, (size_t)err_cap, "Could not build the tunnel config.");
		}

		return 0;
	}

	f = fopen(s_session.config_path, "wb");

	if (!f) {
		free(yaml);
		snprintf(err, (size_t)err_cap, "Could not write %s.", s_session.config_path);
		return 0;
	}

	len = strlen(yaml);
	wrote = fwrite(yaml, 1, len, f);
	fclose(f);
	free(yaml);

	if (wrote != len) {
		snprintf(err, (size_t)err_cap, "Could not write %s.", s_session.config_path);
		return 0;
	}

	return 1;
}

/*
===============
nic_score
Mihomo's adapter. Friendly name is Meta; the description mentions Wintun.
===============
*/
static int nic_score(MIB_IFROW *row) {
	wchar_t	name[MAX_INTERFACE_NAME_LEN + 1];
	char	descr[MAXLEN_IFDESCR + 1];
	DWORD	n;

	if (row->dwOperStatus != MIB_IF_OPER_STATUS_OPERATIONAL && row->dwOperStatus != MIB_IF_OPER_STATUS_CONNECTED) {
		return 0;
	}

	wcsncpy(name, row->wszName, MAX_INTERFACE_NAME_LEN);
	name[MAX_INTERFACE_NAME_LEN] = 0;
	n = row->dwDescrLen;

	if (n > MAXLEN_IFDESCR) {
		n = MAXLEN_IFDESCR;
	}

	memcpy(descr, row->bDescr, n);
	descr[n] = '\0';

	if (_wcsicmp(name, L"Meta") == 0 || _wcsicmp(name, L"Mihomo") == 0) {
		return 2;
	}

	if (wcsstr(name, L"Meta") || wcsstr(name, L"Mihomo") || wcsstr(name, L"Wintun")) {
		return 2;
	}

	if (strstr(descr, "Wintun") || strstr(descr, "Meta") || strstr(descr, "Mihomo")) {
		return 1;
	}

	return 0;
}

/*
===============
tun_routes_active
True when general IPv4 traffic is steered into Meta/Wintun (mihomo auto-route).
Same idea as Mac `route get 1.1.1.1` → utun.
===============
*/
static int tun_routes_active(void) {
	struct in_addr	dest;
	DWORD			ifindex;
	MIB_IFROW		row;

	if (InetPtonA(AF_INET, "1.1.1.1", &dest) != 1) {
		return 0;
	}

	ifindex = 0;

	if (GetBestInterface(dest.S_un.S_addr, &ifindex) != NO_ERROR || ifindex == 0) {
		return 0;
	}

	memset(&row, 0, sizeof(row));
	row.dwIndex = ifindex;

	if (GetIfEntry(&row) != NO_ERROR) {
		return 0;
	}

	return nic_score(&row) > 0;
}

/*
===============
tunnel_path_ready
TUN is enabled in mihomo and the system default path actually enters that adapter.
===============
*/
static int tunnel_path_ready(void) {
	return tun_enabled() && tun_routes_active();
}

/*
===============
nic_totals
Byte counters from the TUN adapter. Local MIB read — no controller request.
===============
*/
static int nic_totals(unsigned long long *rx, unsigned long long *tx) {
	MIB_IFTABLE	*table;
	DWORD		size;
	DWORD		i;
	int			best;
	int			rank;

	*rx = 0;
	*tx = 0;
	size = 0;
	table = NULL;
	best = -1;
	rank = 0;

	if (GetIfTable(NULL, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size == 0) {
		return 0;
	}

	table = (MIB_IFTABLE *)malloc(size);

	if (!table || GetIfTable(table, &size, FALSE) != NO_ERROR) {
		free(table);
		return 0;
	}

	for (i = 0; i < table->dwNumEntries; i++) {
		int	score;

		score = nic_score(&table->table[i]);

		if (score > rank) {
			rank = score;
			best = (int)i;
		}
	}

	if (best >= 0) {
		/* In = host download, Out = host upload, from the stack's side of the NIC. */
		*rx = table->table[best].dwInOctets;
		*tx = table->table[best].dwOutOctets;
	}

	free(table);
	return best >= 0;
}

/*
===============
close_connections
Drop live flows so new rules apply immediately after a reload.
Do not flush DNS caches — browsers still hold resolved addresses.
===============
*/
static void close_connections(void) {
	char	reply[128];
	int		status;

	status = 0;
	api("DELETE", "/connections", NULL, reply, (int)sizeof(reply), &status, 3000);
}

#define APP_EXE_MAX 64

/*
===============
remember_exe
Keep the file name. Process rules match chrome.exe, not the directory.
===============
*/
static void remember_exe(wchar_t (*names)[MAX_PATH], int *count, int cap, const wchar_t *text) {
	const wchar_t	*slash;
	const wchar_t	*base;
	int				i;

	if (!names || !count || !text || !text[0] || *count >= cap) {
		return;
	}

	slash = wcsrchr(text, L'\\');

	if (!slash) {
		slash = wcsrchr(text, L'/');
	}

	base = slash ? slash + 1 : text;

	if (!base[0]) {
		return;
	}

	for (i = 0; i < *count; i++) {
		if (_wcsicmp(names[i], base) == 0) {
			return;
		}
	}

	wcsncpy(names[*count], base, MAX_PATH - 1);
	names[*count][MAX_PATH - 1] = L'\0';
	(*count)++;
}

/*
===============
remember_utf8
===============
*/
static void remember_utf8(wchar_t (*names)[MAX_PATH], int *count, int cap, const char *text) {
	wchar_t	wide[MAX_PATH];

	if (!text || !text[0]) {
		return;
	}

	if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, MAX_PATH) <= 0) {
		return;
	}

	remember_exe(names, count, cap, wide);
}

/*
===============
exes_from_yaml
Names already in config.yaml, so a removed app is reset too.
===============
*/
static void exes_from_yaml(const char *path, wchar_t (*names)[MAX_PATH], int *count, int cap) {
	FILE	*f;
	char	*buf;
	long	n;
	char	*p;

	if (!path || !path[0]) {
		return;
	}

	f = fopen(path, "rb");

	if (!f) {
		return;
	}

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return;
	}

	n = ftell(f);

	if (n <= 0 || n > (1 << 20)) {
		fclose(f);
		return;
	}

	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return;
	}

	buf = (char *)malloc((size_t)n + 1);

	if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		fclose(f);
		return;
	}

	buf[n] = '\0';
	fclose(f);
	p = buf;

	while ((p = strstr(p, "PROCESS-")) != NULL) {
		char	raw[MAX_PATH * 2];
		char	*comma;
		char	*end;
		int		k;
		int		o;

		if (strncmp(p, "PROCESS-NAME,", 13) != 0 && strncmp(p, "PROCESS-PATH,", 13) != 0) {
			p += 8;
			continue;
		}

		comma = strchr(p, ',');

		if (!comma) {
			break;
		}

		comma++;
		end = strchr(comma, ',');

		if (!end || (size_t)(end - comma) >= sizeof(raw)) {
			p = comma;
			continue;
		}

		o = 0;

		for (k = 0; comma + k < end && o + 1 < (int)sizeof(raw); k++) {
			if (comma[k] == '\\' && comma[k + 1] == '\\') {
				raw[o++] = '\\';
				k++;
			} else {
				raw[o++] = comma[k];
			}
		}

		raw[o] = '\0';
		remember_utf8(names, count, cap, raw);
		p = end;
	}

	free(buf);
}

/*
===============
exe_listed
===============
*/
static int exe_listed(wchar_t (*names)[MAX_PATH], int count, const wchar_t *base) {
	int	a;

	for (a = 0; a < count; a++) {
		if (_wcsicmp(base, names[a]) == 0) {
			return 1;
		}
	}

	return 0;
}

/*
===============
reset_app_tcp4
===============
*/
static void reset_app_tcp4(wchar_t (*names)[MAX_PATH], int count) {
	MIB_TCPTABLE_OWNER_PID	*table;
	DWORD					size;
	DWORD					i;

	size = 0;
	table = NULL;

	if (GetExtendedTcpTable(NULL, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != ERROR_INSUFFICIENT_BUFFER || !size) {
		return;
	}

	table = (MIB_TCPTABLE_OWNER_PID *)malloc(size);

	if (!table || GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
		free(table);
		return;
	}

	for (i = 0; i < table->dwNumEntries; i++) {
		MIB_TCPROW_OWNER_PID	*row;
		HANDLE					proc;
		wchar_t					image[MAX_PATH];
		DWORD					nlen;
		wchar_t					*base;
		MIB_TCPROW				kill;

		row = &table->table[i];

		if (row->dwState != MIB_TCP_STATE_ESTAB && row->dwState != MIB_TCP_STATE_CLOSE_WAIT) {
			continue;
		}

		if (row->dwRemoteAddr == htonl(INADDR_LOOPBACK) || row->dwLocalAddr == htonl(INADDR_LOOPBACK)) {
			continue;
		}

		proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row->dwOwningPid);

		if (!proc) {
			continue;
		}

		nlen = MAX_PATH;

		if (!QueryFullProcessImageNameW(proc, 0, image, &nlen)) {
			CloseHandle(proc);
			continue;
		}

		CloseHandle(proc);
		base = wcsrchr(image, L'\\');
		base = base ? base + 1 : image;

		if (!exe_listed(names, count, base)) {
			continue;
		}

		memset(&kill, 0, sizeof(kill));
		kill.dwState = MIB_TCP_STATE_DELETE_TCB;
		kill.dwLocalAddr = row->dwLocalAddr;
		kill.dwLocalPort = row->dwLocalPort;
		kill.dwRemoteAddr = row->dwRemoteAddr;
		kill.dwRemotePort = row->dwRemotePort;
		SetTcpEntry(&kill);
	}

	free(table);
}

/*
===============
reset_app_tcp
The system TUN stack keeps the app's own sockets. Mihomo only forgets them,
so a new process rule waits until the app is restarted. RST those IPv4 flows.
(SetTcpEntry is IPv4-only; UDP/443 QUIC is rejected in config so Chrome uses TCP.)
===============
*/
static void reset_app_tcp(wchar_t (*names)[MAX_PATH], int count) {
	if (!names || count <= 0) {
		return;
	}

	reset_app_tcp4(names, count);
}

/*
===============
resolve_endpoint_ip
DNS lookup before TUN is up. Stores an IPv4 address for ICMP ping.
===============
*/
static void resolve_endpoint_ip(void) {
	char				host[256];
	struct addrinfo		hints;
	struct addrinfo		*res;
	struct addrinfo		*a;

	s_session.endpoint_ip[0] = '\0';

	if (!endpoint_host(s_session.endpoint, host, (int)sizeof(host))) {
		return;
	}

	/* Already a dotted IPv4 — no DNS. */
	{
		struct in_addr	addr;

		if (InetPtonA(AF_INET, host, &addr) == 1) {
			snprintf(s_session.endpoint_ip, sizeof(s_session.endpoint_ip), "%s", host);
			return;
		}
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;

	if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) {
		return;
	}

	for (a = res; a; a = a->ai_next) {
		if (a->ai_family == AF_INET) {
			char	ip[64];

			if (getnameinfo(a->ai_addr, (socklen_t)a->ai_addrlen, ip, (DWORD)sizeof(ip), NULL, 0, NI_NUMERICHOST) == 0) {
				snprintf(s_session.endpoint_ip, sizeof(s_session.endpoint_ip), "%s", ip);
			}

			break;
		}
	}

	freeaddrinfo(res);
}

/*
===============
ping_endpoint
ICMP echo to the pre-resolved endpoint IP. No DNS after connect.
===============
*/
static int ping_endpoint(void) {
	struct in_addr		addr;
	HANDLE				icmp;
	char				send[32];
	char				reply[sizeof(ICMP_ECHO_REPLY) + 64];
	DWORD				n;
	PICMP_ECHO_REPLY	echo;
	int					rtt;

	rtt = 0;

	if (!s_session.endpoint_ip[0] || InetPtonA(AF_INET, s_session.endpoint_ip, &addr) != 1) {
		return 0;
	}

	icmp = IcmpCreateFile();

	if (icmp == INVALID_HANDLE_VALUE) {
		return 0;
	}

	memset(send, 0, sizeof(send));
	n = IcmpSendEcho(icmp, addr.S_un.S_addr, send, (WORD)sizeof(send), NULL, reply, (DWORD)sizeof(reply), 800);
	IcmpCloseHandle(icmp);

	if (n > 0) {
		echo = (PICMP_ECHO_REPLY)reply;

		if (echo->Status == IP_SUCCESS) {
			rtt = (int)echo->RoundTripTime;

			if (rtt <= 0) {
				rtt = 1;
			}
		}
	}

	return rtt;
}

/*
===============
protocol_label
WireGuard or AmneziaWG from the stored config.
===============
*/
static void protocol_label(char *out, int cap) {
	doc_t	doc;

	snprintf(out, (size_t)cap, "WireGuard");

	if (!s_session.wg || !s_session.wg[0]) {
		return;
	}

	doc_init(&doc);
	doc_parse(&doc, s_session.wg);

	if (amnezia_is(&doc)) {
		snprintf(out, (size_t)cap, "AmneziaWG");
	}

	doc_free(&doc);
}

/*
===============
fill_identity
Protocol label, peer endpoint, and counters — no liveness probe.
===============
*/
static void fill_identity(win_tun_result_t *out) {
	static const char *const lan_ranges[] = {
		"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "169.254.0.0/16", "fc00::/7", "fe80::/10"
	};
	int					i;

	protocol_label(out->iface, (int)sizeof(out->iface));
	snprintf(out->live_endpoint, sizeof(out->live_endpoint), "%s", s_session.endpoint);
	out->gateway_v4[0] = '\0';
	out->stage[0] = '\0';
	out->lan_count = 0;

	for (i = 0; i < (int)(sizeof(lan_ranges) / sizeof(lan_ranges[0])) && out->lan_count < 8; i++) {
		snprintf(out->lan[out->lan_count++], sizeof(out->lan[0]), "%s", lan_ranges[i]);
	}

	if (s_session.endpoint_ip[0]) {
		snprintf(out->gateway_v4, sizeof(out->gateway_v4), "%s", s_session.endpoint_ip);
	} else if (s_session.endpoint[0]) {
		char	host[256];

		if (endpoint_host(s_session.endpoint, host, (int)sizeof(host))) {
			snprintf(out->gateway_v4, sizeof(out->gateway_v4), "%s", host);
		}
	}

	out->handshake = 0;
	out->delay_ms = 0;
	out->ping_ms = 0;
	out->rx = 0;
	out->tx = 0;
	out->down_bps = 0;
	out->up_bps = 0;

	if (!is_running()) {
		return;
	}

	/* Last good counters — refresh_traffic() updates these once Connected. */
	out->rx = s_session.traffic_rx;
	out->tx = s_session.traffic_tx;
	out->down_bps = s_session.down_bps;
	out->up_bps = s_session.up_bps;
}

/*
===============
refresh_traffic
Rates from the adapter counters. Local MIB read only — never an HTTP call.
===============
*/
static void refresh_traffic(win_tun_result_t *out) {
	unsigned long long	rx;
	unsigned long long	tx;
	ULONGLONG			now;

	now = GetTickCount64();

	if (!nic_totals(&rx, &tx)) {
		out->rx = s_session.traffic_rx;
		out->tx = s_session.traffic_tx;

		if (s_session.traffic_tick == 0 || now < s_session.traffic_tick ||
			(now - s_session.traffic_tick) > 2000) {
			s_session.down_bps = 0;
			s_session.up_bps = 0;
		}

		out->down_bps = s_session.down_bps;
		out->up_bps = s_session.up_bps;
		return;
	}

	if (s_session.traffic_tick != 0 && now > s_session.traffic_tick &&
		rx >= s_session.traffic_rx && tx >= s_session.traffic_tx) {
		double	dt;

		dt = (double)(now - s_session.traffic_tick) / 1000.0;

		if (dt >= 0.2 && dt < 8.0) {
			s_session.down_bps = (unsigned long long)((rx - s_session.traffic_rx) / dt);
			s_session.up_bps = (unsigned long long)((tx - s_session.traffic_tx) / dt);
		}
	} else {
		s_session.down_bps = 0;
		s_session.up_bps = 0;
	}

	s_session.traffic_rx = rx;
	s_session.traffic_tx = tx;
	s_session.traffic_tick = now;
	out->rx = rx;
	out->tx = tx;
	out->down_bps = s_session.down_bps;
	out->up_bps = s_session.up_bps;
}

/*
===============
fill_result
Counters, path-ready Connected, ICMP ping for the UI (only after the tunnel is up).
Connected = system path enters Meta/Wintun — never the HTTP delay probe.
===============
*/
static void fill_result(win_tun_result_t *out) {
	time_t	now;
	int		need_probe;
	int		need_ping;
	int		gap;
	int		path_ready;
	int		ping;

	fill_identity(out);
	now = time(NULL);

	if (!is_running()) {
		s_session.last_probe = 0;
		s_session.last_ok = 0;
		s_session.last_ping_at = 0;
		s_session.last_delay_ms = 0;
		s_session.last_ping_ms = 0;
		s_session.probe_fails = 0;

		if (s_session.wg && s_session.recoveries < PROBE_MAX_RECOVER) {
			log_line("fairlead: mihomo exited, restarting");
			s_session.recoveries++;
			s_session.recover_until = now + 45;
			stop_proc();

			if (spawn_mihomo()) {
				snprintf(out->stage, sizeof(out->stage), "Opening the tunnel…");
				return;
			}
		}

		snprintf(out->stage, sizeof(out->stage), "The tunnel could not be reached.");
		add_warning(out, "mihomo is not running.");
		return;
	}

	/* After a respawn, wait for the system path before counting failures. */
	if (s_session.recover_until != 0 && now < s_session.recover_until && !tunnel_path_ready()) {
		snprintf(out->stage, sizeof(out->stage), "Opening the tunnel…");
		return;
	}

	if (s_session.recover_until != 0 && (now >= s_session.recover_until || tunnel_path_ready())) {
		s_session.recover_until = 0;
	}

	/*
	 * Connected = system path enters our TUN (what Chrome/curl use).
	 * HTTP delay is not used — gstatic can time out on a working tunnel.
	 */
	path_ready = tunnel_path_ready();

	if (path_ready) {
		s_session.last_ok = now;
		s_session.probe_fails = 0;
		s_session.recover_until = 0;
		out->stage[0] = '\0';
	} else {
		s_session.last_ok = 0;
		s_session.last_delay_ms = 0;
		snprintf(out->stage, sizeof(out->stage),
			s_session.last_probe == 0 ? "Opening the tunnel…" : "Checking the tunnel…");
	}

	gap = path_ready ? PROBE_EVERY_SEC : PROBE_RETRY_SEC;
	need_probe = s_session.last_probe == 0 ||
		(now >= s_session.last_probe && (now - s_session.last_probe) >= gap);

	if (need_probe) {
		s_session.last_probe = now;

		if (!path_ready) {
			s_session.probe_fails++;

			/*
			 * Rapid down/up can leave WireGuard stuck until mihomo is restarted.
			 * A few recoveries per session; grace window avoids burning them while TUN rises.
			 */
			if (s_session.probe_fails >= PROBE_FAIL_RESTART &&
				s_session.recoveries < PROBE_MAX_RECOVER && s_session.wg) {
				log_line("fairlead: tunnel path lost, restarting mihomo");
				s_session.recoveries++;
				s_session.probe_fails = 0;
				s_session.last_probe = 0;
				s_session.last_ok = 0;
				s_session.recover_until = now + 45;
				stop_proc();

				if (spawn_mihomo()) {
					snprintf(out->stage, sizeof(out->stage), "Opening the tunnel…");
				} else {
					snprintf(out->stage, sizeof(out->stage), "The tunnel could not be reached.");
					add_warning(out, "The tunnel could not be restarted.");
				}
			} else if (s_session.recoveries >= PROBE_MAX_RECOVER &&
				s_session.probe_fails >= PROBE_FAIL_RESTART) {
				snprintf(out->stage, sizeof(out->stage), "The tunnel could not be reached.");
			}
		}
	}

	if (path_ready && s_session.last_ok != 0 &&
		now >= s_session.last_ok && (now - s_session.last_ok) <= PROBE_STALE_SEC) {
		out->handshake = (int)s_session.last_ok;
		out->delay_ms = 0;
		out->stage[0] = '\0';
		/* Tiny ICMP to the endpoint IP. Every few seconds is enough for the UI. */
		need_ping = s_session.last_ping_at == 0 ||
			(now >= s_session.last_ping_at && (now - s_session.last_ping_at) >= PING_EVERY_SEC);

		if (need_ping) {
			s_session.last_ping_at = now;
			ping = ping_endpoint();

			if (ping > 0) {
				s_session.last_ping_ms = ping;
			}
		}

		out->ping_ms = s_session.last_ping_ms;
		refresh_traffic(out);
	} else {
		s_session.last_ok = 0;
		s_session.last_delay_ms = 0;
		s_session.down_bps = 0;
		s_session.up_bps = 0;
		out->down_bps = 0;
		out->up_bps = 0;
	}
}

/*
===============
json_path
Escape backslashes and quotes for a JSON string.
===============
*/
static void json_path(const char *src, char *dst, int cap) {
	int		n;

	n = 0;

	for (; *src && n + 3 < cap; src++) {
		if (*src == '\\' || *src == '"') {
			dst[n++] = '\\';
		}

		dst[n++] = *src;
	}

	dst[n] = '\0';
}

/*
===============
session_clear
===============
*/
static void session_clear(void) {
	free(s_session.wg);
	s_session.wg = NULL;
	s_session.active = 0;
	s_session.slug[0] = '\0';
	s_session.endpoint[0] = '\0';
	s_session.endpoint_ip[0] = '\0';
	s_session.last_probe = 0;
	s_session.last_ok = 0;
	s_session.last_ping_at = 0;
	s_session.recover_until = 0;
	s_session.traffic_tick = 0;
	s_session.traffic_rx = 0;
	s_session.traffic_tx = 0;
	s_session.down_bps = 0;
	s_session.up_bps = 0;
	s_session.last_delay_ms = 0;
	s_session.last_ping_ms = 0;
	s_session.probe_fails = 0;
	s_session.recoveries = 0;
}

/*
===============
read_endpoint
The peer Endpoint from the WireGuard text, for the status line.
===============
*/
static void read_endpoint(const char *wg_text) {
	doc_t	doc;
	int		peers[PEER_MAX];

	s_session.endpoint[0] = '\0';
	doc_init(&doc);
	doc_parse(&doc, wg_text);

	if (doc_peers(&doc, peers, PEER_MAX) > 0) {
		doc_value(&doc, peers[0], "Endpoint", s_session.endpoint, (int)sizeof(s_session.endpoint));
	}

	doc_free(&doc);
}

/*
===============
win_tun_active
===============
*/
int win_tun_active(void) {
	return s_session.active;
}

/*
===============
win_tun_status
Counters and liveness for the reply. Empty when no session is up.
===============
*/
void win_tun_status(win_tun_result_t *out) {
	memset(out, 0, sizeof(*out));

	if (!s_session.active) {
		return;
	}

	out->ok = 1;
	fill_result(out);
}

/*
===============
win_tun_down
Stop mihomo. It drops the adapter and routes with it.
===============
*/
int win_tun_down(win_tun_result_t *out) {
	if (out) {
		memset(out, 0, sizeof(*out));
	}

	stop_proc();
	session_clear();

	if (out) {
		out->ok = 1;
	}

	return 1;
}

/*
===============
win_tun_reconcile
On helper start, end a mihomo a previous helper left behind. Only the recorded pid is touched.
===============
*/
void win_tun_reconcile(void) {
	char			dir[MAX_PATH];
	char			path[MAX_PATH];
	char			line[64];
	FILE			*f;
	unsigned long	pid;
	HANDLE			proc;
	wchar_t			image[MAX_PATH];
	DWORD			n;
	wchar_t			*base;

	if (!run_dir(dir, MAX_PATH)) {
		return;
	}

	snprintf(path, sizeof(path), "%s\\mihomo.pid", dir);
	f = fopen(path, "rb");

	if (!f) {
		return;
	}

	pid = 0;

	if (fgets(line, (int)sizeof(line), f)) {
		pid = strtoul(line, NULL, 10);
	}

	fclose(f);

	if (pid) {
		proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)pid);

		if (proc) {
			n = MAX_PATH;

			if (QueryFullProcessImageNameW(proc, 0, image, &n)) {
				base = wcsrchr(image, L'\\');
				base = base ? base + 1 : image;

				if (_wcsicmp(base, L"mihomo.exe") == 0) {
					TerminateProcess(proc, 0);
					WaitForSingleObject(proc, 4000);
					log_line("fairlead: ended an orphan mihomo");
				}
			}

			CloseHandle(proc);
		}
	}

	DeleteFileA(path);
}

/*
===============
paths_init
Resolve the paths for this session.
===============
*/
static int paths_init(const char *exe_dir, char *err, int err_cap) {
	snprintf(s_session.exe, sizeof(s_session.exe), "%s\\bin\\mihomo.exe", exe_dir && exe_dir[0] ? exe_dir : ".");

	if (!file_exists(s_session.exe)) {
		snprintf(err, (size_t)err_cap, "mihomo.exe was not found in bin.");
		return 0;
	}

	if (!run_dir(s_session.workdir, MAX_PATH)) {
		snprintf(err, (size_t)err_cap, "ProgramData is not available.");
		return 0;
	}

	snprintf(s_session.config_path, sizeof(s_session.config_path), "%s\\config.yaml", s_session.workdir);
	snprintf(s_session.log_path, sizeof(s_session.log_path), "%s\\mihomo.log", s_session.workdir);
	snprintf(s_session.pid_path, sizeof(s_session.pid_path), "%s\\mihomo.pid", s_session.workdir);
	return 1;
}

/*
===============
win_tun_up
Write config.yaml, start mihomo, and wait until the controller and TUN are up.
===============
*/
int win_tun_up(const char *slug, const char *config, const char **cidrs, int cidr_count,
	const char **domains, int domain_count, const char **apps, int app_count,
	int exclude_lan, int block_quic, int selected, const char *exe_dir, win_tun_result_t *out) {
	memset(out, 0, sizeof(*out));

	if (s_session.active || s_session.proc_open) {
		stop_proc();
		session_clear();
	}

	if (!paths_init(exe_dir, out->error, (int)sizeof(out->error))) {
		return 0;
	}

	make_secret(s_session.secret, (int)sizeof(s_session.secret));

	if (!write_config(config, cidrs, cidr_count, domains, domain_count, apps, app_count, exclude_lan, block_quic, selected,
		out->error, (int)sizeof(out->error))) {
		return 0;
	}

	/* Resolve the endpoint before TUN steals DNS. Ping uses this address only. */
	read_endpoint(config);
	resolve_endpoint_ip();

	if (!spawn_mihomo()) {
		snprintf(out->error, sizeof(out->error), "The tunnel could not be started.");
		return 0;
	}

	if (!wait_ready(out->error, (int)sizeof(out->error))) {
		log_line(out->error);
		stop_proc();
		return 0;
	}

	snprintf(s_session.slug, sizeof(s_session.slug), "%s", slug ? slug : "");
	s_session.wg = _strdup(config);
	s_session.active = 1;
	out->ok = 1;
	/* Connected waits for the HTTP probe on later status ticks — keep up() short. */
	fill_identity(out);
	snprintf(out->stage, sizeof(out->stage), "Opening the tunnel…");
	return 1;
}

/*
===============
win_tun_bypass
Rewrite config.yaml with the new rules and reload mihomo. If the reload is refused, restart it.
===============
*/
int win_tun_bypass(const char **cidrs, int cidr_count, const char **domains, int domain_count,
	const char **apps, int app_count, int exclude_lan, int block_quic, int selected, win_tun_result_t *out) {
	char		err[256];
	wchar_t		(*exes)[MAX_PATH];
	int			exe_count;
	int			i;

	memset(out, 0, sizeof(*out));
	exes = NULL;
	exe_count = 0;

	if (!s_session.active || !s_session.wg) {
		snprintf(out->error, sizeof(out->error), "The tunnel is not running.");
		return 0;
	}

	exes = (wchar_t (*)[MAX_PATH])calloc(APP_EXE_MAX, sizeof(*exes));

	if (exes) {
		exes_from_yaml(s_session.config_path, exes, &exe_count, APP_EXE_MAX);

		for (i = 0; i < app_count; i++) {
			remember_utf8(exes, &exe_count, APP_EXE_MAX, apps ? apps[i] : NULL);
		}
	}

	if (!write_config(s_session.wg, cidrs, cidr_count, domains, domain_count, apps, app_count, exclude_lan, block_quic, selected,
		out->error, (int)sizeof(out->error))) {
		free(exes);
		return 0;
	}

	if (!is_running()) {
		snprintf(out->error, sizeof(out->error), "The tunnel is not running.");
		free(exes);
		return 0;
	}

	/*
	 * Soft PUT /configs recreates proxies/DNS inside a live process and can leave
	 * apps on the physical path while the internal delay probe still succeeds.
	 * Restart so TUN, routes, and WireGuard come up together, then RST listed apps.
	 */
	log_line("fairlead: applying rules with mihomo restart");
	close_connections();
	stop_proc();

	if (!spawn_mihomo() || !wait_ready(err, (int)sizeof(err))) {
		snprintf(out->error, sizeof(out->error), "The tunnel could not be restarted.");
		stop_proc();
		session_clear();
		free(exes);
		return 0;
	}

	s_session.recoveries = 0;
	s_session.probe_fails = 0;
	s_session.last_probe = 0;
	s_session.last_ok = 0;
	s_session.last_delay_ms = 0;
	s_session.traffic_tick = 0;
	s_session.down_bps = 0;
	s_session.up_bps = 0;
	s_session.recover_until = time(NULL) + 45;
	reset_app_tcp(exes, exe_count);

	out->ok = 1;
	fill_identity(out);

	/* wait_ready already required path-ready; confirm before claiming Connected. */
	if (tunnel_path_ready()) {
		s_session.last_ok = time(NULL);
		s_session.recover_until = 0;
		out->handshake = (int)s_session.last_ok;
		out->delay_ms = 0;
		out->ping_ms = s_session.last_ping_ms;
	} else {
		snprintf(out->stage, sizeof(out->stage), "Checking the tunnel…");
	}

	free(exes);
	return 1;
}
