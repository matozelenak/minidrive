#include "client.hpp"
#include <iostream>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>

#include "args.hpp"
#include "minidrive/version.hpp"
#include "minidrive/error_codes.hpp"
#include "minidrive/async_socket.hpp"

using json = nlohmann::json;
using asio::ip::tcp;
namespace fs = std::filesystem;

Client::Client(asio::io_context &io, tcp::socket &&socket, Args &&args)
    :_io(io), _client(std::move(socket)), _state(State::AUTH), _args(std::move(args)), _uwd(".") {

}

void Client::run() {
    _running = true;
    _client.start(
        [this](data_type type, std::shared_ptr<MsgPayload> payload) {
            switch(type) {
            case data_type::COMMAND:
                spdlog::debug("msg type: {}, payload length: {}", static_cast<uint32_t>(type), payload->size());
                processMessage(*payload);
                break;
            default:
                break;
            }
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

void Client::processMessage(const MsgPayload &payload) {
    json data;
    try {
        data = json::parse(payload.begin(), payload.end());
    } catch (const json::parse_error &e) {
        spdlog::error("json parse failed: {}", e.what());
        return;
    }
    
    spdlog::debug("msg: '{}'", data.dump());

    std::lock_guard lock(_mutex);
    if (!_waitForReply) {
        spdlog::warn("unwanted message arrived");
        return;
    }

    _replyArrived = true;
    _waitForReply = false;
    _lastReply = std::move(data);
    _cv.notify_one();
}


void Client::userThread() {
    while (_running) {

        std::unique_lock lock(_mutex);
        if (_waitForReply) {
            _cv.wait(lock, [this] {return _replyArrived.operator bool();});
        }

        if (_replyArrived) {
            // react to reply
            _replyArrived = false;

            std::string status = _lastReply["status"];
            uint32_t code = _lastReply["code"];
            std::string message = _lastReply["message"];
            json data = (_lastReply.contains("data") ? _lastReply["data"] : json::object());

            switch(_state) {
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
                } else if (code == minidrive::error::USER_NOT_FOUND.code()) {
                    spdlog::warn("user not found");
                    _state = State::REG; 
                } else if (code == minidrive::error::INCORRECT_PASSWORD.code()){
                    spdlog::info("incorrect password");
                }
                else {
                    stop();
                }
                break;
            }
            case State::REG:
            {
                if (code == minidrive::error::SUCCESS.code()) {
                    spdlog::info("register success");
                    _state = State::COMMAND;
                } else {
                    stop();
                }
                break;
            }
            
            
            case State::COMMAND:
            {
                if (code == minidrive::error::SUCCESS.code()) {
                    if (_lastReply.contains("uwd") && _lastReply["uwd"].is_string()) _uwd = _lastReply["uwd"];
                    std::cout << "OK" << std::endl;
                    if (data.contains("files") && data["files"].is_array()) {
                        json files = data["files"];
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
                } else {
                    std::cout << "ERROR: " << code << '\n';
                    std::cout << minidrive::getErrorByCode(code)->what() << '\n';
                    std::cout << "message: " << message << std::endl;
                }
                break;
            }
            }
            

        } 
        else {
            // just proccess commands
            switch(_state) {
            case State::AUTH:
            {
                if (_args.username.empty()) { // public mode
                    sendMessage("AUTH", { {"mode", "public"} });
                } else { // private mode
                    std::cout << "authenticating as user '" << _args.username << "'" << std::endl;
                    std::cout << "password: ";
                    std::string password;
                    std::getline(std::cin, password);
                    if (password.back() == '\n') password.pop_back();
                    sendMessage("AUTH", { {"mode", "private"}, {"username", _args.username}, {"password", password} });
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
                    sendMessage("REGISTER", { {"username", _args.username}, {"password", password} });
                }
                break;
            }

            case State::COMMAND:
            {
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
                    sendMessage("LIST", { {"path", path}});
                }
                else if (cmd == "REMOVE") {
                    std::string path;
                    ss >> path;
                    if (path.empty()) {
                        std::cout << "usage: REMOVE <path>" << std::endl;
                        break;
                    }
                    sendMessage("REMOVE", { {"path", path}});
                }
                else if (cmd == "CD") {
                    std::string path;
                    ss >> path;
                    if (path.empty()) {
                        std::cout << "usage: CD <path>" << std::endl;
                        break;
                    }
                    sendMessage("CD", { {"path", path}});
                }
                else if (cmd == "MKDIR") {
                    std::string path;
                    ss >> path;
                    if (path.empty()) {
                        std::cout << "usage: MKDIR <path>" << std::endl;
                        break;
                    }
                    sendMessage("MKDIR", { {"path", path}});
                }
                else if (cmd == "RMDIR") {
                    std::string path;
                    ss >> path;
                    if (path.empty()) {
                        std::cout << "usage: RMDIR <path>" << std::endl;
                        break;
                    }
                    sendMessage("RMDIR", { {"path", path}});
                }
                else if (cmd == "EXIT") {
                    stop();
                }
                else {
                    spdlog::warn("unknown command");
                }

                break;
            }
            
            }
        }




    }
}