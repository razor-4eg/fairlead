#include "pch.h"
#include "text.h"

#include <filesystem>
#include <fstream>

std::wstring widen(std::string_view text) {
	if (text.empty()) {
		return {};
	}

	int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
	std::wstring out(n, 0);
	MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), n);
	return out;
}

std::string narrow(std::wstring_view text) {
	if (text.empty()) {
		return {};
	}

	int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
	std::string out(n, 0);
	WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), n, nullptr, nullptr);
	return out;
}

std::string json_quote(std::string_view text) {
	std::string out = "\"";

	for (unsigned char c : text) {
		if (c == '"' || c == '\\') {
			out.push_back('\\');
			out.push_back((char)c);
		} else if (c == '\n') {
			out += "\\n";
		} else if (c == '\r') {
			out += "\\r";
		} else if (c == '\t') {
			out += "\\t";
		} else if (c < 32) {
			char tmp[8];
			snprintf(tmp, sizeof(tmp), "\\u%04x", c);
			out += tmp;
		} else {
			out.push_back((char)c);
		}
	}

	out.push_back('"');
	return out;
}

std::string read_file(std::wstring const& path) {
	std::ifstream in(std::filesystem::path(path), std::ios::binary);
	if (!in) {
		return {};
	}

	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_file(std::wstring const& path, std::string const& text) {
	std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
	if (!out) {
		return false;
	}

	out.write(text.data(), (std::streamsize)text.size());
	return (bool)out;
}

static bool is_space(unsigned char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trim_copy(std::string text) {
	size_t a = 0;
	while (a < text.size() && is_space((unsigned char)text[a])) {
		a++;
	}

	size_t b = text.size();
	while (b > a && is_space((unsigned char)text[b - 1])) {
		b--;
	}

	return text.substr(a, b - a);
}

std::wstring trim_w(std::wstring text) {
	size_t a = 0;
	while (a < text.size() && iswspace(text[a])) {
		a++;
	}

	size_t b = text.size();
	while (b > a && iswspace(text[b - 1])) {
		b--;
	}

	return text.substr(a, b - a);
}

bool iequals(std::wstring_view a, std::wstring_view b) {
	if (a.size() != b.size()) {
		return false;
	}

	for (size_t i = 0; i < a.size(); i++) {
		if (towlower(a[i]) != towlower(b[i])) {
			return false;
		}
	}

	return true;
}

bool icontains(std::wstring_view hay, std::wstring_view needle) {
	if (needle.empty()) {
		return true;
	}

	auto lower = [](wchar_t c) { return (wchar_t)towlower(c); };
	return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [&](wchar_t a, wchar_t b) {
		return lower(a) == lower(b);
	}) != hay.end();
}
