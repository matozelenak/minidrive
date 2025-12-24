#include "auth.hpp"
#include <string>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sodium.h>
#include "globals.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

bool AuthModule::loadConfig() {
    spdlog::info("loading user database...");
    if (!fs::exists(USERS_FILE_PATH)) {
        spdlog::info("users file does not exists, creating it...");
        std::ofstream out(USERS_FILE_PATH);
        out << R"({"users":[]})" << '\n';
        out.close();
        if (out.fail()) {
            spdlog::error("users file could not be created");
            return false;
        }
    }
    try {
        std::ifstream in(USERS_FILE_PATH);
        _config = json::parse(in);
    } catch (const json::parse_error &e) {
        spdlog::error("json parse failed: {}", e.what());
        return false;
    } catch (const std::exception &e) {
        spdlog::error("authModule: {}", e.what());
        return false;
    }
    if (!_config.contains("users") || !_config["users"].is_array()) {
        spdlog::error("invalid users file structure");
        return false;
    }
    spdlog::info("user database loaded");
    return true;
}

bool AuthModule::saveConfig() {
    spdlog::info("saving user database...");
    try {
        std::ofstream out(USERS_FILE_PATH);
        out << _config.dump();
    } catch (const std::exception &e) {
        spdlog::error("authModule: {}", e.what());
        return false;
    }
    spdlog::info("user database saved");
    return true;
}

bool AuthModule::userExists(const std::string &username) const {
    for (const auto &entry : _config["users"]) {
        if (!entry.contains("username")) continue;
        json j = entry["username"];
        if (!j.is_string()) continue;
        if (j.get<std::string>() == username) return true;
    }
    return false;
}

bool AuthModule::verifyPassword(const std::string &username, const std::string &password) const {
    for (const auto &entry : _config["users"]) {
        if (!entry.contains("username") || !entry.contains("pw_hash")) continue;
        json userName = entry["username"];
        json pw_hash = entry["pw_hash"];
        if (!userName.is_string() || !pw_hash.is_string()) continue;
        if (userName.get<std::string>() != username) continue;

        std::string pwHashStr = pw_hash.get<std::string>();
        // the crypto function reads the pw hash from a char array of fixed maximum size,
        // so we make sure there are enough bytes in the string
        if (pwHashStr.size() < crypto_pwhash_STRBYTES) pwHashStr.reserve(crypto_pwhash_STRBYTES);
        // if (pwHashStr.size() > crypto_pwhash_STRBYTES-1) return false; // longer than max allowed
        return crypto_pwhash_str_verify(
            pwHashStr.c_str(),
            password.c_str(),
            password.size() ) == 0;
    }
    return false;
}

bool AuthModule::createUser(const std::string &username, const std::string &password) {
    char pw_hash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(
            pw_hash,
            password.c_str(),
            password.size(),
            crypto_pwhash_OPSLIMIT_MODERATE,
            crypto_pwhash_MEMLIMIT_MODERATE) != 0) {
        return false;
    }
    json entry = { {"username", username}, {"pw_hash", std::string(pw_hash)} };
    _config["users"].push_back(entry);
    return true;
}
