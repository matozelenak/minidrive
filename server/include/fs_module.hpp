#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>

bool fs_validatePath(std::filesystem::path startLocation, std::filesystem::path other);
std::filesystem::path fs_resolvePath(std::filesystem::path startPath, std::filesystem::path uwd, std::filesystem::path other);
nlohmann::json fs_listFiles(std::filesystem::path path, bool includeHash);
