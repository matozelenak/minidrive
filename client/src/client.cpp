#include "client.hpp"
#include <iostream>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

#include "args.hpp"
#include "minidrive/version.hpp"
#include "minidrive/error_codes.hpp"
#include "minidrive/async_socket.hpp"

using json = nlohmann::json;
using asio::ip::tcp;
namespace fs = std::filesystem;

Client::Client(asio::io_context &io, tcp::socket &&socket, Args &&args)
    :_io(io), _client(std::move(socket)), _args(std::move(args)), _uwd(".") {
    
    _state = (_args.username.empty() ? State::AUTH : State::CHECK_USER);
}

void Client::run() {
    _running = true;
    _client.start(
        [this](data_type type, std::shared_ptr<MsgPayload> payload) {
            std::lock_guard lock(_mutex);
            if (!_waitForReply) {
                spdlog::warn("unwanted message arrived");
                return;
            }
            _replyArrived = true;
            _waitForReply = false;

            switch(type) {
            case data_type::COMMAND:
                spdlog::debug("msg type: {}, payload length: {}", static_cast<uint32_t>(type), payload->size());
                processMessage(*payload);
                break;
            case data_type::DATA:
                processData(*payload);
                break;
            default:
                break;
            }

            _cv.notify_one();
        },
        [this](const asio::error_code &ec) {
            if (ec == asio::error::eof) {
                spdlog::info("server disconnected");
            } else {
                spdlog::error("{}", ec.message());
            }
            stop();
        },
        [this](const asio::error_code &ec) {
            if (ec == asio::error::eof) {
                spdlog::info("server disconnected");
            } else {
                spdlog::error("{}", ec.message());
            }
            stop();
        }
    );

    _th = std::move(std::thread([this]() {userThread();}));

    _io.run();
    _th.join();
}

void Client::stop() {
    _running = false;
    _io.stop();
}

void Client::sendMessage(const std::string &cmd, json &&args) {
    json msg;
    msg["cmd"] = cmd;
    msg["args"] = std::move(args);
    _client.sendMessage(msg.dump());
    _waitForReply = true;
}

void Client::sendData(MsgPayload &&payload) {
    _client.sendMessage(data_type::DATA, std::move(payload));
    _waitForReply = true;
}

void Client::processMessage(const MsgPayload &payload) {
    json data;
    try {
        data = json::parse(payload.begin(), payload.end());
    } catch (const json::parse_error &e) {
        spdlog::error("json parse failed: {}", e.what());
        return;
    }
    
    spdlog::debug("msg: '{}'", data.dump());
    _lastReply = std::move(data);
}

void Client::processData(const MsgPayload &payload) {
    if (!_transfer.active || _transfer.type != Transfer::Type::DOWNLOAD) {
        spdlog::error("received file data but there are no active downloads");
        return;
    }
    _transfer.stream.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    _transfer.sequenceNum += payload.size();
    spdlog::debug("written {} Bytes, seq: {}", payload.size(), _transfer.sequenceNum);

    if (_transfer.sequenceNum > _transfer.size) {
        spdlog::error("for some reason this happened, {} > {}", _transfer.sequenceNum, _transfer.size);
    }

    if (_transfer.sequenceNum == _transfer.size) {
        // finished
        _transfer.active = false;
        _transfer.stream.flush();
        _transfer.stream.close();
        fs::rename(_transfer.resolvedPathTmp, _transfer.resolvedPath); 
        _client.sendMessage(json({ {"cmd", "DOWNLOAD"}, {"seq", _transfer.sequenceNum} }).dump()); // tell server the transfer is finished
        _state = State::COMMAND;
        spdlog::info("transfer finished");
    }
}


void Client::userThread() {
    while (_running) {
        std::unique_lock lock(_mutex);
        if (_waitForReply) {
            _cv.wait(lock, [this] {return _replyArrived.operator bool();});
        }

        if (_replyArrived) {
            _replyArrived = false;
            reactToReply();
        } 
        else {
            processUserInput();
        }
    }
}


void Client::reactToReply() {
    std::string status = _lastReply["status"];
    uint32_t code = _lastReply["code"];
    std::string message = _lastReply["message"];
    json data = (_lastReply.contains("data") ? _lastReply["data"] : json::object());

    switch (_state) {
    case State::CHECK_USER:
    {
        if (code == minidrive::error::SUCCESS.code()) {
            _state = State::AUTH;
        } else if (code == minidrive::error::USER_NOT_FOUND.code()) {
            spdlog::warn("user not found");
            _state = State::REG;
        } else {
            stop();
        }
        break;
    }

    case State::AUTH:
    {
        if (code == minidrive::error::SUCCESS.code()) {
            if (_args.username.empty()) {
                std::cout << "[warning] operating in public mode - files are visible to everyone" << std::endl;
            } else {
                std::cout << "logged in as user '" << _args.username << "'" << std::endl;
            }
            spdlog::info("auth success");
            _state = State::COMMAND;
        } else if (code == minidrive::error::INCORRECT_PASSWORD.code()) {
            spdlog::info("incorrect password");
        } else {
            stop();
        }
        break;
    }

    case State::REG:
    {
        if (code == minidrive::error::SUCCESS.code()) {
            std::cout << "USER REGISTERED" << std::endl;
            spdlog::info("register success");
        } else {
            std::cout << "ERROR: " << code << '\n';
            std::cout << minidrive::getErrorByCode(code)->what() << '\n';
            std::cout << "message: " << message << std::endl;
        }
        stop();
        break;
    }

    case State::COMMAND:
    {
        if (code == minidrive::error::SUCCESS.code()) {
            if (_lastReply.contains("uwd") && _lastReply["uwd"].is_string()) _uwd = _lastReply["uwd"];
            std::cout << "OK" << std::endl;

            // print the LIST command response
            if (data.contains("files") && data["files"].is_array()) {
                printLIST(data["files"]);
            }
        }
        else{
            std::cout << "ERROR: " << code << '\n';
            std::cout << minidrive::getErrorByCode(code)->what() << '\n';
            std::cout << "message: " << message << std::endl;
        }
        break;
    }

    case State::UPLOAD:
    {
        if (code != minidrive::error::SUCCESS.code()) {
            std::cout << "ERROR: " << code << '\n';
            std::cout << minidrive::getErrorByCode(code)->what() << '\n';
            std::cout << "message: " << message << std::endl;
            _state = State::COMMAND;
            break;
        }
        if (!data.contains("chunk_size") || !data["chunk_size"].is_number() || !data.contains("seq") || !data["seq"].is_number()) {
            spdlog::error("upload response did not contain required arguments");
            _state = State::COMMAND;
            break;
        }
        _transfer.active = true;
        _transfer.chunkSize = data["chunk_size"];
        _transfer.sequenceNum = data["seq"];

        // check if upload is finished
        if (_transfer.sequenceNum == _transfer.size) {
            _transfer.stream.close();
            _transfer.active = false;
            _state = State::COMMAND;
            spdlog::info("transfer finished");
            std::cout << "OK\n" << _transfer.size << "B uploaded" << std::endl;
            break;
        }

        // handle upload
        _transfer.stream.seekg(_transfer.sequenceNum);
        uintmax_t actualSize = std::min(_transfer.chunkSize, _transfer.size - _transfer.sequenceNum);
        MsgPayload payload(actualSize);
        _transfer.stream.read(reinterpret_cast<char *>(payload.data()), payload.size());
        spdlog::debug("sending {} Bytes, seq: {}", actualSize, _transfer.sequenceNum);
        sendData(std::move(payload));

        break;
    }

    case State::DOWNLOAD:
    {
        if (code != minidrive::error::SUCCESS.code()) {
            std::cout << "ERROR: " << code << '\n';
            std::cout << minidrive::getErrorByCode(code)->what() << '\n';
            std::cout << "message: " << message << std::endl;
            _transfer.stream.close();
            _state = State::COMMAND;
            break;
        }
        if (!data.contains("seq") || !data.contains("chunk_size") || !data.contains("size")) {
            spdlog::error("reply did not contain required arguments");
            _transfer.stream.close();
            _state = State::COMMAND;
            break;
        }
        _transfer.sequenceNum = data["seq"];
        _transfer.chunkSize = data["chunk_size"];
        _transfer.size = data["size"];
        
        _transfer.active = true;
        _state = State::DOWNLOADING;
        spdlog::info("download initiated");
        _client.sendMessage(json({ {"cmd", "DOWNLOAD"}, {"seq", _transfer.sequenceNum} }).dump());
        _waitForReply = true;
        break;
    }

    case State::DOWNLOADING:
    {
        _client.sendMessage(json({ {"cmd", "DOWNLOAD"}, {"seq", _transfer.sequenceNum} }).dump());
        _waitForReply = true;
        break;
    }
    }
}

void Client::processUserInput() {
    switch (_state) {
    case State::CHECK_USER:
    {
        sendMessage("AUTH", {{"mode", "private"}, {"username", _args.username} });
        break;
    }

    case State::AUTH:
    {
        if (_args.username.empty()) { // public mode
            sendMessage("AUTH", {{"mode", "public"}});
        }
        else { // private mode
            std::cout << "authenticating as user '" << _args.username << "'" << std::endl;
            std::cout << "password: ";
            std::string password;
            std::getline(std::cin, password);
            if (password.back() == '\n') password.pop_back();
            sendMessage("AUTH", {{"mode", "private"}, {"username", _args.username}, {"password", password}});
        }
        break;
    }

    case State::REG:
    {
        std::cout << "user '" << _args.username << "'not found, register? (y/n)" << std::endl;
        char c;
        std::string line;
        std::getline(std::cin, line);
        if (line.empty()) break;
        if (line[0] == 'y') {
            std::cout << "create a password: ";
            std::string password;
            std::getline(std::cin, password);
            if (password.back() == '\n') password.pop_back();
            sendMessage("REGISTER", {{"username", _args.username}, {"password", password}});
        } else if (line[0] == 'n') {
            stop();
        }
        break;
    }

    case State::COMMAND:
        processCommands();
        break;

    case State::UPLOAD:
    case State::DOWNLOAD:
        spdlog::error("should not reach this");
        break;
    }
}

void Client::processCommands() {
    std::cout << _uwd << " > ";

    std::string line;
    std::getline(std::cin, line);

    std::stringstream ss;
    ss << line;

    std::string cmd;
    ss >> cmd;
    spdlog::debug("requested cmd: {}", cmd);
    if (cmd == "LIST") {
        std::string path;
        ss >> path;
        sendMessage("LIST", {{"path", path}});
    }
    else if (cmd == "REMOVE") {
        std::string path;
        ss >> path;
        if (path.empty()) {
            std::cout << "usage: REMOVE <path>" << std::endl;
            return;
        }
        sendMessage("REMOVE", {{"path", path}});
    }
    else if (cmd == "CD") {
        std::string path;
        ss >> path;
        if (path.empty()) {
            std::cout << "usage: CD <path>" << std::endl;
            return;
        }
        sendMessage("CD", {{"path", path}});
    }
    else if (cmd == "MKDIR") {
        std::string path;
        ss >> path;
        if (path.empty()) {
            std::cout << "usage: MKDIR <path>" << std::endl;
            return;
        }
        sendMessage("MKDIR", {{"path", path}});
    }
    else if (cmd == "RMDIR") {
        std::string path;
        ss >> path;
        if (path.empty()) {
            std::cout << "usage: RMDIR <path>" << std::endl;
            return;
        }
        sendMessage("RMDIR", {{"path", path}});
    }
    else if (cmd == "COPY") {
        std::string src, dst;
        ss >> src >> dst;
        if (src.empty() || dst.empty()) {
            std::cout << "usage: COPY <src> <dst>" << std::endl;
            return;
        }
        sendMessage("COPY", { {"src", src}, {"dst", dst} });
    }
    else if (cmd == "MOVE") {
        std::string src, dst;
        ss >> src >> dst;
        if (src.empty() || dst.empty()) {
            std::cout << "usage: MOVE <src> <dst>" << std::endl;
            return;
        }
        sendMessage("MOVE", { {"src", src}, {"dst", dst} });
    }
    else if (cmd == "UPLOAD") {
        std::string src, dst;
        ss >> src >> dst;
        if (src.empty()) {
            std::cout << "usage: UPLOAD <local_path> [remote_path]" << std::endl;
            return;
        }
        
        if (!fs::exists(src)) {
            spdlog::error("{} does not exist", src);
            return;
        }
        try {
            auto type = fs::status(src).type();
            if (type != fs::file_type::regular) {
                std::cout << "error: not a regular file" << std::endl;
                return;
            }
        } catch (const fs::filesystem_error &e) {
            spdlog::error("getFileType: {}", e.what());
            return;
        }
        _transfer.resolvedPath = src;
        _transfer.size = fs::file_size(_transfer.resolvedPath);
        std::fstream stream(_transfer.resolvedPath, std::ios_base::in | std::ios_base::binary);
        if (stream.fail()) {
            spdlog::error("failed to open stream");
            return;
        }
        _transfer.stream = std::move(stream);
        _state = State::UPLOAD;
        sendMessage("UPLOAD", { {"src", src}, {"dst", dst}, {"size", _transfer.size} });
    }
    else if (cmd == "DOWNLOAD") {
        std::string src, dst;
        ss >> src >> dst;
        if (src.empty()) {
            std::cout << "usage: DOWNLOAD <remote_path> [local_path]" << std::endl;
            return;
        }
        if (dst.empty()) dst = ".";
        fs::path result = dst;
        
        if (fs::exists(result)) {
            fs::file_type type = fs::status(result).type();
            if (type == fs::file_type::directory) {
                result /= fs::path(src).filename();
                if (fs::exists(result)) {
                    std::cout << "error: file already exists" << std::endl;
                    return;
                } // otherwise its good
            } else {
                std::cout << "error: file already exists";
                return;
            }
        }
        
        _transfer.type = Transfer::Type::DOWNLOAD;
        _transfer.resolvedPath = result;
        _transfer.resolvedPathTmp = result;
        _transfer.resolvedPathTmp.replace_extension(result.extension().string() + ".part");
        std::fstream stream(_transfer.resolvedPathTmp, std::ios_base::out | std::ios_base::binary);
        if (stream.fail()) {
            std::cout << "failed to create download stream" << std::endl;
            _state = State::COMMAND;
            return;
        }
        _transfer.stream = std::move(stream);
        
        _state = State::DOWNLOAD;
        sendMessage("DOWNLOAD", { {"src", src}, {"dst", dst} });
    }
    else if (cmd == "EXIT") {
        stop();
    }
    else if (cmd == "HELP") {
        printHelp();
    }
    else {
        std::cout << "unknown command" << std::endl;
    }
}


void Client::printLIST(const nlohmann::json &files) {
    for (const auto &entry : files) {
        std::string name;
        int64_t size = 0;
        fs::file_type type = fs::file_type::none;
        if (entry.contains("name")) name = entry["name"];
        if (entry.contains("size")) size = entry["size"];
        if (entry.contains("type")) type = entry["type"];
        std::cout << "name: " << name << "  size: " << size << "B  type: ";
        if (type == fs::file_type::regular) std::cout << "regular";
        else if (type == fs::file_type::directory) std::cout << "directory";
        else std::cout << "unknown";
        std::cout << std::endl;
    }
}

void Client::printHelp() {
    std::cout << "available commands:\n";
    std::cout << "LIST [path]\n";
    std::cout << "REMOVE <path>\n";
    std::cout << "CD <path>\n";
    std::cout << "MKDIR <path>\n";
    std::cout << "RMDIR <path>\n";
    std::cout << "COPY <src> <dst>\n";
    std::cout << "MOVE <src> <dst>\n";
    std::cout << "UPLOAD <local_path> [remote_path]\n";
    std::cout << "DOWNLOAD <remote_path> [local_path]\n";
    std::cout << "EXIT\n";
    std::cout << "HELP\n";
    std::cout << std::endl;
}