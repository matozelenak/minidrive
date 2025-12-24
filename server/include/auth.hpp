#pragma once
#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

class AuthModule {
public:
    bool loadConfig();
    bool saveConfig();

    bool userExists(const std::string &username) const;
    bool verifyPassword(const std::string &username, const std::string &password) const;
    bool createUser(const std::string &username, const std::string &password);

private:
    nlohmann::json _config;

};