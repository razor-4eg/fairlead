#pragma once

#include <string>
#include <string_view>

std::wstring widen(std::string_view text);
std::string narrow(std::wstring_view text);
std::string json_quote(std::string_view text);
std::string read_file(std::wstring const& path);
bool write_file(std::wstring const& path, std::string const& text);
std::string trim_copy(std::string text);
std::wstring trim_w(std::wstring text);
bool iequals(std::wstring_view a, std::wstring_view b);
bool icontains(std::wstring_view hay, std::wstring_view needle);
