#include "pch.h"
#include "coreabi.h"

#include "text.h"

namespace coreabi {
namespace {

using InspectFn = char* (__cdecl*)(char const*);
using ApplyFn = char* (__cdecl*)(char const*, char const*);
using BlankFn = char* (__cdecl*)();
using KeypairFn = int (__cdecl*)(char*, int, char*, int);
using ImportFn = int (__cdecl*)(char const*, char*, int, char*, int);
using ClassifyFn = int (__cdecl*)(char const*, char*, int, char*, int, char*, int, char*, int);
using FreeFn = void (__cdecl*)(void*);

HMODULE module = nullptr;
InspectFn inspect_fn = nullptr;
ApplyFn apply_fn = nullptr;
BlankFn blank_fn = nullptr;
KeypairFn keypair_fn = nullptr;
ImportFn import_fn = nullptr;
ClassifyFn classify_fn = nullptr;
FreeFn free_fn = nullptr;
std::wstring loaded;

std::string take(char* p) {
	if (!p) {
		return {};
	}

	std::string text = p;
	if (free_fn) {
		free_fn(p);
	}

	return text;
}

std::string from_buf(char* buf, int cap) {
	if (!buf || cap <= 0) {
		return {};
	}

	buf[cap - 1] = 0;
	return buf;
}

}

bool load() {
	if (module) {
		return ready();
	}

	wchar_t self[MAX_PATH];
	if (!GetModuleFileNameW(nullptr, self, MAX_PATH)) {
		return false;
	}

	std::wstring path = self;
	auto slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos) {
		return false;
	}

	path.resize(slash);
	path += L"\\fairlead-core.dll";
	loaded = path;
	module = LoadLibraryW(path.c_str());
	if (!module) {
		return false;
	}

	inspect_fn = (InspectFn)GetProcAddress(module, "abi_inspect");
	apply_fn = (ApplyFn)GetProcAddress(module, "abi_apply");
	blank_fn = (BlankFn)GetProcAddress(module, "abi_blank");
	keypair_fn = (KeypairFn)GetProcAddress(module, "abi_keypair");
	import_fn = (ImportFn)GetProcAddress(module, "abi_import");
	classify_fn = (ClassifyFn)GetProcAddress(module, "abi_classify");
	free_fn = (FreeFn)GetProcAddress(module, "abi_free");
	return ready();
}

bool ready() {
	return inspect_fn && apply_fn && blank_fn && keypair_fn && import_fn && classify_fn && free_fn;
}

std::wstring dll_path() {
	return loaded;
}

std::string inspect(std::string const& config) {
	if (!inspect_fn) {
		return "{}";
	}

	return take(inspect_fn(config.c_str()));
}

std::string apply(std::string const& config, std::string const& fields) {
	if (!apply_fn) {
		return config;
	}

	return take(apply_fn(config.c_str(), fields.c_str()));
}

std::string blank() {
	if (!blank_fn) {
		return {};
	}

	return take(blank_fn());
}

void keypair(std::string& priv, std::string& pub) {
	priv.clear();
	pub.clear();
	if (!keypair_fn) {
		return;
	}

	char priv_buf[160]{};
	char pub_buf[160]{};
	keypair_fn(priv_buf, (int)sizeof(priv_buf), pub_buf, (int)sizeof(pub_buf));
	priv = from_buf(priv_buf, (int)sizeof(priv_buf));
	pub = from_buf(pub_buf, (int)sizeof(pub_buf));
}

int import_text(std::string const& text, std::string& config, std::string& name) {
	config.clear();
	name.clear();
	if (!import_fn) {
		return 2;
	}

	std::string cfg(49152, '\0');
	std::string nm(256, '\0');
	int code = import_fn(text.c_str(), cfg.data(), (int)cfg.size(), nm.data(), (int)nm.size());
	config = from_buf(cfg.data(), (int)cfg.size());
	name = from_buf(nm.data(), (int)nm.size());
	return code;
}

void classify(std::string const& line, std::string& kind, std::string& value, std::string& note, std::string& error) {
	kind.clear();
	value.clear();
	note.clear();
	error.clear();
	if (!classify_fn) {
		kind = "error";
		error = "fairlead-core.dll is not loaded.";
		return;
	}

	char kind_buf[32]{};
	char value_buf[256]{};
	char note_buf[256]{};
	char error_buf[256]{};
	classify_fn(line.c_str(), kind_buf, 32, value_buf, 256, note_buf, 256, error_buf, 256);
	kind = from_buf(kind_buf, 32);
	value = from_buf(value_buf, 256);
	note = from_buf(note_buf, 256);
	error = from_buf(error_buf, 256);
}

}
