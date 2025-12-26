#include "session.hpp"
#include <asio.hpp>
#include <string>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "minidrive/error_codes.hpp"
#include "server.hpp"
#include "globals.hpp"

using asio::ip::tcp;
using nlohmann::json;
namespace fs = std::filesystem;

void Session::handleLIST(const std::string &cmd, const json &args, const json &data) {
    if (_mode == mode::NOT_AUTHENTICATED) {
        spdlog::warn("session is not authenticated");
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), "not authenticated");
        return;
    }
    if (!args.contains("path")) {
        spdlog::warn("request does not contain 'path'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT, "path");
        return;
    }
    std::string path = args["path"];
    auto[result, valid] = _server->fs_resolvePath(this, path);
    spdlog::debug("requested path: {}", path);
    spdlog::debug("uwd: {}", _uwd.string());
    spdlog::debug("resolved path: {}", result.string());
    spdlog::debug("valid: {}", valid);
    if (!valid) {
        spdlog::warn("access denied: {}", result.string());
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), result.string());
        return;
    }
    spdlog::info("listing path: {}", result.string());
    json replyData = { {"files", _server->fs_listFiles(result)} };
    spdlog::debug("json: {}", replyData.dump());
    sendOkReply(result.string(), replyData);
}

void Session::handleREMOVE(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data) {
    if (_mode == mode::NOT_AUTHENTICATED) {
        spdlog::warn("session is not authenticated");
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), "not authenticated");
        return;
    }
    if (!args.contains("path")) {
        spdlog::warn("request does not contain 'path'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "path");
        return;
    }
    std::string path = args["path"];
    auto[result, valid] = _server->fs_resolvePath(this, path);
    if (!valid) {
        spdlog::warn("access denied: {}", result.string());
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), result.string());
        return;
    }
    if (!_server->fs_exists(result)) {
        spdlog::warn("target does not exist: {}", result.string());
        sendFailReply(minidrive::error::TARGET_NOT_FOUND.code(), result.string());
        return;
    }
    auto type = _server->fs_getFileType(result);
    if (type != fs::file_type::regular) {
        spdlog::warn("target is not a regular file: {}", result.string());
        sendFailReply(minidrive::error::FS_ERROR.code(), std::string("target is not a regular file: ") + result.string());
        return;
    }
    if (!_server->fs_remove(result)) {
        spdlog::warn("could not remove file: {}", result.string());
        sendFailReply(minidrive::error::FS_ERROR.code(), std::string("could not remove file: ") + result.string());
        return;
    }

    sendOkReply("file removed");
}

void Session::handleCD(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data) {
    if (_mode == mode::NOT_AUTHENTICATED) {
        spdlog::warn("session is not authenticated");
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), "not authenticated");
        return;
    }
    if (!args.contains("path")) {
        spdlog::warn("request does not contain 'path'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "path");
        return;
    }
    std::string path = args["path"];
    auto[result, valid] = _server->fs_resolvePath(this, path);
    if (!valid) {
        spdlog::warn("access denied: {}", result.string());
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), result.string());
        return;
    }
    if (!_server->fs_exists(result)) {
        spdlog::warn("target does not exist: {}", result.string());
        sendFailReply(minidrive::error::TARGET_NOT_FOUND.code(), result.string());
        return;
    }
    auto type = _server->fs_getFileType(result);
    if (type != fs::file_type::directory) {
        spdlog::warn("target is not a directory: {}", result.string());
        sendFailReply(minidrive::error::FS_ERROR.code(), std::string("target is not a directory: ") + result.string());
        return;
    }

    try {
        _uwd = fs::relative(result, USERDATA_DIR_PATH / _username);
        spdlog::info("changed UWD: {}", _uwd.string());
        sendOkReply("");
    } catch (const fs::filesystem_error &e) {
        spdlog::error("fs::relative(): {}", e.what());
        sendFailReply(minidrive::error::FS_ERROR.code(), "filesystem error");
    }
}

void Session::handleMKDIR(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data) {
    if (_mode == mode::NOT_AUTHENTICATED) {
        spdlog::warn("session is not authenticated");
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), "not authenticated");
        return;
    }
    if (!args.contains("path")) {
        spdlog::warn("request does not contain 'path'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "path");
        return;
    }
    std::string path = args["path"];
    auto[result, valid] = _server->fs_resolvePath(this, path);
    if (!valid) {
        spdlog::warn("access denied: {}", result.string());
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), result.string());
        return;
    }
    if (_server->fs_exists(result)) {
        spdlog::warn("target already exists: {}", result.string());
        sendFailReply(minidrive::error::TARGET_ALREADY_EXISTS.code(), result.string());
        return;
    }

    if (!_server->fs_createDir(result, false)) {
        spdlog::error("failed to create directory: {}", result.string());
        sendFailReply(minidrive::error::FS_ERROR.code(), std::string("failed to create directory: ") + result.string());
        return;
    }

    sendOkReply("");
}

void Session::handleRMDIR(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data) {
    if (_mode == mode::NOT_AUTHENTICATED) {
        spdlog::warn("session is not authenticated");
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), "not authenticated");
        return;
    }
    if (!args.contains("path")) {
        spdlog::warn("request does not contain 'path'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "path");
        return;
    }
    std::string path = args["path"];
    auto[result, valid] = _server->fs_resolvePath(this, path);
    if (!valid) {
        spdlog::warn("access denied: {}", result.string());
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), result.string());
        return;
    }
    if (!_server->fs_exists(result)) {
        spdlog::warn("target does not exist: {}", result.string());
        sendFailReply(minidrive::error::TARGET_NOT_FOUND.code(), result.string());
        return;
    }
    auto type = _server->fs_getFileType(result);
    if (type != fs::file_type::directory) {
        spdlog::warn("target is not a directory: {}", result.string());
        sendFailReply(minidrive::error::FS_ERROR.code(), std::string("target is not a directory: ") + result.string());
        return;
    }

    if (!_server->fs_removeDir(result)) {
        spdlog::error("failed to remove directory: {}", result.string());
        sendFailReply(minidrive::error::FS_ERROR.code(), std::string("failed to remove directory: ") + result.string());
        return;
    }

    sendOkReply("");
}


void Session::handleAUTH(const std::string &cmd, const json &args, const json &data) {
    // TODO error if already authenticated
    if (_mode != mode::NOT_AUTHENTICATED) {
        spdlog::warn("session already authenticated");
        sendFailReply(minidrive::error::ALREADY_AUTHENTICATED.code(), "");
        return;
    }
    if (!data.contains("mode")) { // check if request contains 'mode' argument
        spdlog::warn("request does not contain 'mode'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "mode");
        return;
    }
    const std::string &mode = data["mode"];
    // public mode
    if (mode == "public") {
        if (!_server->fs_createDir(PUBLIC_DIR_PATH)) {
            spdlog::error("failed to create public directory");
            sendFailReply(minidrive::error::FS_ERROR.code(), "failed to create public directory");
            return;
        }
        _mode = mode::PUBLIC;
        spdlog::info("authenticated as public user");
        sendOkReply("running as public user");
    }
    // private mode
    else if (mode == "private") {
        spdlog::info("attempting authentication as private user");
        if (!args.contains("username")) {
            spdlog::warn("request does not contain 'username'");
            sendFailReply(minidrive::error::MISSING_ARGUMENT, "username");
            return;
        }
        if (!args.contains("password")) {
            spdlog::warn("request does not contain 'password'");
            sendFailReply(minidrive::error::MISSING_ARGUMENT, "password");
            return;
        }
        const std::string &username = args["username"];
        const std::string &password = args["password"];
        if (_server->auth_userExists(username)) {
            if (_server->auth_verifyPassword(username, password)) {
                if (!_server->fs_createDir(USERDATA_DIR_PATH / username)) {
                    spdlog::error("failed to create user directory");
                    sendFailReply(minidrive::error::FS_ERROR.code(), "failed to create user directory");
                    return;
                }
                _mode = mode::PRIVATE;
                _username = username;
                spdlog::info("authentication success, user: '{}'", username);
                sendOkReply("");
            } else {
                spdlog::warn("incorrect password for user '{}'", username);
                sendFailReply(minidrive::error::INCORRECT_PASSWORD.code(), "");
            }
        } else {
            spdlog::warn("user '{}' does not exist", username);
            sendFailReply(minidrive::error::USER_NOT_FOUND.code(), username);
        }
        
    }
    else { // incorrect (not public nor private)
        spdlog::warn("uknown mode: '{}'", mode);
        sendFailReply(minidrive::error::MISSING_ARGUMENT, "mode must be 'public' or 'private'");
        return;
    }
}

void Session::handleREGISTER(const std::string &cmd, const json &args, const json &data) {
    if (!args.contains("username")) {
        spdlog::warn("request does not contain 'username'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT, "username");
        return;
    }
    if (!args.contains("password")) {
        spdlog::warn("request does not contain 'password'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT, "password");
        return;
    }
    const std::string &username = args["username"];
    const std::string &password = args["password"];
    if (_server->auth_userExists(username)) {
        spdlog::warn("user '{}' already exists", username);
        sendFailReply(minidrive::error::USER_ALREADY_EXISTS.code(), username);
        return;
    }
    if (_server->auth_createUser(username, password)) {
        spdlog::info("user 'username' was registered");
        sendOkReply("user registered");
    } else {
        spdlog::warn("failed to register user 'username', password hashing failed (probably)");
        sendFailReply(minidrive::error::USER_REGISTER.code(), "password hashing failed somehow :(");
    }
}