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

void Session::handleLIST(const json &args) {
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

void Session::handleREMOVE(const json &args) {
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

void Session::handleCD(const json &args) {
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
        _uwd = fs::relative(result, (_mode == mode::PRIVATE ? USERDATA_DIR_PATH / _username : PUBLIC_DIR_PATH));
        spdlog::info("changed UWD: {}", _uwd.string());
        sendOkReply("");
    } catch (const fs::filesystem_error &e) {
        spdlog::error("fs::relative(): {}", e.what());
        sendFailReply(minidrive::error::FS_ERROR.code(), "filesystem error");
    }
}

void Session::handleMKDIR(const json &args) {
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

void Session::handleRMDIR(const json &args) {
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

void Session::handleCOPY(const nlohmann::json &args, bool move) {
    if (_mode == mode::NOT_AUTHENTICATED) {
        spdlog::warn("session is not authenticated");
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), "not authenticated");
        return;
    }
    if (!args.contains("src")) {
        spdlog::warn("request does not contain 'src'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "src");
        return;
    }
    if (!args.contains("dst")) {
        spdlog::warn("request does not contain 'dst'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "dst");
        return;
    }
    std::string src = args["src"];
    std::string dst = args["dst"];
    auto[resultSrc, validSrc] = _server->fs_resolvePath(this, src);
    if (!validSrc) {
        spdlog::warn("access denied: {}", resultSrc.string());
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), resultSrc.string());
        return;
    }
    auto[resultDst, validDst] = _server->fs_resolvePath(this, dst);
    if (!validDst) {
        spdlog::warn("access denied: {}", resultDst.string());
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), resultDst.string());
        return;
    }
    if (!_server->fs_exists(resultSrc)) {
        spdlog::warn("target does not exist: {}", resultSrc.string());
        sendFailReply(minidrive::error::TARGET_NOT_FOUND.code(), resultSrc.string());
        return;
    }

    try {
        fs::copy(resultSrc, resultDst);
        if (move) fs::remove_all(resultSrc);
    } catch (const fs::filesystem_error &e) {
        spdlog::error("copy/move failed: {}", e.what());
        sendFailReply(minidrive::error::FS_ERROR.code(), e.what());
        return;
    }

    sendOkReply("");
}


void Session::handleAUTH(const json &args) {
    if (_mode != mode::NOT_AUTHENTICATED) {
        spdlog::warn("session already authenticated");
        sendFailReply(minidrive::error::ALREADY_AUTHENTICATED.code(), "");
        return;
    }
    if (!args.contains("mode")) { // check if request contains 'mode' argument
        spdlog::warn("request does not contain 'mode'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "mode");
        return;
    }
    const std::string &mode = args["mode"];
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
        if (!args.contains("username")) {
            spdlog::warn("request does not contain 'username'");
            sendFailReply(minidrive::error::MISSING_ARGUMENT, "username");
            return;
        }
        const std::string &username = args["username"];

        if (!args.contains("password")) {
            spdlog::info("checking for user existence: '{}'", username);
            if (_server->auth_userExists(username)) {
                spdlog::info("found");
                sendOkReply("");
            } else {
                spdlog::info("not found");
                sendFailReply(minidrive::error::USER_NOT_FOUND.code(), username);
            }
            return;
        }

        const std::string &password = args["password"];
        spdlog::info("attempting authentication as private user");
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

void Session::handleREGISTER(const json &args) {
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


void Session::handleUPLOAD(const json &args, const json &data) {
    if (!args.contains("src")) {
        spdlog::warn("request does not contain 'src'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "src");
        return;
    }
    if (!args.contains("size")) {
        spdlog::warn("request does not contain 'size'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "size");
        return;
    }
    std::string src = args["src"];
    std::string dst;
    if (args.contains("dst")) {
        dst = args["dst"];
    }
    
    auto[result, valid] = _server->fs_resolvePath(this, dst);
    if (!valid) {
        spdlog::warn("access denied: {}", result.string());
        sendFailReply(minidrive::error::ACCESS_DENIED.code(), result.string());
        return;
    }
    if (_server->fs_exists(result)) {
        fs::file_type type = _server->fs_getFileType(result);
        if (type == fs::file_type::directory) {
            result /= fs::path(src).filename();
            if (_server->fs_exists(result)) {
                spdlog::warn("target already exists: {}", result.string());
                sendFailReply(minidrive::error::TARGET_ALREADY_EXISTS.code(), result.string());
                return;
            } // otherwise its good
        } else if (type != fs::file_type::none) {
            spdlog::warn("target already exists: {}", result.string());
            sendFailReply(minidrive::error::TARGET_ALREADY_EXISTS.code(), result.string());
            return;
        }
    }

    
    _transfer.type = Transfer::Type::UPLOAD;
    _transfer.jsonFilename = "transfer." + _username + ".json";
    _transfer.command = data;
    _transfer.size = args["size"];
    _transfer.sequenceNum = 0;
    _transfer.chunkSize = 4096;
    _transfer.resolvedPath = result;
    _transfer.resolvedPathTmp = result;
    _transfer.resolvedPathTmp.replace_extension(result.extension().string() + ".part");
    
    std::fstream stream(_transfer.resolvedPathTmp, std::ios_base::out | std::ios_base::binary);
    if (stream.fail()) {
        spdlog::error("failed to create upload stream");
        sendFailReply(minidrive::error::FS_ERROR.code(), "failed to create upload stream");
        return;
    }
    _transfer.stream = std::move(stream);
    _transfer.active = true;

    sendOkReply("", { {"seq", _transfer.sequenceNum}, {"chunk_size", _transfer.chunkSize} });
}

void Session::handleDOWNLOAD(const json &args, const json &data) {
    if (data.contains("seq") && data["seq"].is_number()) {
        // send file chunk
        if (!_transfer.active || _transfer.type != Transfer::Type::DOWNLOAD) {
            spdlog::error("received file chunk request but there is no active download");
            return;
        }
        _transfer.sequenceNum = data["seq"];

        if (_transfer.sequenceNum > _transfer.size) {
            spdlog::error("for some reason this happened, {} > {}", _transfer.sequenceNum, _transfer.size);
        }

        if (_transfer.sequenceNum == _transfer.size) {
            // finished
            _transfer.active = false;
            _transfer.stream.close();
            deleteTransferFile();
            spdlog::info("transfer finished");
            return;
        }

        _transfer.stream.seekg(_transfer.sequenceNum);
        uintmax_t actualSize = std::min(_transfer.chunkSize, _transfer.size - _transfer.sequenceNum);
        MsgPayload payload(actualSize);
        _transfer.stream.read(reinterpret_cast<char *>(payload.data()), payload.size());
        spdlog::debug("sending {} Bytes, seq: {}", actualSize, _transfer.sequenceNum);
        _cmdSocket.sendMessage(data_type::DATA, std::move(payload));
        return;
    }

    // check if the file actually exists and if the user has access to it

    if (!args.contains("src")) {
        spdlog::warn("request does not contain 'src'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "src");
        return;
    }
    std::string src = args["src"];
    auto[result, valid] = _server->fs_resolvePath(this, src);
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
    uintmax_t size = 0;
    try {
        size = fs::file_size(result);
    } catch (const fs::filesystem_error &e) {
        spdlog::error("failed to get file size: {}", e.what());
        sendFailReply(minidrive::error::FS_ERROR.code(), "failed to get file size");
        return;
    }

    _transfer.type = Transfer::Type::DOWNLOAD;
    _transfer.jsonFilename = "transfer." + _username + ".json";
    _transfer.command = data;
    _transfer.size = size;
    _transfer.sequenceNum = 0;
    _transfer.chunkSize = 4096;
    _transfer.resolvedPath = result;
    _transfer.resolvedPathTmp = "";
    
    std::fstream stream(_transfer.resolvedPath, std::ios_base::in | std::ios_base::binary);
    if (stream.fail()) {
        spdlog::error("failed to create download stream");
        sendFailReply(minidrive::error::FS_ERROR.code(), "failed to create download stream");
        return;
    }
    _transfer.stream = std::move(stream);
    _transfer.active = true;

    sendOkReply("", { {"seq", _transfer.sequenceNum}, {"chunk_size", _transfer.chunkSize}, {"size", _transfer.size} });
}