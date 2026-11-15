#include "pch.h"
#include "l10n.h"

#include "text.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>

namespace l10n {

namespace {

using Table = std::unordered_map<std::string, std::string>;

std::vector<std::string> const language_codes = { "en", "ru", "de", "es", "fr", "pt", "zh-Hans" };

std::wstring root;
std::string pref = "auto";
std::string active = "en";
Table english;
Table current;
bool english_loaded = false;

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

void put_utf8(std::string& out, unsigned code) {
	if (code < 0x80) {
		out.push_back((char)code);
	} else if (code < 0x800) {
		out.push_back((char)(0xC0 | (code >> 6)));
		out.push_back((char)(0x80 | (code & 0x3F)));
	} else {
		out.push_back((char)(0xE0 | (code >> 12)));
		out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
		out.push_back((char)(0x80 | (code & 0x3F)));
	}
}

/* Apple .strings are UTF-8 here, but tolerate a BOM or UTF-16LE from Xcode exports. */
std::string as_utf8(std::string data) {
	if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
		return data.substr(3);
	}

	if (data.size() >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE) {
		std::wstring wide((wchar_t const*)(data.data() + 2), (data.size() - 2) / 2);
		return narrow(wide);
	}

	return data;
}

/* "key" = "value"; with \" \\ \n \t \r \uXXXX escapes, and block or line comments. */
Table parse(std::string const& raw) {
	Table out;
	std::string data = as_utf8(raw);
	size_t at = 0;
	size_t n = data.size();
	auto skip = [&] {
		while (at < n) {
			if (isspace((unsigned char)data[at])) {
				at++;
			} else if (data.compare(at, 2, "/*") == 0) {
				size_t end = data.find("*/", at + 2);
				at = end == std::string::npos ? n : end + 2;
			} else if (data.compare(at, 2, "//") == 0) {
				size_t end = data.find('\n', at);
				at = end == std::string::npos ? n : end + 1;
			} else {
				break;
			}
		}
	};
	auto quoted = [&](std::string& value) {
		if (at >= n || data[at] != '"') {
			return false;
		}

		at++;
		value.clear();
		while (at < n && data[at] != '"') {
			char c = data[at++];
			if (c != '\\' || at >= n) {
				value.push_back(c);
				continue;
			}

			char e = data[at++];
			switch (e) {
			case 'n': value.push_back('\n'); break;
			case 't': value.push_back('\t'); break;
			case 'r': value.push_back('\r'); break;
			case 'u':
			case 'U': {
				unsigned code = 0;
				int digits = 0;
				while (digits < 4 && at < n && isxdigit((unsigned char)data[at])) {
					char h = data[at++];
					code = code * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
					digits++;
				}

				put_utf8(value, code);
				break;
			}
			default: value.push_back(e); break;
			}
		}

		if (at >= n) {
			return false;
		}

		at++;
		return true;
	};

	while (true) {
		skip();
		std::string key, value;
		if (!quoted(key)) {
			break;
		}

		skip();
		if (at >= n || data[at] != '=') {
			break;
		}

		at++;
		skip();
		if (!quoted(value)) {
			break;
		}

		skip();
		if (at < n && data[at] == ';') {
			at++;
		}

		out[key] = value;
	}

	return out;
}

Table load(std::string const& code) {
	std::vector<std::filesystem::path> where;
	if (!root.empty()) {
		where.push_back(std::filesystem::path(root) / widen(code) / L"Localizable.strings");
	}

	std::filesystem::path exe = module_dir();
	where.push_back(exe / L"l10n" / widen(code) / L"Localizable.strings");
	/* Running from dist\win\bin in a checkout: read the macOS catalogs in place. */
	where.push_back(exe / L".." / L".." / L".." / L"platform" / L"mac" / L"Support" / widen(code + ".lproj") / L"Localizable.strings");
	for (auto const& path : where) {
		std::error_code error;
		if (!std::filesystem::exists(path, error)) {
			continue;
		}

		Table table = parse(read_file(path.wstring()));
		if (!table.empty()) {
			return table;
		}
	}

	return {};
}

std::string canonical(std::string raw) {
	raw = trim_copy(raw);
	std::string lower = raw;
	for (char& c : lower) {
		c = c == '_' ? '-' : (char)tolower((unsigned char)c);
	}

	if (lower.empty() || lower == "auto") {
		return "auto";
	}

	if (lower.rfind("zh", 0) == 0) {
		return "zh-Hans";
	}

	std::string head = lower.substr(0, lower.find('-'));
	for (auto const& code : language_codes) {
		if (code == head) {
			return code;
		}
	}

	return "auto";
}

void reload() {
	english = load("en");
	english_loaded = true;
	active = pref == "auto" ? system_language() : pref;
	current = active == "en" ? Table() : load(active);
}

void ensure() {
	if (!english_loaded) {
		reload();
	}
}

}

void set_root(std::wstring dir) {
	root = std::move(dir);
	reload();
}

void set_preference(std::string value) {
	pref = canonical(std::move(value));
	reload();
}

std::string const& preference() {
	return pref;
}

std::string const& active_language() {
	ensure();
	return active;
}

std::string system_language() {
	wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
	if (!GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) {
		return "en";
	}

	std::string tag = narrow(name);
	for (char& c : tag) {
		c = (char)tolower((unsigned char)c);
	}

	if (tag.rfind("zh", 0) == 0) {
		return "zh-Hans";
	}

	std::string head = tag.substr(0, tag.find('-'));
	for (auto const& code : language_codes) {
		if (code != "zh-Hans" && code == head) {
			return code;
		}
	}

	return "en";
}

std::vector<std::string> const& codes() {
	return language_codes;
}

std::string language_title(std::string const& code) {
	if (code == "auto") {
		return text("Auto");
	}

	if (code == "en") return "English";
	if (code == "ru") return "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9";
	if (code == "de") return "Deutsch";
	if (code == "es") return "Espa\xC3\xB1ol";
	if (code == "fr") return "Fran\xC3\xA7""ais";
	if (code == "pt") return "Portugu\xC3\xAA""s";
	if (code == "zh-Hans") return "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87";
	return code;
}

std::string resolved() {
	return active_language();
}

std::wstring wtext(std::string_view key) {
	return widen(text(key));
}

std::string addresses(int count) {
	return plural("%d addresses", count);
}

std::string domains(int count) {
	return plural("%d domains", count);
}

std::string text(std::string_view key) {
	ensure();
	std::string name(key);
	if (auto it = current.find(name); it != current.end()) {
		return it->second;
	}

	if (auto it = english.find(name); it != english.end()) {
		return it->second;
	}

	return name;
}

std::string display(std::string_view message) {
	ensure();
	std::string whole(message);
	if (whole.empty() || english.find(whole) != english.end()) {
		return text(whole);
	}

	/* "Known line\nextra detail" keeps its detail lines. */
	size_t cut = whole.find('\n');
	if (cut == std::string::npos) {
		return whole;
	}

	std::string head = whole.substr(0, cut);
	if (english.find(head) == english.end()) {
		return whole;
	}

	return text(head) + whole.substr(cut);
}

std::string plural(std::string_view base, int count) {
	ensure();
	std::string form = "other";
	if (active == "ru") {
		int mod100 = count % 100;
		int mod10 = count % 10;
		if (mod100 >= 11 && mod100 <= 14) {
			form = "many";
		} else if (mod10 == 1) {
			form = "one";
		} else if (mod10 >= 2 && mod10 <= 4) {
			form = "few";
		} else {
			form = "many";
		}
	} else if (active == "fr") {
		form = count <= 1 ? "one" : "other";
	} else if (active != "zh-Hans") {
		form = count == 1 ? "one" : "other";
	}

	return l10n::format(std::string(base) + "." + form, count);
}

std::string pattern(std::string_view key) {
	std::string out = text(key);
	for (size_t at = out.find("%@"); at != std::string::npos; at = out.find("%@", at + 2)) {
		out[at + 1] = 's';
	}

	return out;
}

}
