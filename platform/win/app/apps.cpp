#include "pch.h"
#include "apps.h"

#include "text.h"
#include "base.h"

#include <algorithm>
#include <filesystem>

namespace {

bool skip_name(std::wstring const& name) {
	auto lower = trim_w(name);
	for (auto& c : lower) {
		c = (wchar_t)towlower(c);
	}

	if (lower.empty()) {
		return true;
	}

	return lower.find(L"uninstall") != std::wstring::npos
		|| lower.find(L"удален") != std::wstring::npos
		|| lower.rfind(L"help ", 0) == 0
		|| (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, L" help") == 0)
		|| (lower.size() >= 7 && lower.compare(lower.size() - 7, 7, L" readme") == 0)
		|| lower.find(L"documentation") != std::wstring::npos
		|| lower == L"desktop" || lower == L"documents" || lower == L"downloads"
		|| lower == L"music" || lower == L"pictures" || lower == L"videos";
}

bool skip_steam_name(std::wstring const& name) {
	auto lower = name;
	for (auto& c : lower) {
		c = (wchar_t)towlower(c);
	}

	return lower.find(L"redistributable") != std::wstring::npos
		|| lower.find(L"steamworks") != std::wstring::npos
		|| lower.find(L"proton") != std::wstring::npos
		|| lower.find(L"steam linux runtime") != std::wstring::npos
		|| lower.find(L"dedicated server") != std::wstring::npos
		|| lower.find(L"server dedicated") != std::wstring::npos
		|| lower.find(L"sdk") != std::wstring::npos
		|| lower.find(L"benchmark") != std::wstring::npos
		|| (lower.size() >= 11 && lower.compare(lower.size() - 11, 11, L" soundtrack") == 0)
		|| lower.find(L"ost ") != std::wstring::npos
		|| lower.rfind(L"soundtrack", 0) == 0;
}

bool skip_exe_name(std::wstring file) {
	for (auto& c : file) {
		c = (wchar_t)towlower(c);
	}

	wchar_t const* exact[] = {
		L"unitycrashhandler64.exe", L"unitycrashhandler32.exe", L"unitycrashhandler.exe",
		L"crashpad_handler.exe", L"crashreporter.exe", L"dxsetup.exe", L"dotnetfx.exe",
		L"uninstall.exe", L"unins000.exe", L"setup.exe", L"installer.exe",
		L"notification_helper.exe", L"steamerrorreporter.exe", L"steam.exe",
		L"epicgameslauncher.exe", L"easervices.exe", L"eadesktop.exe", L"origin.exe",
		L"upc.exe", L"ubisoftconnect.exe", L"galaxyclient.exe", L"battle.net.exe",
		L"riotclientservices.exe", L"wegame.exe", L"awesomium_process.exe",
		L"cefsharp.browsersubprocess.exe"
	};
	for (auto* one : exact) {
		if (file == one) {
			return true;
		}
	}

	return file.rfind(L"unitycrash", 0) == 0
		|| file.rfind(L"crashpad", 0) == 0
		|| file.find(L"unins") != std::wstring::npos
		|| file.find(L"redist") != std::wstring::npos
		|| file.find(L"vcredist") != std::wstring::npos;
}

bool skip_path(std::wstring path) {
	for (auto& c : path) {
		c = (wchar_t)towlower(c);
	}

	auto slash = path.find_last_of(L"\\/");
	std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
	if (skip_exe_name(file)) {
		return true;
	}

	return path.find(L"\\windows\\installer\\") != std::wstring::npos
		|| path.find(L"\\update\\") != std::wstring::npos
		|| path.find(L"\\redistributable") != std::wstring::npos
		|| path.find(L"\\redist\\") != std::wstring::npos;
}

struct RegKey {
	HKEY handle = nullptr;
	RegKey() = default;
	explicit RegKey(HKEY key) : handle(key) {}
	RegKey(RegKey&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
	RegKey& operator=(RegKey&& other) noexcept {
		if (handle) {
			RegCloseKey(handle);
		}

		handle = other.handle;
		other.handle = nullptr;
		return *this;
	}
	~RegKey() {
		if (handle) {
			RegCloseKey(handle);
		}
	}
	explicit operator bool() const { return handle != nullptr; }
	HKEY get() const { return handle; }
};

RegKey open_key(HKEY root, wchar_t const* path) {
	HKEY key = nullptr;
	if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
		return {};
	}

	return RegKey(key);
}

std::wstring reg_sz(HKEY key, wchar_t const* name) {
	DWORD type = 0;
	DWORD bytes = 0;
	if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
		return {};
	}

	std::wstring value(bytes / sizeof(wchar_t), 0);
	if (RegQueryValueExW(key, name, nullptr, &type, (BYTE*)value.data(), &bytes) != ERROR_SUCCESS) {
		return {};
	}

	while (!value.empty() && value.back() == 0) {
		value.pop_back();
	}

	if (type == REG_EXPAND_SZ) {
		wchar_t expanded[4096];
		if (ExpandEnvironmentStringsW(value.c_str(), expanded, 4096)) {
			return expanded;
		}
	}

	return value;
}

std::vector<std::wstring> subkeys(HKEY key) {
	std::vector<std::wstring> names;
	wchar_t name[256];
	for (DWORD i = 0;; i++) {
		DWORD n = 256;
		if (RegEnumKeyExW(key, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
			break;
		}

		names.emplace_back(name);
	}

	return names;
}

std::wstring unescape_vdf(std::wstring value) {
	std::wstring out;
	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] == L'\\' && i + 1 < value.size() && value[i + 1] == L'\\') {
			out.push_back(L'\\');
			i++;
		} else {
			out.push_back(value[i]);
		}
	}

	return out;
}

std::wstring vdf_value(std::wstring const& text, std::wstring const& key) {
	size_t at = 0;
	while (at < text.size()) {
		size_t end = text.find(L'\n', at);
		std::wstring line = text.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
		at = end == std::wstring::npos ? text.size() : end + 1;
		size_t q1 = line.find(L'"');
		if (q1 == std::wstring::npos) {
			continue;
		}

		size_t q2 = line.find(L'"', q1 + 1);
		if (q2 == std::wstring::npos) {
			continue;
		}

		std::wstring found = line.substr(q1 + 1, q2 - q1 - 1);
		if (!iequals(found, key)) {
			continue;
		}

		size_t q3 = line.find(L'"', q2 + 1);
		size_t q4 = q3 == std::wstring::npos ? std::wstring::npos : line.find(L'"', q3 + 1);
		if (q3 == std::wstring::npos || q4 == std::wstring::npos) {
			continue;
		}

		return unescape_vdf(line.substr(q3 + 1, q4 - q3 - 1));
	}

	return {};
}

std::wstring normalize_name(std::wstring value) {
	std::wstring out;
	for (wchar_t c : value) {
		if (iswalnum(c)) {
			out.push_back((wchar_t)towlower(c));
		}
	}

	return out;
}

void put(std::vector<std::pair<std::wstring, std::wstring>>& by_path, std::wstring name, std::wstring path, bool prefer_name) {
	name = trim_w(name);
	path = trim_w(path);
	if (!path.empty() && path.front() == L'"') {
		path.erase(path.begin());
	}

	if (!path.empty() && path.back() == L'"') {
		path.pop_back();
	}

	if (name.empty() || path.size() < 4 || !iequals(path.substr(path.size() - 4), L".exe")) {
		return;
	}

	std::error_code error;
	if (!std::filesystem::exists(std::filesystem::path(path), error) || skip_path(path)) {
		return;
	}

	for (auto& one : by_path) {
		if (!iequals(one.first, path)) {
			continue;
		}

		if (!prefer_name && one.second.size() <= name.size()) {
			return;
		}

		if (prefer_name || name.size() < one.second.size()) {
			one.second = name;
		}

		return;
	}

	by_path.emplace_back(path, name);
}

std::wstring find_exe(std::wstring const& dir, std::wstring const& game, std::wstring const& installdir) {
	std::error_code error;
	if (!std::filesystem::exists(std::filesystem::path(dir), error)) {
		return {};
	}

	struct Candidate {
		std::wstring path;
		std::wstring name;
		std::wstring folder;
		uintmax_t size = 0;
		bool root = false;
	};
	std::vector<Candidate> candidates;
	auto add_dir = [&](std::wstring const& folder, bool deep, bool root) {
		std::error_code local;
		int added = 0;
		auto options = deep ? std::filesystem::directory_options::skip_permission_denied : std::filesystem::directory_options::skip_permission_denied;
		if (!deep) {
			for (auto const& entry : std::filesystem::directory_iterator(std::filesystem::path(folder), options, local)) {
				if (local || !entry.is_regular_file()) {
					continue;
				}

				if (!iequals(entry.path().extension().wstring(), L".exe")) {
					continue;
				}

				candidates.push_back({ entry.path().wstring(), entry.path().filename().wstring(), folder, entry.file_size(local), root });
			}

			return;
		}

		for (auto it = std::filesystem::recursive_directory_iterator(std::filesystem::path(folder), options, local); it != std::filesystem::recursive_directory_iterator(); it.increment(local)) {
			if (local || added >= 40 || !it->is_regular_file()) {
				continue;
			}

			if (!iequals(it->path().extension().wstring(), L".exe")) {
				continue;
			}

			candidates.push_back({ it->path().wstring(), it->path().filename().wstring(), folder, it->file_size(local), false });
			added++;
		}
	};

	add_dir(dir, false, true);
	if (candidates.empty()) {
		for (auto const& entry : std::filesystem::directory_iterator(std::filesystem::path(dir), std::filesystem::directory_options::skip_permission_denied, error)) {
			if (error || !entry.is_directory()) {
				continue;
			}

			std::wstring leaf = entry.path().filename().wstring();
			if (iequals(leaf, L"Engine") || iequals(leaf, L"Redist") || iequals(leaf, L"__Installer") || (leaf.size() > 5 && iequals(leaf.substr(leaf.size() - 5), L"_Data"))) {
				continue;
			}

			bool deep = iequals(leaf, L"Binaries") || iequals(leaf, L"bin") || iequals(leaf, L"Bin");
			add_dir(entry.path().wstring(), deep, false);
		}
	}

	for (auto* relative : { L"\\Binaries\\Win64", L"\\bin\\Win64", L"\\Binaries\\Win32" }) {
		add_dir(dir + relative, false, false);
	}

	std::wstring needle = normalize_name(game);
	std::wstring folder = normalize_name(installdir);
	int best = INT_MIN;
	std::wstring chosen;
	for (auto const& file : candidates) {
		if (skip_exe_name(file.name)) {
			continue;
		}

		int score = 0;
		std::wstring stem = normalize_name(std::filesystem::path(file.name).stem().wstring());
		if (!stem.empty() && (needle.find(stem) != std::wstring::npos || stem.find(needle) != std::wstring::npos || folder.find(stem) != std::wstring::npos || stem.find(folder) != std::wstring::npos)) {
			score += 1000;
		}

		if (file.root) {
			score += 200;
		}

		score += (int)std::min<uintmax_t>(file.size / (128 * 1024), 500);
		if (score > best) {
			best = score;
			chosen = file.path;
		}
	}

	return chosen;
}

std::wstring resolve_link(std::wstring const& link) {
	winrt::com_ptr<IShellLinkW> shell;
	if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(shell.put())))) {
		return {};
	}

	winrt::com_ptr<IPersistFile> file;
	if (FAILED(shell->QueryInterface(IID_PPV_ARGS(file.put()))) || FAILED(file->Load(link.c_str(), STGM_READ))) {
		return {};
	}

	shell->Resolve(nullptr, SLR_NO_UI | SLR_NOUPDATE);
	wchar_t target[MAX_PATH];
	if (FAILED(shell->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH))) {
		return {};
	}

	return trim_w(target);
}

void scan_start(std::vector<std::pair<std::wstring, std::wstring>>& by_path) {
	std::vector<std::wstring> roots;
	PWSTR raw = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &raw)) && raw) {
		roots.emplace_back(raw);
		CoTaskMemFree(raw);
	}

	raw = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &raw)) && raw) {
		roots.emplace_back(raw);
		CoTaskMemFree(raw);
	}

	raw = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_StartMenu, 0, nullptr, &raw)) && raw) {
		roots.push_back(std::wstring(raw) + L"\\Programs");
		CoTaskMemFree(raw);
	}

	for (auto const& root : roots) {
		std::error_code error;
		if (!std::filesystem::exists(std::filesystem::path(root), error)) {
			continue;
		}

		for (auto it = std::filesystem::recursive_directory_iterator(std::filesystem::path(root), std::filesystem::directory_options::skip_permission_denied, error); it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
			if (error || !it->is_regular_file() || !iequals(it->path().extension().wstring(), L".lnk")) {
				continue;
			}

			std::wstring name = it->path().stem().wstring();
			if (skip_name(name)) {
				continue;
			}

			put(by_path, name, resolve_link(it->path().wstring()), false);
		}
	}
}

std::wstring steam_root() {
	if (auto key = open_key(HKEY_CURRENT_USER, L"Software\\Valve\\Steam")) {
		std::wstring path = reg_sz(key.get(), L"SteamPath");
		for (auto& c : path) {
			if (c == L'/') {
				c = L'\\';
			}
		}

		if (!path.empty() && std::filesystem::exists(std::filesystem::path(path))) {
			return path;
		}
	}

	wchar_t program[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, SHGFP_TYPE_CURRENT, program))) {
		std::wstring path = std::wstring(program) + L"\\Steam";
		if (std::filesystem::exists(std::filesystem::path(path))) {
			return path;
		}
	}

	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, program))) {
		std::wstring path = std::wstring(program) + L"\\Steam";
		if (std::filesystem::exists(std::filesystem::path(path))) {
			return path;
		}
	}

	return {};
}

void scan_steam(std::vector<std::pair<std::wstring, std::wstring>>& by_path) {
	std::wstring steam = steam_root();
	if (steam.empty()) {
		return;
	}

	std::vector<std::wstring> libraries{ steam };
	std::wstring vdf = steam + L"\\steamapps\\libraryfolders.vdf";
	std::wstring text = widen(read_file(vdf));
	size_t at = 0;
	while (at < text.size()) {
		auto path = vdf_value(text.substr(at), L"path");
		if (!path.empty()) {
			bool seen = false;
			for (auto const& one : libraries) {
				if (iequals(one, path)) {
					seen = true;
				}
			}

			if (!seen && std::filesystem::exists(std::filesystem::path(path))) {
				libraries.push_back(path);
			}
		}

		auto next = text.find(L"\"path\"", at + 4);
		if (next == std::wstring::npos) {
			break;
		}

		at = next + 1;
	}

	for (auto const& library : libraries) {
		std::wstring apps = library + L"\\steamapps";
		std::error_code error;
		if (!std::filesystem::exists(std::filesystem::path(apps), error)) {
			continue;
		}

		for (auto const& entry : std::filesystem::directory_iterator(std::filesystem::path(apps), error)) {
			if (error || !entry.is_regular_file()) {
				continue;
			}

			std::wstring name = entry.path().filename().wstring();
			if (name.rfind(L"appmanifest_", 0) != 0 || !iequals(entry.path().extension().wstring(), L".acf")) {
				continue;
			}

			std::wstring manifest = widen(read_file(entry.path().wstring()));
			std::wstring game = vdf_value(manifest, L"name");
			std::wstring install = vdf_value(manifest, L"installdir");
			std::wstring flags = vdf_value(manifest, L"StateFlags");
			if (game.empty() || install.empty() || skip_steam_name(game)) {
				continue;
			}

			if (!flags.empty()) {
				unsigned state = (unsigned)wcstoul(flags.c_str(), nullptr, 10);
				if ((state & 4) == 0) {
					continue;
				}
			}

			std::wstring dir = apps + L"\\common\\" + install;
			put(by_path, game, find_exe(dir, game, install), true);
		}
	}
}

void scan_epic(std::vector<std::pair<std::wstring, std::wstring>>& by_path) {
	wchar_t common[MAX_PATH];
	if (FAILED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, SHGFP_TYPE_CURRENT, common))) {
		return;
	}

	std::wstring root = std::wstring(common) + L"\\Epic\\EpicGamesLauncher\\Data\\Manifests";
	std::error_code error;
	if (!std::filesystem::exists(std::filesystem::path(root), error)) {
		return;
	}

	for (auto const& entry : std::filesystem::directory_iterator(std::filesystem::path(root), error)) {
		if (error || !iequals(entry.path().extension().wstring(), L".item")) {
			continue;
		}

		std::string text = read_file(entry.path().wstring());
		arena_t arena;
		arena_init(&arena, (int)std::max<size_t>(text.size() * 8, 8192));
		json_t* json = json_parse(&arena, text.c_str());
		if (json) {
			char const* name = json_string(json_get(json, "DisplayName"));
			char const* location = json_string(json_get(json, "InstallLocation"));
			char const* launch = json_string(json_get(json, "LaunchExecutable"));
			if (name && location && launch && *name && *location && *launch) {
				std::wstring exe_name = widen(launch);
				std::wstring game = widen(name);
				if (!skip_name(game) && (exe_name.size() < 4 || !iequals(exe_name.substr(exe_name.size() - 4), L".bat"))) {
					std::filesystem::path full = std::filesystem::path(widen(location)) / exe_name;
					put(by_path, game, full.wstring(), true);
				}
			}
		}

		arena_free(&arena);
	}
}

void scan_gog(std::vector<std::pair<std::wstring, std::wstring>>& by_path) {
	wchar_t const* hives[] = { L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", L"SOFTWARE\\GOG.com\\Games" };
	for (auto* hive : hives) {
		auto root = open_key(HKEY_LOCAL_MACHINE, hive);
		if (!root) {
			continue;
		}

		for (auto const& id : subkeys(root.get())) {
			auto game = open_key(root.get(), id.c_str());
			if (!game) {
				continue;
			}

			std::wstring name = trim_w(reg_sz(game.get(), L"gameName"));
			if (name.empty()) {
				name = trim_w(reg_sz(game.get(), L"DISPLAYNAME"));
			}

			std::wstring path = trim_w(reg_sz(game.get(), L"path"));
			if (path.empty()) {
				path = trim_w(reg_sz(game.get(), L"PATH"));
			}

			std::wstring exe = trim_w(reg_sz(game.get(), L"exe"));
			if (exe.empty()) {
				exe = trim_w(reg_sz(game.get(), L"EXE"));
			}

			if (name.empty() || path.empty()) {
				continue;
			}

			std::wstring full = exe.empty() ? find_exe(path, name, std::filesystem::path(path).filename().wstring()) : (std::filesystem::path(path) / std::filesystem::path(exe).filename()).wstring();
			put(by_path, name, full, true);
		}
	}
}

void scan_ubisoft(std::vector<std::pair<std::wstring, std::wstring>>& by_path) {
	wchar_t const* hives[] = { L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher\\Installs", L"SOFTWARE\\Ubisoft\\Launcher\\Installs" };
	for (auto* hive : hives) {
		auto root = open_key(HKEY_LOCAL_MACHINE, hive);
		if (!root) {
			continue;
		}

		for (auto const& id : subkeys(root.get())) {
			auto game = open_key(root.get(), id.c_str());
			if (!game) {
				continue;
			}

			std::wstring dir = trim_w(reg_sz(game.get(), L"InstallDir"));
			if (dir.empty() || !std::filesystem::exists(std::filesystem::path(dir))) {
				continue;
			}

			while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) {
				dir.pop_back();
			}

			std::wstring name = std::filesystem::path(dir).filename().wstring();
			if (skip_name(name)) {
				continue;
			}

			put(by_path, name, find_exe(dir, name, name), true);
		}
	}
}

std::wstring attr_of(std::wstring const& xml, std::wstring const& tag, std::wstring const& attr) {
	auto lower = xml;
	for (auto& c : lower) {
		c = (wchar_t)towlower(c);
	}

	std::wstring needle = L"<" + tag;
	for (auto& c : needle) {
		c = (wchar_t)towlower(c);
	}

	size_t at = 0;
	while ((at = lower.find(needle, at)) != std::wstring::npos) {
		size_t end = lower.find(L'>', at);
		if (end == std::wstring::npos) {
			return {};
		}

		std::wstring element = xml.substr(at, end - at);
		std::wstring key = attr + L"=\"";
		auto folded = element;
		for (auto& c : folded) {
			c = (wchar_t)towlower(c);
		}

		std::wstring folded_key = key;
		for (auto& c : folded_key) {
			c = (wchar_t)towlower(c);
		}

		size_t pos = folded.find(folded_key);
		if (pos != std::wstring::npos) {
			size_t start = pos + key.size();
			size_t stop = element.find(L'"', start);
			if (stop != std::wstring::npos) {
				return element.substr(start, stop - start);
			}
		}

		at = end;
	}

	return {};
}

void scan_xbox(std::vector<std::pair<std::wstring, std::wstring>>& by_path) {
	std::vector<std::wstring> roots;
	wchar_t system[MAX_PATH];
	if (GetSystemDirectoryW(system, MAX_PATH)) {
		std::wstring drive(system, 3);
		roots.push_back(drive + L"XboxGames");
	}

	for (auto* extra : { L"D:\\XboxGames", L"E:\\XboxGames", L"F:\\XboxGames" }) {
		if (std::filesystem::exists(std::filesystem::path(extra))) {
			bool seen = false;
			for (auto const& one : roots) {
				if (iequals(one, extra)) {
					seen = true;
				}
			}

			if (!seen) {
				roots.emplace_back(extra);
			}
		}
	}

	for (auto const& root : roots) {
		std::error_code error;
		if (!std::filesystem::exists(std::filesystem::path(root), error)) {
			continue;
		}

		for (auto const& title : std::filesystem::directory_iterator(std::filesystem::path(root), error)) {
			if (error || !title.is_directory()) {
				continue;
			}

			std::wstring content = title.path().wstring() + L"\\Content";
			std::wstring dir = std::filesystem::exists(std::filesystem::path(content)) ? content : title.path().wstring();
			std::wstring name = title.path().filename().wstring();
			std::wstring config = dir + L"\\MicrosoftGame.config";
			std::wstring exe;
			if (std::filesystem::exists(std::filesystem::path(config))) {
				std::wstring xml = widen(read_file(config));
				std::wstring display = attr_of(xml, L"ShellVisuals", L"DefaultDisplayName");
				if (display.empty()) {
					display = attr_of(xml, L"Identity", L"Name");
				}

				if (!trim_w(display).empty()) {
					name = trim_w(display);
				}

				std::wstring file = attr_of(xml, L"Executable", L"Name");
				if (file.size() > 4 && iequals(file.substr(file.size() - 4), L".exe")) {
					for (auto& c : file) {
						if (c == L'/') {
							c = L'\\';
						}
					}

					exe = dir + L"\\" + file;
				}
			}

			if (exe.empty()) {
				exe = find_exe(dir, name, name);
			}

			put(by_path, name, exe, true);
		}
	}
}

bool game_publisher(std::wstring publisher) {
	for (auto& c : publisher) {
		c = (wchar_t)towlower(c);
	}

	if (publisher.empty()) {
		return false;
	}

	wchar_t const* needles[] = {
		L"electronic arts", L"ea swiss", L"blizzard", L"battle.net", L"activision",
		L"rockstar", L"riot games", L"ubisoft", L"valve", L"epic games", L"gog.com",
		L"xbox game studios", L"bethesda", L"cd projekt"
	};
	for (auto* needle : needles) {
		if (publisher.find(needle) != std::wstring::npos) {
			return true;
		}
	}

	return publisher.rfind(L"2k", 0) == 0 || publisher.find(L"2k ") != std::wstring::npos;
}

void scan_uninstall(std::vector<std::pair<std::wstring, std::wstring>>& by_path) {
	struct Root { HKEY hive; wchar_t const* path; };
	Root roots[] = {
		{ HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
		{ HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
		{ HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall" }
	};
	for (auto const& root : roots) {
		auto key = open_key(root.hive, root.path);
		if (!key) {
			continue;
		}

		for (auto const& id : subkeys(key.get())) {
			auto game = open_key(key.get(), id.c_str());
			if (!game || !game_publisher(reg_sz(game.get(), L"Publisher"))) {
				continue;
			}

			std::wstring name = trim_w(reg_sz(game.get(), L"DisplayName"));
			if (skip_name(name) || skip_steam_name(name)) {
				continue;
			}

			std::wstring icon = trim_w(reg_sz(game.get(), L"DisplayIcon"));
			if (!icon.empty() && icon.front() == L'"') {
				icon.erase(icon.begin());
			}

			if (!icon.empty() && icon.back() == L'"') {
				icon.pop_back();
			}

			auto comma = icon.find(L',');
			if (comma != std::wstring::npos) {
				icon = trim_w(icon.substr(0, comma));
			}

			if (icon.size() > 4 && iequals(icon.substr(icon.size() - 4), L".exe")) {
				put(by_path, name, icon, true);
				continue;
			}

			std::wstring loc = trim_w(reg_sz(game.get(), L"InstallLocation"));
			if (!loc.empty()) {
				while (!loc.empty() && (loc.back() == L'\\' || loc.back() == L'"')) {
					loc.pop_back();
				}

				put(by_path, name, find_exe(loc, name, std::filesystem::path(loc).filename().wstring()), true);
			}
		}
	}
}

}

std::vector<InstalledApp> scan_installed() {
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	bool own = hr == S_OK || hr == S_FALSE;
	std::vector<std::pair<std::wstring, std::wstring>> by_path;
	scan_start(by_path);
	scan_steam(by_path);
	scan_epic(by_path);
	scan_gog(by_path);
	scan_ubisoft(by_path);
	scan_xbox(by_path);
	scan_uninstall(by_path);
	std::vector<InstalledApp> apps;
	apps.reserve(by_path.size());
	for (auto const& one : by_path) {
		apps.push_back({ narrow(one.second), narrow(one.first) });
	}

	std::sort(apps.begin(), apps.end(), [](InstalledApp const& a, InstalledApp const& b) {
		return CompareStringW(LOCALE_USER_DEFAULT, LINGUISTIC_IGNORECASE, widen(a.name).c_str(), -1, widen(b.name).c_str(), -1) == CSTR_LESS_THAN;
	});
	if (own) {
		CoUninitialize();
	}

	return apps;
}
