#include "pch.h"
#include "session.h"

#include "coreabi.h"
#include "l10n.h"
#include "text.h"
#include "base.h"

#include <algorithm>
#include <filesystem>

namespace {

std::string hex_id(int bytes) {
	unsigned char raw[16]{};
	if (bytes > 16) {
		bytes = 16;
	}

	if (BCryptGenRandom(nullptr, raw, (ULONG)bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
		return "0000000000000000";
	}

	static char const* hex = "0123456789abcdef";
	std::string out(bytes * 2, '0');
	for (int i = 0; i < bytes; i++) {
		out[i * 2] = hex[raw[i] >> 4];
		out[i * 2 + 1] = hex[raw[i] & 0xf];
	}

	return out;
}

std::string json_str(json_t const* obj, char const* key) {
	char const* text = json_string(json_get(obj, key));
	return text ? text : "";
}

bool json_bool(json_t const* obj, char const* key, bool fallback) {
	json_t const* value = json_get(obj, key);
	if (!value) {
		return fallback;
	}

	if (value->type == JSON_BOOL) {
		return value->boolean != 0;
	}

	char const* text = json_string(value);
	return text && strcmp(text, "true") == 0;
}

uint64_t json_u64(json_t const* obj, char const* key) {
	char const* text = json_string(json_get(obj, key));
	return text ? _strtoui64(text, nullptr, 10) : 0;
}

int json_int(json_t const* obj, char const* key) {
	char const* text = json_string(json_get(obj, key));
	return text ? atoi(text) : 0;
}

std::string bytes_of(double n) {
	std::string units[] = { l10n::text("B"), l10n::text("KB"), l10n::text("MB"), l10n::text("GB") };
	int i = 0;
	if (n < 0) {
		n = 0;
	}

	while (n >= 1024 && i < 3) {
		n /= 1024;
		i++;
	}

	char buf[64];
	if (i == 0) {
		snprintf(buf, sizeof(buf), "%llu", (unsigned long long)n);
	} else if (n >= 10) {
		snprintf(buf, sizeof(buf), "%.0f", n);
	} else {
		snprintf(buf, sizeof(buf), "%.1f", n);
	}

	return std::string(buf) + " " + units[i];
}

std::string rate_of(double n) {
	return bytes_of(n < 0 ? 0 : n) + l10n::text("/s");
}

std::string ago_of(int stamp) {
	if (stamp <= 0) {
		return l10n::text("none");
	}

	auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	int seconds = (int)now - stamp;
	if (seconds < 5) {
		return l10n::text("just now");
	}

	if (seconds < 60) {
		return l10n::format("%d s ago", seconds);
	}

	if (seconds < 3600) {
		return l10n::format("%d min ago", seconds / 60);
	}

	if (seconds < 86400) {
		return l10n::format("%d h ago", seconds / 3600);
	}

	return l10n::text("a while ago");
}

/* Idle tunnels carry display strings; rewrite them when the language changes. */
void localize_idle(Tunnel& tunnel) {
	if (tunnel.lan_default) {
		tunnel.lan = l10n::text("Private LAN ranges stay off the tunnel when this is on.");
	}

	if (tunnel.phase == "Off") {
		tunnel.detail = l10n::text("Tunnel is off");
		tunnel.down = rate_of(0);
		tunnel.up = rate_of(0);
		tunnel.rx = bytes_of(0);
		tunnel.tx = bytes_of(0);
	} else if (tunnel.phase == "Disconnecting") {
		tunnel.detail = l10n::text("Stopping the tunnel…");
	}
}

std::wstring module_dir() {
	wchar_t self[MAX_PATH];
	if (!GetModuleFileNameW(nullptr, self, MAX_PATH)) {
		return L".";
	}

	std::wstring path = self;
	auto slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos) {
		return L".";
	}

	path.resize(slash);
	return path;
}

std::string tool_dir() {
	std::wstring dir = module_dir();
	std::wstring parent = dir;
	auto slash = parent.find_last_of(L"\\/");
	if (slash != std::wstring::npos) {
		parent.resize(slash);
		if (std::filesystem::exists(std::filesystem::path(parent) / L"fairlead-helper.exe")) {
			return narrow(parent);
		}
	}

	return narrow(dir);
}

std::string appdata_dir() {
	PWSTR raw = nullptr;
	std::wstring path;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)) && raw) {
		path = raw;
		CoTaskMemFree(raw);
	}

	if (path.empty()) {
		path = L".";
	}

	path += L"\\Fairlead";
	std::error_code error;
	std::filesystem::create_directories(std::filesystem::path(path), error);
	return narrow(path);
}

bool pipe_xfer(HANDLE pipe, void* buf, int n, bool reading, DWORD timeout_ms) {
	int got = 0;
	while (got < n) {
		OVERLAPPED ov{};
		ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!ov.hEvent) {
			return false;
		}

		DWORD chunk = 0;
		BOOL ok = reading
			? ReadFile(pipe, (char*)buf + got, (DWORD)(n - got), &chunk, &ov)
			: WriteFile(pipe, (char*)buf + got, (DWORD)(n - got), &chunk, &ov);
		if (!ok) {
			if (GetLastError() != ERROR_IO_PENDING) {
				CloseHandle(ov.hEvent);
				return false;
			}

			DWORD wait = WaitForSingleObject(ov.hEvent, timeout_ms);
			if (wait != WAIT_OBJECT_0) {
				CancelIoEx(pipe, &ov);
				WaitForSingleObject(ov.hEvent, 1000);
				CloseHandle(ov.hEvent);
				return false;
			}

			if (!GetOverlappedResult(pipe, &ov, &chunk, FALSE) || chunk == 0) {
				CloseHandle(ov.hEvent);
				return false;
			}
		} else if (chunk == 0) {
			CloseHandle(ov.hEvent);
			return false;
		}

		CloseHandle(ov.hEvent);
		got += (int)chunk;
	}

	return true;
}

bool read_full(HANDLE pipe, void* buf, int n, DWORD timeout_ms) {
	return pipe_xfer(pipe, buf, n, true, timeout_ms);
}

bool write_full(HANDLE pipe, void* buf, int n, DWORD timeout_ms) {
	return pipe_xfer(pipe, buf, n, false, timeout_ms);
}

void read_lists(json_t* item, Tunnel* tunnel) {
	if (json_t* lists = json_get(item, "routeLists")) {
		for (json_t* one = lists->child; one; one = one->next) {
			auto list = std::make_unique<RouteList>();
			list->id = json_str(one, "id");
			list->name = json_str(one, "name");
			list->enabled = json_bool(one, "enabled", true);
			if (list->id.empty()) {
				list->id = hex_id(8);
			}

			if (json_t* entries = json_get(one, "entries")) {
				for (json_t* entry = entries->child; entry; entry = entry->next) {
					RouteEntry row;
					row.id = json_str(entry, "id");
					row.kind = json_str(entry, "kind");
					row.value = json_str(entry, "value");
					if (row.id.empty()) {
						row.id = hex_id(8);
					}

					list->entries.push_back(std::move(row));
				}
			}

			tunnel->lists.push_back(std::move(list));
		}

		return;
	}

	if (json_t* ips = json_get(item, "ipRules")) {
		for (json_t* one = ips->child; one; one = one->next) {
			auto list = std::make_unique<RouteList>();
			std::string cidr = json_str(one, "cidr");
			std::string note = trim_copy(json_str(one, "note"));
			list->id = json_str(one, "id");
			list->name = note.empty() ? cidr : note;
			list->enabled = json_bool(one, "enabled", true);
			if (list->id.empty()) {
				list->id = hex_id(8);
			}

			RouteEntry row;
			row.id = hex_id(8);
			row.kind = "ip";
			row.value = cidr;
			list->entries.push_back(std::move(row));
			tunnel->lists.push_back(std::move(list));
		}
	}

	if (json_t* domains = json_get(item, "domainRules")) {
		for (json_t* one = domains->child; one; one = one->next) {
			auto list = std::make_unique<RouteList>();
			std::string domain = json_str(one, "name");
			std::string note = trim_copy(json_str(one, "note"));
			list->id = json_str(one, "id");
			list->name = note.empty() ? domain : note;
			list->enabled = json_bool(one, "enabled", true);
			if (list->id.empty()) {
				list->id = hex_id(8);
			}

			RouteEntry row;
			row.id = hex_id(8);
			row.kind = "domain";
			row.value = domain;
			list->entries.push_back(std::move(row));
			tunnel->lists.push_back(std::move(list));
		}
	}
}

std::unique_ptr<Tunnel> new_tunnel(std::string const& name, std::string const& config) {
	auto tunnel = std::make_unique<Tunnel>();
	tunnel->id = hex_id(8);
	tunnel->name = name;
	tunnel->slug = "fld" + hex_id(4);
	tunnel->config = config;
	tunnel->exclude_lan = true;
	return tunnel;
}

}

Session::Session() {
	WSADATA data{};
	WSAStartup(MAKEWORD(2, 2), &data);
	coreabi::load();
	dir = appdata_dir();
	exe_dir = tool_dir();
	l10n::set_root(module_dir() + L"\\l10n");
	token = LoadToken();
	Load();
	for (auto const& tunnel : tunnels) {
		localize_idle(*tunnel);
	}

	RefreshTools();
	selected = ResolveDefault();
	if (!selected && !tunnels.empty()) {
		selected = tunnels[0].get();
	}
}

std::string Session::HelperText() const {
	return l10n::text(helper_online ? "Helper is running" : "Helper is not running");
}

std::string Session::PolicyTitle() const {
	return l10n::text(selected && selected->selected_policy ? "Through the tunnel" : "Direct");
}

std::string Session::PolicyBody() const {
	return l10n::text(selected && selected->selected_policy
		? "Only these addresses use the tunnel. Everything else stays on the regular network."
		: "These addresses stay out of the tunnel and use the regular network.");
}

std::vector<Tunnel*> Session::Filtered() const {
	std::wstring query_text = trim_w(widen(query));
	std::vector<Tunnel*> out;
	for (auto const& tunnel : tunnels) {
		if (query_text.empty() || icontains(widen(tunnel->name), query_text) || icontains(widen(tunnel->endpoint), query_text)) {
			out.push_back(tunnel.get());
		}
	}

	return out;
}

Tunnel* Session::Find(std::string const& id) const {
	for (auto const& tunnel : tunnels) {
		if (tunnel->id == id) {
			return tunnel.get();
		}
	}

	return nullptr;
}

Tunnel* Session::ResolveDefault() const {
	std::string key = trim_copy(default_id);
	if (key.empty()) {
		return nullptr;
	}

	std::string id = key == last_key ? last_active_id : key;
	return id.empty() ? nullptr : Find(id);
}

void Session::RefreshLabels(Tunnel* tunnel) {
	if (!tunnel) {
		return;
	}

	std::string text = coreabi::inspect(tunnel->config);
	arena_t arena;
	arena_init(&arena, 1 << 16);
	json_t* root = json_parse(&arena, text.c_str());
	if (root) {
		std::string endpoint = json_str(root, "endpoint");
		tunnel->endpoint = endpoint.empty() ? l10n::text("no endpoint") : endpoint;
		tunnel->address = json_str(root, "address");
		tunnel->amnezia = json_bool(root, "amnezia", false);
	}

	arena_free(&arena);
}

void Session::RefreshTools() {
	if (!coreabi::ready()) {
		notice = l10n::format("fairlead-core.dll is missing from %@", narrow(coreabi::dll_path()));
		awg_ok = false;
		awg_text = "engine  " + l10n::text("not found");
		return;
	}

	std::filesystem::path awg = std::filesystem::path(widen(exe_dir)) / L"bin" / L"mihomo.exe";
	awg_ok = std::filesystem::exists(awg);
	awg_text = awg_ok ? narrow(awg.wstring()) : "engine  " + l10n::text("not found");
	if (!helper_online) {
		notice = l10n::text("The system helper is not running. A tunnel cannot start without it.");
	} else if (selected && !awg_ok) {
		notice = l10n::text("This copy of Fairlead is missing the tunnel engine.");
	} else {
		notice.clear();
	}
}

std::string Session::Call(std::string const& body, int connect_ms) {
	std::unique_lock<std::timed_mutex> lock(pipe, std::chrono::milliseconds(connect_ms + 1000));
	if (!lock.owns_lock()) {
		return {};
	}

	DWORD timeout = (DWORD)std::max(50, connect_ms);
	WaitNamedPipeW(L"\\\\.\\pipe\\fairlead", timeout);
	/* OVERLAPPED so status ticks can time out — sync ReadFile wedged metrics forever. */
	HANDLE pipe_handle = CreateFileW(L"\\\\.\\pipe\\fairlead", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
	if (pipe_handle == INVALID_HANDLE_VALUE) {
		return {};
	}

	std::string reply;
	uint32_t n = (uint32_t)body.size();
	unsigned char header[4] = { (unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n };
	if (write_full(pipe_handle, header, 4, timeout) &&
		(n == 0 || write_full(pipe_handle, const_cast<char*>(body.data()), (int)body.size(), timeout)) &&
		read_full(pipe_handle, header, 4, timeout)) {
		int size = (header[0] << 24) | (header[1] << 16) | (header[2] << 8) | header[3];
		if (size >= 0 && size <= 2000000) {
			reply.resize(size);
			if (size > 0 && !read_full(pipe_handle, reply.data(), size, timeout)) {
				reply.clear();
			}
		}
	}

	CloseHandle(pipe_handle);
	return reply;
}

std::string Session::Rpc(char const* op, Tunnel* tunnel, bool with_config) {
	std::string body = "{\"token\":" + json_quote(token) + ",\"op\":" + json_quote(op);
	body += ",\"name\":" + json_quote(tunnel->slug);
	if (with_config && strcmp(op, "up") == 0) {
		body += ",\"config\":" + json_quote(tunnel->config);
	}

	if (strcmp(op, "up") == 0 || strcmp(op, "bypass") == 0) {
		body += tunnel->selected_policy ? ",\"policy\":\"selected\"" : ",\"policy\":\"exceptions\"";
		body += tunnel->exclude_lan ? ",\"excludeLAN\":true" : ",\"excludeLAN\":false";
		/* mihomo resolves domains and matches processes itself. Only literal addresses go as cidrs. */
		auto emit = [&](char const* key, auto&& fill) {
			bool first = true;
			body += ",\"";
			body += key;
			body += "\":[";
			fill([&](std::string const& value) {
				if (!first) {
					body += ',';
				}

				first = false;
				body += json_quote(value);
			});
			body += "]";
		};
		emit("cidrs", [&](auto&& add) {
			for (auto const& list : tunnel->lists) {
				if (!list->enabled) {
					continue;
				}

				for (auto const& entry : list->entries) {
					if (entry.kind == "ip") {
						add(entry.value);
					}
				}
			}
		});
		emit("domains", [&](auto&& add) {
			for (auto const& list : tunnel->lists) {
				if (!list->enabled) {
					continue;
				}

				for (auto const& entry : list->entries) {
					if (entry.kind == "domain") {
						add(entry.value);
					}
				}
			}
		});
		emit("apps", [&](auto&& add) {
			for (auto const& app : tunnel->apps) {
				if (app->enabled && !app->path.empty()) {
					add(app->path);
				}
			}
		});
	}

	body += "}";
	return body;
}

std::string Session::StatusBody() const {
	return "{\"token\":" + json_quote(token) + ",\"op\":\"status\"}";
}

std::string Session::QuitBody() const {
	return "{\"token\":" + json_quote(token) + ",\"op\":\"quit\"}";
}

bool Session::ReplyOk(std::string const& reply) const {
	return reply.find("\"ok\":true") != std::string::npos;
}

std::string Session::ErrorOf(std::string const& reply) const {
	if (reply.empty()) {
		return {};
	}

	arena_t arena;
	arena_init(&arena, (int)std::max<size_t>(reply.size() * 4, 4096));
	json_t* root = json_parse(&arena, reply.c_str());
	std::string error = root ? json_str(root, "error") : "";
	arena_free(&arena);
	return error;
}

void Session::ApplyStatus(std::string const& reply) {
	Tunnel* live = nullptr;
	for (auto const& tunnel : tunnels) {
		if (tunnel->connected || tunnel->phase == "Connecting") {
			live = tunnel.get();
			break;
		}
	}

	if (!live || reply.empty()) {
		return;
	}

	arena_t arena;
	arena_init(&arena, (int)std::max<size_t>(reply.size() * 8, 1 << 16));
	json_t* root = json_parse(&arena, reply.c_str());
	if (!root) {
		arena_free(&arena);
		return;
	}

	if (json_t* devices = json_get(root, "devices"); devices && devices->child) {
		if (json_t* peers = json_get(devices->child, "peers"); peers && peers->child) {
			json_t* peer = peers->child;
			uint64_t rx = json_u64(peer, "rx");
			uint64_t tx = json_u64(peer, "tx");
			json_t* down_node = json_get(peer, "downRate");
			json_t* up_node = json_get(peer, "upRate");
			int64_t now = (int64_t)GetTickCount64();
			live->handshake = json_int(peer, "handshake");
			live->delay_ms = json_int(peer, "delay");
			live->ping_ms = json_int(peer, "ping");
			std::string endpoint = json_str(peer, "endpoint");
			if (!endpoint.empty()) {
				live->endpoint = endpoint;
			}

			/*
			 * Same as Mac absorb(): prefer helper rates when present; else rx/tx deltas.
			 * Never wipe totals/rates on a handshake blip — that made the UI look frozen at 0.
			 */
			if (down_node && up_node) {
				live->down = rate_of((double)json_u64(peer, "downRate"));
				live->up = rate_of((double)json_u64(peer, "upRate"));
			} else if (live->sample_tick != 0 && now > live->sample_tick &&
				rx >= live->sample_rx && tx >= live->sample_tx) {
				double dt = (now - live->sample_tick) / 1000.0;
				if (dt >= 0.2) {
					double down = (double)(rx - live->sample_rx) / dt;
					double up = (double)(tx - live->sample_tx) / dt;
					double cap = 10.0 * 1024.0 * 1024.0 * 1024.0;
					live->down = rate_of(down > cap ? 0 : down);
					live->up = rate_of(up > cap ? 0 : up);
				}
			} else if (rx < live->sample_rx || tx < live->sample_tx) {
				live->down = rate_of(0);
				live->up = rate_of(0);
			}

			live->sample_rx = rx;
			live->sample_tx = tx;
			live->sample_tick = now;
			live->rx = bytes_of((double)rx);
			live->tx = bytes_of((double)tx);

			if (live->handshake > 0) {
				live->connected = true;
				live->phase = "Connected";
				live->detail = live->ping_ms > 0
					? l10n::format("Reachable · %d ms", live->ping_ms)
					: l10n::text("Reachable");
			} else if (live->phase == "Connecting" || live->connected) {
				live->connected = false;
				live->phase = "Connecting";
				std::string stage = json_str(peer, "stage");
				live->detail = stage.empty() ? l10n::text("Checking the tunnel…") : l10n::display(stage);
				live->delay_ms = 0;
			}
		}
	}

	if (json_t* managed = json_get(root, "managed"); managed && managed->child) {
		json_t* item = managed->child;
		std::string iface = json_str(item, "realInterface");
		std::string gateway = json_str(item, "gatewayV4");
		/* iface is WireGuard/AmneziaWG; gatewayV4 carries the peer endpoint IP. */
		live->iface = iface.empty() ? "" : (gateway.empty() ? iface : iface + "  \xC2\xB7  " + gateway);
		if (json_t* lans = json_get(item, "lanCIDRs"); lans && lans->child) {
			std::string joined;
			for (json_t* lan = lans->child; lan; lan = lan->next) {
				if (char const* text = json_string(lan)) {
					if (!joined.empty()) {
						joined += ", ";
					}

					joined += text;
				}
			}

			if (!joined.empty()) {
				live->lan = joined;
				live->lan_default = false;
			}
		}
	}

	arena_free(&arena);
}

void Session::BeginConnect(Tunnel* tunnel) {
	banner.clear();
	tunnel->phase = "Connecting";
	tunnel->detail = l10n::text("Opening the tunnel…");
	tunnel->connected = false;
	tunnel->handshake = 0;
	tunnel->down = rate_of(0);
	tunnel->up = rate_of(0);
	tunnel->rx = bytes_of(0);
	tunnel->tx = bytes_of(0);
	tunnel->sample_rx = 0;
	tunnel->sample_tx = 0;
	tunnel->sample_tick = 0;
}

bool Session::FinishConnect(Tunnel* tunnel, std::string const& reply) {
	if (reply.empty() || !ReplyOk(reply)) {
		tunnel->phase = "Off";
		tunnel->connected = false;
		tunnel->detail = l10n::text("Tunnel is off");
		banner = l10n::display(ErrorOf(reply));
		if (banner.empty()) {
			banner = l10n::text("The tunnel did not start.");
		}

		return false;
	}

	tunnel->connected = false;
	tunnel->phase = "Connecting";
	tunnel->detail = l10n::text("Opening the tunnel…");
	ApplyStatus(reply);
	RememberLast(tunnel->id);
	return true;
}

void Session::BeginDisconnect(Tunnel* tunnel) {
	tunnel->phase = "Disconnecting";
	tunnel->detail = l10n::text("Stopping the tunnel…");
}

void Session::FinishDisconnect(Tunnel* tunnel) {
	tunnel->connected = false;
	tunnel->phase = "Off";
	tunnel->detail = l10n::text("Tunnel is off");
	tunnel->iface.clear();
	tunnel->handshake = 0;
	tunnel->down = rate_of(0);
	tunnel->up = rate_of(0);
	tunnel->rx = bytes_of(0);
	tunnel->tx = bytes_of(0);
	tunnel->sample_rx = 0;
	tunnel->sample_tx = 0;
	tunnel->sample_tick = 0;
}

std::unique_ptr<Tunnel> Session::MakeDraft() {
	auto tunnel = new_tunnel(l10n::format("Tunnel %d", (int)tunnels.size() + 1), coreabi::blank());
	localize_idle(*tunnel);
	RefreshLabels(tunnel.get());
	return tunnel;
}

void Session::Adopt(std::unique_ptr<Tunnel> tunnel) {
	if (!tunnel) {
		return;
	}

	selected = tunnel.get();
	tunnels.push_back(std::move(tunnel));
}

bool Session::ImportFile(std::string const& path) {
	std::string text = read_file(widen(path));
	if (text.empty() && !std::filesystem::exists(std::filesystem::path(widen(path)))) {
		banner = l10n::text("Could not read that file.");
		return false;
	}

	std::string config;
	std::string name;
	int code = coreabi::import_text(text, config, name);
	if (code != 0 || config.empty()) {
		banner = l10n::text(code == 1 ? "That file is not a WireGuard or AmneziaWG config." : "The Amnezia share could not be read.");
		return false;
	}

	if (name.empty()) {
		name = narrow(std::filesystem::path(widen(path)).stem().wstring());
	}

	auto tunnel = new_tunnel(name, config);
	localize_idle(*tunnel);
	RefreshLabels(tunnel.get());
	selected = tunnel.get();
	tunnels.push_back(std::move(tunnel));
	Save();
	return true;
}

void Session::Delete(Tunnel* tunnel) {
	if (!tunnel) {
		return;
	}

	if (default_id == tunnel->id) {
		default_id.clear();
	}

	if (last_active_id == tunnel->id) {
		last_active_id.clear();
	}

	bool was = selected == tunnel;
	std::erase_if(tunnels, [&](std::unique_ptr<Tunnel> const& item) { return item.get() == tunnel; });
	if (was) {
		selected = tunnels.empty() ? nullptr : tunnels[0].get();
	}

	Save();
}

void Session::SaveConfig(Tunnel* tunnel, std::string config) {
	tunnel->config = std::move(config);
	RefreshLabels(tunnel);
	Save();
}

void Session::RememberLast(std::string const& id) {
	if (last_active_id == id) {
		return;
	}

	last_active_id = id;
	Save();
}

void Session::SetPolicy(bool through) {
	if (!selected) {
		return;
	}

	selected->selected_policy = through;
	Save();
}

void Session::SetLan(bool on) {
	if (!selected) {
		return;
	}

	selected->exclude_lan = on;
	Save();
}

void Session::SaveRules() {
	Save();
}

BuiltList Session::BuildList(std::string const& name, std::string const& text, RouteList* replacing) {
	BuiltList built;
	std::string title = trim_copy(name);
	if (!selected || title.empty()) {
		return built;
	}

	auto list = std::make_unique<RouteList>();
	list->id = replacing ? replacing->id : hex_id(8);
	list->name = title;
	list->enabled = replacing ? replacing->enabled : true;
	std::vector<std::string> seen;
	for (auto const& existing : selected->lists) {
		if (replacing && existing.get() == replacing) {
			continue;
		}

		for (auto const& entry : existing->entries) {
			seen.push_back(entry.kind + " " + entry.value);
		}
	}

	bool reported = false;
	std::string normalized = text;
	for (char& c : normalized) {
		if (c == '\r') {
			c = '\n';
		}
	}

	size_t at = 0;
	while (at <= normalized.size()) {
		size_t next = normalized.find('\n', at);
		std::string line = trim_copy(normalized.substr(at, next == std::string::npos ? std::string::npos : next - at));
		at = next == std::string::npos ? normalized.size() + 1 : next + 1;
		if (line.empty()) {
			continue;
		}

		if (list->entries.size() >= 4096) {
			built.problems.push_back({ line, reported ? "" : l10n::text("A list can hold 4096 entries.") });
			reported = true;
			continue;
		}

		std::string kind, value, note, error;
		coreabi::classify(line, kind, value, note, error);
		if (!note.empty() || kind == "error" || kind.empty()) {
			built.problems.push_back({ line, error.empty() ? l10n::text("Enter an IP, a network, or a domain.") : l10n::display(error) });
			continue;
		}

		std::string key = kind + " " + value;
		if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
			built.problems.push_back({ line, l10n::text(kind == "domain" ? "That domain is already listed." : "That address is already listed.") });
			continue;
		}

		seen.push_back(key);
		RouteEntry entry;
		entry.id = hex_id(8);
		entry.kind = kind;
		entry.value = value;
		list->entries.push_back(std::move(entry));
	}

	if (list->entries.empty()) {
		if (built.problems.empty()) {
			built.problems.push_back({ "", l10n::text("Enter an IP, a network, or a domain.") });
		}

		return built;
	}

	built.list = std::move(list);
	return built;
}

void Session::CommitNew(std::unique_ptr<RouteList> list) {
	if (!selected || !list) {
		return;
	}

	selected->lists.push_back(std::move(list));
	Save();
}

void Session::CommitUpdate(RouteList* list, std::string name, std::vector<RouteEntry> entries) {
	if (!list) {
		return;
	}

	list->name = trim_copy(name);
	list->entries = std::move(entries);
	Save();
}

bool Session::AddProgram(std::string const& path, std::string const& name) {
	if (!selected || selected->apps.size() >= 12 || path.empty()) {
		return false;
	}

	for (auto const& app : selected->apps) {
		if (iequals(widen(app->path), widen(path))) {
			return false;
		}
	}

	auto rule = std::make_unique<AppRule>();
	rule->id = hex_id(8);
	rule->path = path;
	rule->name = trim_copy(name);
	if (rule->name.empty()) {
		rule->name = narrow(std::filesystem::path(widen(path)).filename().wstring());
	}

	rule->enabled = true;
	selected->apps.push_back(std::move(rule));
	Save();
	return true;
}

void Session::SetLaunch(bool on) {
	launch_at_login = on;
	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
		if (on) {
			wchar_t exe[MAX_PATH];
			if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
				std::wstring value = L"\"" + std::wstring(exe) + L"\"";
				RegSetValueExW(key, L"Fairlead", 0, REG_SZ, (BYTE const*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
			}
		} else {
			RegDeleteValueW(key, L"Fairlead");
		}

		RegCloseKey(key);
	}

	Save();
}

void Session::SetStartHidden(bool on) {
	start_hidden = on;
	Save();
}

void Session::SetLanguage(std::string const& pref) {
	l10n::set_preference(pref);
	language = l10n::preference();
	Relocalize();
	Save();
}

/* Rewrites stored display strings after the language changes. Live tunnels refresh on the next status tick. */
void Session::Relocalize() {
	for (auto const& tunnel : tunnels) {
		localize_idle(*tunnel);
		if (!tunnel->connected) {
			RefreshLabels(tunnel.get());
		}
	}

	RefreshTools();
}

void Session::SetDefault(std::string const& id) {
	std::string key = trim_copy(id);
	if (key.empty()) {
		default_id.clear();
	} else if (key == last_key) {
		default_id = last_key;
	} else if (Find(key)) {
		default_id = key;
	} else {
		default_id.clear();
	}

	Save();
}

void Session::OpenFolder() const {
	ShellExecuteW(nullptr, L"open", widen(dir).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool Session::Ping() {
	return ReplyOk(Call("{\"token\":" + json_quote(token) + ",\"op\":\"ping\"}", 400));
}

bool Session::RunScheduled() const {
	wchar_t system[MAX_PATH];
	if (!GetSystemDirectoryW(system, MAX_PATH)) {
		return false;
	}

	std::wstring exe = std::wstring(system) + L"\\schtasks.exe";
	std::wstring cmd = L"\"" + exe + L"\" /Run /TN FairleadHelper";
	STARTUPINFOW info{};
	info.cb = sizeof(info);
	info.dwFlags = STARTF_USESHOWWINDOW;
	info.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &info, &process)) {
		return false;
	}

	DWORD wait = WaitForSingleObject(process.hProcess, 8000);
	DWORD code = 1;
	if (wait == WAIT_OBJECT_0) {
		GetExitCodeProcess(process.hProcess, &code);
	}

	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return wait == WAIT_OBJECT_0 && code == 0;
}

void Session::StopHelpers() const {
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) {
		return;
	}

	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	if (Process32FirstW(snap, &entry)) {
		do {
			if (!iequals(entry.szExeFile, L"fairlead-helper.exe")) {
				continue;
			}

			HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
			if (!process) {
				continue;
			}

			TerminateProcess(process, 1);
			WaitForSingleObject(process, 4000);
			CloseHandle(process);
		} while (Process32NextW(snap, &entry));
	}

	CloseHandle(snap);
}

bool Session::LaunchHelper(void* hwnd, std::string& error) {
	error.clear();
	std::wstring exe = widen(exe_dir) + L"\\fairlead-helper.exe";
	if (!std::filesystem::exists(std::filesystem::path(exe))) {
		error = l10n::format("fairlead-helper.exe is missing from %@", exe_dir);
		return false;
	}

	BOOL admin = FALSE;
	SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
	PSID group = nullptr;
	if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
		CheckTokenMembership(nullptr, group, &admin);
		FreeSid(group);
	}

	std::wstring args = L"--helper " + widen(token);
	std::wstring directory = widen(exe_dir);
	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS;
	info.hwnd = (HWND)hwnd;
	info.lpVerb = admin ? L"open" : L"runas";
	info.lpFile = exe.c_str();
	info.lpParameters = args.c_str();
	info.lpDirectory = directory.c_str();
	info.nShow = SW_HIDE;
	if (!ShellExecuteExW(&info)) {
		error = l10n::text("The helper start was cancelled.");
		return false;
	}

	if (info.hProcess) {
		CloseHandle(info.hProcess);
	}

	Sleep(300);
	return true;
}

void Session::MarkHelper(bool online) {
	helper_online = online;
	RefreshTools();
}

void Session::ClearBanner() {
	banner.clear();
}

std::string Session::LoadToken() {
	std::wstring path = widen(dir) + L"\\helper.token";
	std::string text = trim_copy(read_file(path));
	if (text.size() > 8) {
		return text;
	}

	text = hex_id(16);
	write_file(path, text);
	return text;
}

void Session::Load() {
	std::string text = read_file(widen(dir) + L"\\tunnels.json");
	if (text.empty()) {
		return;
	}

	arena_t arena;
	size_t cap = text.size() * 16 + (1 << 20);
	if (cap < (1 << 16)) {
		cap = 1 << 16;
	}

	if (cap > (32u << 20)) {
		cap = 32u << 20;
	}

	arena_init(&arena, (int)cap);
	json_t* root = json_parse(&arena, text.c_str());
	if (!root) {
		arena_free(&arena);
		return;
	}

	launch_at_login = json_bool(root, "launchAtLogin", false);
	start_hidden = json_bool(root, "startHidden", false);
	language = trim_copy(json_str(root, "language"));
	if (language.empty()) {
		language = "auto";
	}

	l10n::set_preference(language);
	language = l10n::preference();
	default_id = json_str(root, "defaultTunnel");
	last_active_id = json_str(root, "lastActiveTunnel");
	json_t* list = json_get(root, "tunnels");
	if (list) {
		for (json_t* item = list->child; item && tunnels.size() < 24; item = item->next) {
			auto tunnel = std::make_unique<Tunnel>();
			tunnel->id = json_str(item, "id");
			tunnel->name = json_str(item, "name");
			tunnel->slug = json_str(item, "slug");
			tunnel->config = json_str(item, "configText");
			tunnel->exclude_lan = json_bool(item, "excludeLAN", true);
			tunnel->selected_policy = json_str(item, "routePolicy") == "selected";
			if (tunnel->id.empty()) {
				tunnel->id = hex_id(8);
			}

			if (tunnel->slug.empty()) {
				tunnel->slug = "fld" + hex_id(4);
			}

			read_lists(item, tunnel.get());
			if (json_t* apps = json_get(item, "appRules")) {
				for (json_t* one = apps->child; one; one = one->next) {
					auto rule = std::make_unique<AppRule>();
					rule->id = json_str(one, "id");
					rule->path = json_str(one, "path");
					rule->name = json_str(one, "name");
					rule->enabled = json_bool(one, "enabled", true);
					if (rule->id.empty()) {
						rule->id = hex_id(8);
					}

					tunnel->apps.push_back(std::move(rule));
				}
			}

			RefreshLabels(tunnel.get());
			tunnels.push_back(std::move(tunnel));
		}
	}

	arena_free(&arena);
}

void Session::Save() {
	std::string out = "{\n  \"launchAtLogin\": ";
	out += launch_at_login ? "true" : "false";
	out += ",\n  \"startHidden\": ";
	out += start_hidden ? "true" : "false";
	out += ",\n  \"language\": " + json_quote(language);
	out += ",\n  \"defaultTunnel\": " + json_quote(default_id);
	out += ",\n  \"lastActiveTunnel\": " + json_quote(last_active_id);
	out += ",\n  \"tunnels\": [\n";
	for (size_t i = 0; i < tunnels.size(); i++) {
		auto const& tunnel = tunnels[i];
		out += "    {\"id\":" + json_quote(tunnel->id);
		out += ",\"name\":" + json_quote(tunnel->name);
		out += ",\"slug\":" + json_quote(tunnel->slug);
		out += ",\"configText\":" + json_quote(tunnel->config);
		out += tunnel->exclude_lan ? ",\"excludeLAN\":true" : ",\"excludeLAN\":false";
		out += tunnel->selected_policy ? ",\"routePolicy\":\"selected\"" : ",\"routePolicy\":\"exceptions\"";
		out += ",\"routeLists\":[";
		for (size_t k = 0; k < tunnel->lists.size(); k++) {
			if (k) {
				out += ',';
			}

			auto const& list = tunnel->lists[k];
			out += "{\"id\":" + json_quote(list->id) + ",\"name\":" + json_quote(list->name);
			out += list->enabled ? ",\"enabled\":true" : ",\"enabled\":false";
			out += ",\"entries\":[";
			for (size_t n = 0; n < list->entries.size(); n++) {
				if (n) {
					out += ',';
				}

				auto const& entry = list->entries[n];
				out += "{\"id\":" + json_quote(entry.id) + ",\"kind\":" + json_quote(entry.kind);
				out += ",\"value\":" + json_quote(entry.value) + "}";
			}

			out += "]}";
		}

		out += "],\"appRules\":[";
		for (size_t k = 0; k < tunnel->apps.size(); k++) {
			if (k) {
				out += ',';
			}

			auto const& rule = tunnel->apps[k];
			out += "{\"id\":" + json_quote(rule->id) + ",\"path\":" + json_quote(rule->path) + ",\"name\":" + json_quote(rule->name);
			out += rule->enabled ? ",\"enabled\":true}" : ",\"enabled\":false}";
		}

		out += "]}";
		out += i + 1 < tunnels.size() ? ",\n" : "\n";
	}

	out += "  ]\n}\n";
	write_file(widen(dir) + L"\\tunnels.json", out);
}
