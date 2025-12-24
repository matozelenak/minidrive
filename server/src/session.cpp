#include "session.hpp"
#include <asio.hpp>
#include <memory>
#include <string>
#include <istream>
#include <mutex>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <cstdint>

#include "minidrive/error_codes.hpp"
#include "server.hpp"

using asio::ip::tcp;
using nlohmann::json;

Session::Session(MiniDriveServer *server, tcp::socket &&cmdSocket)
    :  _server(server), _cmdSocket(std::move(cmdSocket)), _mode(mode::NOT_AUTHENTICATED) {
    
}

Session::~Session() {
    spdlog::debug("~Session()");
}

bool Session::isDead() const {
    return _cmdSocket.isDead();
}

void Session::start() {
    _cmdSocket.start(
        [this](data_type type, std::shared_ptr<MsgPayload> payload) {
            switch(type) {
            case data_type::COMMAND:
            {
                const auto &endpoint = _cmdSocket.getSocket().remote_endpoint();
                spdlog::debug("IP: {}, port: {}, msg type: {}, payload length: {}", endpoint.address().to_string(),
                    endpoint.port(), static_cast<uint32_t>(type), payload->size());
                processMessage(*payload);
            }
                break;
            default:
                break;
            }
        },
        [this](const asio::error_code &ec) {
            const auto &endpoint = _cmdSocket.getSocket().remote_endpoint();
            if (ec == asio::error::eof) {
                spdlog::info("client disconnected: IP: {}, port: {}",
                    endpoint.address().to_string(), endpoint.port());
            } else {
                spdlog::error("client error occurred: IP: {}, port: {}, error: {}",
                    endpoint.address().to_string(), endpoint.port(), ec.message());
            }
        },
        [this](const asio::error_code &ec) {
            const auto &endpoint = _cmdSocket.getSocket().remote_endpoint();
            if (ec == asio::error::eof) {
                spdlog::info("client disconnected: IP: {}, port: {}",
                    endpoint.address().to_string(), endpoint.port());
            } else {
                spdlog::error("client (write) error occurred: IP: {}, port: {}, error: {}",
                    endpoint.address().to_string(), endpoint.port(), ec.message());
            }
        }
    );
}


void Session::processMessage(const MsgPayload &payload) {
    json data;
    try {
        data = json::parse(payload.begin(), payload.end());
    } catch (const json::parse_error &e) {
        spdlog::error("json parse failed: {}", e.what());
        sendFailReply(minidrive::error::JSON_PARSE_ERROR.code(), e.what());
        return;
    }
    
    spdlog::debug("msg: '{}'", data.dump());

    if (!data.contains("cmd")) {
        spdlog::error("request did not contain 'cmd'");
        sendFailReply(minidrive::error::MISSING_ARGUMENT.code(), "cmd");
        return;
    }
    
    if (!data["cmd"].is_string()) {
        sendFailReply(minidrive::error::JSON_TYPE_ERROR.code(), "cmd must be string");
        return;
    }
    const std::string &cmd = data["cmd"];

    handleMessage(cmd, data);
}

void Session::handleMessage(const std::string &cmd, const json &data) {
    const json &args = data.contains("args") ? data["args"] : json::object();

    try {
        spdlog::info("command: {}", cmd);
        if (cmd == "LIST") {
            std::string path;
            if (args.contains("path")) {
                path = args["path"];
            }
            // if path is absolute, prepend only the USER directory
            // otherwise prepend USER and CWD
            // ask server to list files
            sendOkReply("toto je odpoved!");
        }
        else if (cmd == "AUTH") {
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
        else if (cmd == "REGISTER") {
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
        else {
            spdlog::error("unknown command: {}", cmd);
            sendFailReply(minidrive::error::UNKNOWN_COMMAND.code(), cmd);
        }

    } catch (const json::type_error &e) {
        spdlog::error("type_error: {}", e.what());
        sendFailReply(minidrive::error::JSON_TYPE_ERROR.code(), e.what());
    }
}


json Session::makeOkReply(const std::string &msg) {
    json reply = { {"status", "OK"}, {"code", minidrive::error::SUCCESS.code()}, {"message", msg} };
    return reply;
}

json Session::makeFailReply(uint32_t code, const std::string &msg) {
    json reply = { {"status", "FAIL"}, {"code", code}, {"message", msg} };
    return reply;
}

void Session::sendOkReply(const std::string &msg) {
    _cmdSocket.sendMessage(makeOkReply(msg).dump());
}

void Session::sendFailReply(uint32_t code, const std::string &msg) {
    _cmdSocket.sendMessage(makeFailReply(code, msg).dump());
}

