#pragma once

#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

/* Reads the same Localizable.strings catalogs as macOS: l10n\{code}\Localizable.strings next to Fairlead.exe. */
namespace l10n {

/* Directory that holds one folder per language code. Reloads the active table. */
void set_root(std::wstring dir);

/* "auto" or a code from codes(). Unknown values become "auto". Reloads the active table. */
void set_preference(std::string pref);
std::string const& preference();

/* Language the UI is using right now: the preference, or the system language when it is "auto". */
std::string const& active_language();
std::string system_language();

std::vector<std::string> const& codes();
std::string language_title(std::string const& code);

/* Same as active_language(), by value. */
std::string resolved();

/* Active table, then English, then the key itself. */
std::string text(std::string_view key);
std::wstring wtext(std::string_view key);

/* "3 addresses" / "3 domains" with the right plural form. */
std::string addresses(int count);
std::string domains(int count);

/* Helper and core messages are English. Localize the ones that are catalog keys, keep the rest as is. */
std::string display(std::string_view message);

/* Plural form of "%d addresses" style keys: base + ".one" / ".few" / ".many" / ".other". */
std::string plural(std::string_view base, int count);

/* Template of key with "%@" turned into "%s". */
std::string pattern(std::string_view key);

namespace detail {

inline char const* pass(std::string const& value) { return value.c_str(); }
inline char const* pass(char const* value) { return value; }

template <class T>
requires std::is_arithmetic_v<T>
T pass(T value) { return value; }

}

template <class... Args>
std::string format(std::string_view key, Args const&... args) {
	std::string tmpl = pattern(key);
	int n = std::snprintf(nullptr, 0, tmpl.c_str(), detail::pass(args)...);
	if (n <= 0) {
		return tmpl;
	}

	std::string out((size_t)n + 1, '\0');
	std::snprintf(out.data(), out.size(), tmpl.c_str(), detail::pass(args)...);
	out.resize((size_t)n);
	return out;
}

}
