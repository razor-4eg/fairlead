#pragma once

#include <string>
#include <vector>

struct InstalledApp {
	std::string name;
	std::string path;
};

std::vector<InstalledApp> scan_installed();
