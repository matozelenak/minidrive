#pragma once
#include <string>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

struct Transfer {
    nlohmann::json command;
    std::filesystem::path resolvedPath;
    std::filesystem::path resolvedPathTmp;
    uintmax_t size;
    uintmax_t chunkSize;
    uintmax_t sequenceNum;
    bool active;
    std::fstream stream;
    std::string jsonFilename;
};