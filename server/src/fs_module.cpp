#include "fs_module.hpp"
#include <filesystem>
#include <string>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "globals.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

bool fs_validatePath(fs::path startLocation, fs::path other) {
    auto it = startLocation.begin(), it2 = other.begin();
    for (; it != startLocation.end(); ++it, ++it2) {
        if (it2 == other.end() || *it != *it2) {
            return false;
        }
    }
    return true;
}

fs::path fs_resolvePath(fs::path startPath, fs::path uwd, fs::path other) {
    fs::path result = startPath;

    // if other is absolute, remove leading slash and don't prepend uwd
    if (other.is_absolute()) {
        other = other.lexically_relative(other.root_path());
    } else {
        result /= uwd;
    }
    result /= other;

    result = result.lexically_normal();

    // if (!fs_validatePath(startPath, result)) {
    //     result.clear();
    // }

    return result;
}

json fs_listFiles(fs::path path, bool includeHash) {
    json result = json::array();
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(path, ec)) {
        try {
            auto status = entry.status();
            json file;
            file["name"] = entry.path().filename();
            file["type"] = status.type();
            file["size"] = (status.type() == fs::file_type::regular ? entry.file_size() : 0);
            result.push_back(file);
        } catch (const fs::filesystem_error &e) {
            spdlog::error("listFiles(): {}", e.what());
        }
    }
    if (ec) {
        spdlog::error("listFiles(): {}", ec.message());
    }
    return result;
}