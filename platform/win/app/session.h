#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct RouteEntry {
	std::string id;
	std::string kind;
	std::string value;
	std::string status;
	std::vector<std::string> addresses;
};

struct RouteList {
	std::string id;
	std::string name;
	bool enabled = true;
	std::vector<RouteEntry> entries;
};

struct AppRule {
	std::string id;
	std::string path;
	std::string name;
	bool enabled = true;
	bool learn = true;
	std::vector<std::string> learned;
};

struct RouteProblem {
	std::string line;
	std::string message;
};

struct Tunnel {
	std::string id;
	std::string slug;
	std::string config;
	std::string name;
	bool exclude_lan = true;
	bool selected_policy = false;
	std::vector<std::unique_ptr<RouteList>> lists;
	std::vector<std::unique_ptr<AppRule>> apps;
	std::string endpoint = "no endpoint";
	std::string address;
	bool amnezia = false;
	std::string phase = "Off";
	std::string detail = "Traffic is bypassing WireGuard";
	std::string iface;
	std::string down = "0 B/s";
	std::string up = "0 B/s";
	std::string rx = "0 B";
	std::string tx = "0 B";
	std::string lan = "The current Wi-Fi or Ethernet network. It appears after you connect.";
	bool connected = false;
	uint64_t sample_rx = 0;
	uint64_t sample_tx = 0;
	int64_t sample_tick = 0;
	int handshake = 0;
};

struct ProcHit {
	std::string path;
	std::vector<std::string> ips;
};

struct BuiltList {
	std::unique_ptr<RouteList> list;
	std::vector<RouteProblem> problems;
	std::vector<RouteEntry*> pending;
};

struct Session {
	std::vector<std::unique_ptr<Tunnel>> tunnels;
	Tunnel* selected = nullptr;
	std::string query;
	std::string notice;
	std::string banner;
	bool helper_online = false;
	std::string awg_text = "amneziawg-go  not found";
	bool awg_ok = false;
	bool launch_at_login = false;
	bool start_hidden = false;
	std::string default_id;
	std::string last_active_id;
	static constexpr char const* last_key = "last";
	std::string dir;
	std::string exe_dir;
	std::string token;

	Session();
	std::string HelperText() const;
	bool HasTunnel() const { return selected != nullptr; }
	bool DirectOn() const { return !(selected && selected->selected_policy); }
	bool RulesEmpty() const { return selected && selected->lists.empty() && selected->apps.empty(); }
	std::string PolicyTitle() const;
	std::string PolicyBody() const;
	std::vector<Tunnel*> Filtered() const;
	Tunnel* Find(std::string const& id) const;
	Tunnel* ResolveDefault() const;
	void RefreshLabels(Tunnel* tunnel);
	void RefreshTools();
	std::string Call(std::string const& body, int connect_ms = 5000);
	std::string Rpc(char const* op, Tunnel* tunnel, bool with_config);
	std::string StatusBody() const;
	std::string QuitBody() const;
	bool ReplyOk(std::string const& reply) const;
	std::string ErrorOf(std::string const& reply) const;
	void ApplyStatus(std::string const& reply);
	void BeginConnect(Tunnel* tunnel);
	bool FinishConnect(Tunnel* tunnel, std::string const& reply);
	void BeginDisconnect(Tunnel* tunnel);
	void FinishDisconnect(Tunnel* tunnel);
	std::unique_ptr<Tunnel> MakeDraft();
	void Adopt(std::unique_ptr<Tunnel> tunnel);
	bool ImportFile(std::string const& path);
	void Delete(Tunnel* tunnel);
	void SaveConfig(Tunnel* tunnel, std::string config);
	void RememberLast(std::string const& id);
	void SetPolicy(bool through);
	void SetLan(bool on);
	void SaveRules();
	BuiltList BuildList(std::string const& name, std::string const& text, RouteList* replacing);
	void CommitNew(std::unique_ptr<RouteList> list);
	void CommitUpdate(RouteList* list, std::string name, std::vector<RouteEntry> entries);
	bool BeginResolve(std::string const& id);
	void EndResolve(std::string const& id);
	std::vector<ProcHit> SnapshotTcp() const;
	bool ApplyLearn(std::vector<ProcHit> const& found);
	bool AddProgram(std::string const& path, std::string const& name);
	void SetLaunch(bool on);
	void SetStartHidden(bool on);
	void SetDefault(std::string const& id);
	void OpenFolder() const;
	bool Ping();
	bool RunScheduled() const;
	void StopHelpers() const;
	bool LaunchHelper(void* hwnd, std::string& error);
	void MarkHelper(bool online);
	void Save();
	void ClearBanner();

private:
	std::timed_mutex pipe;
	std::vector<std::string> resolving;
	void Load();
	std::string LoadToken();
};

std::vector<std::string> lookup_host(std::string const& host);
