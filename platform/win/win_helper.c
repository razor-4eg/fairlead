//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// win_helper.c -- Elevated helper. Length-prefixed JSON over a named pipe, and the login task that restarts it.
//
//===============================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/fl.h"
#include "../../core/fl_json.h"
#include "win_tun.h"

static char s_token[80];
static char s_exe_dir[MAX_PATH];

/*
===============
read_full
Read n bytes or fail.
===============
*/
static int read_full(HANDLE pipe, void *buf, int n) {
	int		got;
	DWORD	chunk;

	got = 0;

	while (got < n) {
		chunk = 0;

		if (!ReadFile(pipe, (char *)buf + got, (DWORD)(n - got), &chunk, NULL) || chunk == 0) {
			return 0;
		}

		got += (int)chunk;
	}

	return 1;
}

/*
===============
write_full
Write n bytes or fail.
===============
*/
static int write_full(HANDLE pipe, const void *buf, int n) {
	int		sent;
	DWORD	chunk;

	sent = 0;

	while (sent < n) {
		chunk = 0;

		if (!WriteFile(pipe, (const char *)buf + sent, (DWORD)(n - sent), &chunk, NULL) || chunk == 0) {
			return 0;
		}

		sent += (int)chunk;
	}

	return 1;
}

/*
===============
append
Grow the reply and add raw text.
===============
*/
static void append(char **buf, int *len, int *cap, const char *text) {
	int		n;
	int		c;

	n = (int)strlen(text);

	if (*len + n + 1 > *cap) {
		c = *cap ? *cap * 2 : 1024;

		while (c < *len + n + 1) {
			c *= 2;
		}

		*buf = (char *)realloc(*buf, (size_t)c);
		*cap = c;
	}

	memcpy(*buf + *len, text, (size_t)n + 1);
	*len += n;
}

/*
===============
append_escaped
Append a JSON string onto the reply buffer.
===============
*/
static void append_escaped(char **buf, int *len, int *cap, const char *text) {
	const unsigned char	*p;
	char				tmp[8];

	append(buf, len, cap, "\"");

	for (p = (const unsigned char *)(text ? text : ""); *p; p++) {
		if (*p == '\\' || *p == '"') {
			tmp[0] = '\\';
			tmp[1] = (char)*p;
			tmp[2] = 0;
			append(buf, len, cap, tmp);
		} else if (*p == '\n') {
			append(buf, len, cap, "\\n");
		} else if (*p == '\r') {
			append(buf, len, cap, "\\r");
		} else if (*p == '\t') {
			append(buf, len, cap, "\\t");
		} else if (*p >= 32) {
			tmp[0] = (char)*p;
			tmp[1] = 0;
			append(buf, len, cap, tmp);
		}
	}

	append(buf, len, cap, "\"");
}

static char s_slug[32];
static int s_quit;

/*
===============
handle_request
One pipe message in, one JSON reply out. The caller frees it.
===============
*/
static char *handle_request(const char *body) {
	fl_arena_t			arena;
	fl_json_t			*root;
	const char			*op;
	const char			*token;
	const char			*id;
	const char			*name;
	const char			*config;
	const char			*policy;
	const fl_json_t		*cidrs;
	const char			*cidr_list[64];
	int					cidr_count;
	int					exclude;
	int					selected;
	win_tun_result_t	tun;
	char				*buf;
	int					len;
	int					cap;
	int					i;
	int					ok;
	const char			*error;
	const fl_json_t		*c;
	const char			*s;
	char				num[64];

	cidr_count = 0;
	exclude = 0;
	selected = 0;
	buf = NULL;
	len = 0;
	cap = 0;
	ok = 0;
	error = NULL;

	fl_arena_init(&arena, 1 << 20);
	root = fl_json_parse(&arena, body);

	if (!root) {
		fl_arena_free(&arena);
		return _strdup("{\"id\":\"\",\"ok\":false,\"error\":\"The config is damaged.\"}");
	}

	token = fl_json_string(fl_json_get(root, "token"));

	if (!token || strcmp(token, s_token) != 0) {
		fl_arena_free(&arena);
		return _strdup("{\"id\":\"\",\"ok\":false,\"error\":\"The helper is not running.\"}");
	}

	id = fl_json_string(fl_json_get(root, "id"));
	op = fl_json_string(fl_json_get(root, "op"));
	name = fl_json_string(fl_json_get(root, "name"));
	config = fl_json_string(fl_json_get(root, "config"));
	policy = fl_json_string(fl_json_get(root, "policy"));

	if (fl_json_get(root, "excludeLAN") && fl_json_get(root, "excludeLAN")->type == FL_JSON_BOOL) {
		exclude = fl_json_get(root, "excludeLAN")->boolean;
	}

	selected = policy && strcmp(policy, "selected") == 0;
	cidrs = fl_json_get(root, "cidrs");

	if (cidrs && cidrs->type == FL_JSON_ARR) {
		for (c = cidrs->child; c && cidr_count < 64; c = c->next) {
			s = fl_json_string(c);

			if (s) {
				cidr_list[cidr_count++] = s;
			}
		}
	}

	memset(&tun, 0, sizeof(tun));

	if (!op) {
		op = "";
	}

	if (strcmp(op, "ping") == 0 || strcmp(op, "tools") == 0) {
		ok = 1;
	} else if (strcmp(op, "status") == 0) {
		win_tun_status(&tun);
		ok = 1;
	} else if (strcmp(op, "up") == 0) {
		if (!name || !config) {
			error = "The config is empty.";
		} else if (win_tun_up(name, config, cidr_list, cidr_count, exclude, selected, s_exe_dir, &tun)) {
			snprintf(s_slug, sizeof(s_slug), "%s", name);
			ok = 1;
		} else {
			error = tun.error[0] ? tun.error : "amneziawg-go did not create an interface.";
		}
	} else if (strcmp(op, "down") == 0) {
		win_tun_down(&tun);
		s_slug[0] = '\0';
		ok = 1;
		memset(&tun, 0, sizeof(tun));
	} else if (strcmp(op, "quit") == 0) {
		win_tun_down(&tun);
		s_slug[0] = '\0';
		ok = 1;
		s_quit = 1;
		memset(&tun, 0, sizeof(tun));
	} else if (strcmp(op, "bypass") == 0) {
		if (!win_tun_bypass(cidr_list, cidr_count, exclude, selected, &tun)) {
			error = tun.error[0] ? tun.error : "The tunnel is not running.";
		} else {
			ok = 1;
		}
	} else {
		error = "Unknown command.";
	}

	append(&buf, &len, &cap, "{\"id\":");
	append_escaped(&buf, &len, &cap, id ? id : "");
	append(&buf, &len, &cap, ok ? ",\"ok\":true" : ",\"ok\":false");

	if (error) {
		append(&buf, &len, &cap, ",\"error\":");
		append_escaped(&buf, &len, &cap, error);
	}

	if (tun.warning_count) {
		append(&buf, &len, &cap, ",\"warnings\":[");

		for (i = 0; i < tun.warning_count; i++) {
			if (i) {
				append(&buf, &len, &cap, ",");
			}

			append_escaped(&buf, &len, &cap, tun.warnings[i]);
		}

		append(&buf, &len, &cap, "]");
	}

	append(&buf, &len, &cap, ",\"managed\":[");

	if (tun.iface[0] && s_slug[0]) {
		append(&buf, &len, &cap, "{\"name\":");
		append_escaped(&buf, &len, &cap, s_slug);
		append(&buf, &len, &cap, ",\"realInterface\":");
		append_escaped(&buf, &len, &cap, tun.iface);
		append(&buf, &len, &cap, ",\"gatewayV4\":");
		append_escaped(&buf, &len, &cap, tun.gateway_v4);
		append(&buf, &len, &cap, ",\"lanCIDRs\":[");

		for (i = 0; i < tun.lan_count; i++) {
			if (i) {
				append(&buf, &len, &cap, ",");
			}

			append_escaped(&buf, &len, &cap, tun.lan[i]);
		}

		append(&buf, &len, &cap, "],\"bypass\":[");

		for (i = 0; i < tun.owned_count; i++) {
			if (i) {
				append(&buf, &len, &cap, ",");
			}

			append_escaped(&buf, &len, &cap, tun.owned[i]);
		}

		append(&buf, &len, &cap, "],\"endpointIPs\":[");

		for (i = 0; i < tun.endpoint_count; i++) {
			if (i) {
				append(&buf, &len, &cap, ",");
			}

			append_escaped(&buf, &len, &cap, tun.endpoints[i]);
		}

		append(&buf, &len, &cap, "]}");
	}

	append(&buf, &len, &cap, "],\"devices\":[");

	if (tun.iface[0]) {
		append(&buf, &len, &cap, "{\"interface\":");
		append_escaped(&buf, &len, &cap, tun.iface);
		append(&buf, &len, &cap, ",\"publicKey\":");
		append_escaped(&buf, &len, &cap, tun.public_key);
		append(&buf, &len, &cap, ",\"peers\":[{\"endpoint\":");
		append_escaped(&buf, &len, &cap, tun.live_endpoint);
		snprintf(num, sizeof(num), ",\"handshake\":%d,\"rx\":%llu,\"tx\":%llu}]}", tun.handshake,
			(unsigned long long)tun.rx, (unsigned long long)tun.tx);
		append(&buf, &len, &cap, num);
	}

	append(&buf, &len, &cap, "]}");
	fl_arena_free(&arena);
	return buf;
}

/*
===============
wcat
Append wide text into a capped buffer.
===============
*/
static void wcat(wchar_t *dst, int cap, const wchar_t *src) {
	int		n;
	int		i;

	n = (int)wcslen(dst);
	i = 0;

	if (n >= cap) {
		return;
	}

	while (src[i] && n + 1 < cap) {
		dst[n++] = src[i++];
	}

	dst[n] = 0;
}

/*
===============
wescape
XML-escape into dst.
===============
*/
static void wescape(wchar_t *dst, int cap, const wchar_t *src) {
	int				n;
	const wchar_t	*rep;

	n = 0;

	for (; *src && n + 6 < cap; src++) {
		rep = NULL;

		if (*src == L'&') {
			rep = L"&amp;";
		} else if (*src == L'<') {
			rep = L"&lt;";
		} else if (*src == L'>') {
			rep = L"&gt;";
		} else if (*src == L'"') {
			rep = L"&quot;";
		}

		if (rep) {
			while (*rep && n + 1 < cap) {
				dst[n++] = *rep++;
			}
		} else {
			dst[n++] = *src;
		}
	}

	dst[n] = 0;
}

/* The first elevated start records a task. Later starts use schtasks /Run and do not ask again. */

/*
===============
register_task
Record the FairleadHelper task so the next launch does not ask again.
===============
*/
static DWORD WINAPI register_task(void *unused) {
	wchar_t				xml[8192];
	wchar_t				dir[MAX_PATH];
	wchar_t				exe[MAX_PATH];
	wchar_t				sam[300];
	wchar_t				sam_esc[600];
	wchar_t				tok[80];
	wchar_t				path[MAX_PATH];
	wchar_t				app[MAX_PATH];
	wchar_t				cmd[2048];
	wchar_t				domain[128];
	wchar_t				user[128];
	HANDLE				file;
	STARTUPINFOW		si;
	PROCESS_INFORMATION	pi;
	DWORD				wrote;
	DWORD				code;
	unsigned short		bom;
	char				note[96];
	FILE				*log;
	char				log_path[MAX_PATH];
	DWORD				envn;

	wchar_t exe_esc[MAX_PATH * 2];
	wchar_t dir_esc[MAX_PATH * 2];
	wrote = 0;
	code = 1;
	bom = 0xFEFF;

	(void)unused;

	if (!s_token[0] || !s_exe_dir[0]) {
		return 0;
	}

	MultiByteToWideChar(CP_UTF8, 0, s_exe_dir, -1, dir, MAX_PATH);
	wcscpy(exe, dir);
	wcscat(exe, L"\\fairlead-helper.exe");
	wescape(exe_esc, MAX_PATH * 2, exe);
	wescape(dir_esc, MAX_PATH * 2, dir);
	MultiByteToWideChar(CP_UTF8, 0, s_token, -1, tok, 80);
	sam[0] = 0;
	domain[0] = 0;
	user[0] = 0;

	if (GetEnvironmentVariableW(L"USERNAME", user, 128)) {
		if (GetEnvironmentVariableW(L"USERDOMAIN", domain, 128) && domain[0]) {
			wcscpy(sam, domain);
			wcscat(sam, L"\\");
			wcscat(sam, user);
		} else {
			wcscpy(sam, user);
		}
	}

	wescape(sam_esc, 600, sam);
	xml[0] = 0;
	wcat(xml, 8192, L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>");
	wcat(xml, 8192, L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">");
	wcat(xml, 8192, L"<Principals><Principal id=\"Author\">");

	if (sam[0]) {
		wcat(xml, 8192, L"<UserId>");
		wcat(xml, 8192, sam_esc);
		wcat(xml, 8192, L"</UserId>");
	}

	wcat(xml, 8192, L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>");
	wcat(xml, 8192, L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>");
	wcat(xml, 8192, L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>");
	wcat(xml, 8192, L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>");
	wcat(xml, 8192, L"<AllowHardTerminate>true</AllowHardTerminate>");
	wcat(xml, 8192, L"<AllowStartOnDemand>true</AllowStartOnDemand>");
	wcat(xml, 8192, L"<Enabled>true</Enabled><Hidden>true</Hidden>");
	wcat(xml, 8192, L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit></Settings>");
	wcat(xml, 8192, L"<Actions Context=\"Author\"><Exec><Command>");
	wcat(xml, 8192, exe_esc);
	wcat(xml, 8192, L"</Command><Arguments>--helper ");
	wcat(xml, 8192, tok);
	wcat(xml, 8192, L"</Arguments><WorkingDirectory>");
	wcat(xml, 8192, dir_esc);
	wcat(xml, 8192, L"</WorkingDirectory></Exec></Actions>");
	wcat(xml, 8192, L"<Triggers><TimeTrigger><StartBoundary>2099-01-01T00:00:00</StartBoundary><Enabled>false</Enabled></TimeTrigger></Triggers>");
	wcat(xml, 8192, L"</Task>");

	if (!GetTempPathW(MAX_PATH - 20, path)) {
		return 0;
	}

	wcscat(path, L"fairlead-task.xml");
	file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (file == INVALID_HANDLE_VALUE) {
		return 0;
	}

	WriteFile(file, &bom, 2, &wrote, NULL);
	WriteFile(file, xml, (DWORD)(wcslen(xml) * sizeof(wchar_t)), &wrote, NULL);
	CloseHandle(file);

	if (!GetSystemDirectoryW(app, MAX_PATH - 20)) {
		DeleteFileW(path);
		return 0;
	}

	wcscat(app, L"\\schtasks.exe");
	wcscpy(cmd, L"\"");
	wcscat(cmd, app);
	wcscat(cmd, L"\" /Create /F /TN FairleadHelper /XML \"");
	wcscat(cmd, path);
	wcscat(cmd, L"\"");
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;

	if (CreateProcessW(app, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
		WaitForSingleObject(pi.hProcess, 15000);
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}

	DeleteFileW(path);
	envn = GetEnvironmentVariableA("ProgramData", log_path, MAX_PATH);

	if (envn > 0 && envn < MAX_PATH - 24) {
		strncat(log_path, "\\Fairlead", MAX_PATH - strlen(log_path) - 1);
		CreateDirectoryA(log_path, NULL);
		strncat(log_path, "\\helper.log", MAX_PATH - strlen(log_path) - 1);
		log = fopen(log_path, "a");

		if (log) {
			snprintf(note, sizeof(note), "fairlead: scheduled task %lu", (unsigned long)code);
			fprintf(log, "%s\n", note);
			fclose(log);
		}
	}

	return 0;
}

/*
===============
helper_main
Serve the pipe until the app quits, or the pipe breaks.
===============
*/
int helper_main(const char *token, const char *exe_dir) {
	HANDLE				pipe;
	HANDLE				reg;
	SECURITY_ATTRIBUTES	sa;
	unsigned int		n;
	char				*body;
	char				*reply;
	int					rlen;

	PSECURITY_DESCRIPTOR sd = NULL;

	snprintf(s_token, sizeof(s_token), "%s", token ? token : "");
	snprintf(s_exe_dir, sizeof(s_exe_dir), "%s", exe_dir ? exe_dir : ".");
	win_tun_reconcile();

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;AU)", SDDL_REVISION_1, &sd, NULL)) {
		return 1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = sd;
	pipe = CreateNamedPipeA("\\\\.\\pipe\\fairlead", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1, 1 << 20, 1 << 20, 0, &sa);

	if (pipe == INVALID_HANDLE_VALUE) {
		LocalFree(sd);
		return 1;
	}

	reg = CreateThread(NULL, 0, register_task, NULL, 0, NULL);

	if (reg) {
		CloseHandle(reg);
	}

	for (;;) {
		if (!ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
			break;
		}

		{
			unsigned char hdr[4];
			n = 0;
			body = NULL;
			reply = NULL;

			if (read_full(pipe, hdr, 4)) {
				n = ((unsigned int)hdr[0] << 24) | ((unsigned int)hdr[1] << 16) | ((unsigned int)hdr[2] << 8) | hdr[3];

				if (n > 0 && n < 2000000) {
					body = (char *)malloc((size_t)n + 1);

					if (body && read_full(pipe, body, (int)n)) {
						unsigned char oh[4];

						body[n] = '\0';
						reply = handle_request(body);
						rlen = reply ? (int)strlen(reply) : 0;
						oh[0] = (unsigned char)((rlen >> 24) & 0xff);
						oh[1] = (unsigned char)((rlen >> 16) & 0xff);
						oh[2] = (unsigned char)((rlen >> 8) & 0xff);
						oh[3] = (unsigned char)(rlen & 0xff);
						write_full(pipe, oh, 4);

						if (rlen) {
							write_full(pipe, reply, rlen);
						}
					}
				}
			}

			free(body);
			free(reply);
		}

		FlushFileBuffers(pipe);
		DisconnectNamedPipe(pipe);

		if (s_quit) {
			break;
		}
	}

	CloseHandle(pipe);
	LocalFree(sd);
	return 0;
}
