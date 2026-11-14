#pragma once

#include <string>

namespace coreabi {

bool load();
bool ready();
std::wstring dll_path();
std::string inspect(std::string const& config);
std::string apply(std::string const& config, std::string const& fields);
std::string blank();
void keypair(std::string& priv, std::string& pub);
int import_text(std::string const& text, std::string& config, std::string& name);
void classify(std::string const& line, std::string& kind, std::string& value, std::string& note, std::string& error);

}
