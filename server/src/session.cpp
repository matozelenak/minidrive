#include "session.hpp"
#include <asio.hpp>
#include <memory>
#include <string>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <cstdint>

#include "minidrive/error_codes.hpp"
#include "server.hpp"
#include "globals.hpp"

using asio::ip::tcp;
using nlohmann::json;
namespace fs = std::filesystem;

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
    const json &args = data.contains("args") ? data["args"] : json::object();

    try {
        spdlog::info("command: {}", cmd);
        if (cmd == "LIST") handleLIST(cmd, args, data);
        else if (cmd == "REMOVE") handleREMOVE(cmd, args, data);
        else if (cmd == "CD") handleCD(cmd, args, data);
        else if (cmd == "MKDIR") handleMKDIR(cmd, args, data);
        else if (cmd == "RMDIR") handleRMDIR(cmd, args, data);
        else if (cmd == "AUTH") handleAUTH(cmd, args, data);
        else if (cmd == "REGISTER") handleREGISTER(cmd, args, data);
        else {
            spdlog::error("unknown command: {}", cmd);
            sendFailReply(minidrive::error::UNKNOWN_COMMAND.code(), cmd);
        }

    } catch (const json::type_error &e) {
        spdlog::error("type_error: {}", e.what());
        sendFailReply(minidrive::error::JSON_TYPE_ERROR.code(), e.what());
    }
}


json Session::makeOkReply(const std::string &msg, const json &data) {
    json reply = { {"status", "OK"}, {"code", minidrive::error::SUCCESS.code()}, {"message", msg}, {"data", data}, {"uwd", _uwd.string()}};
    return reply;
}

json Session::makeFailReply(uint32_t code, const std::string &msg) {
    json reply = { {"status", "FAIL"}, {"code", code}, {"message", msg} };
    return reply;
}

void Session::sendOkReply(const std::string &msg, const json &data) {
    _cmdSocket.sendMessage(makeOkReply(msg, data).dump());
}

void Session::sendFailReply(uint32_t code, const std::string &msg) {
    _cmdSocket.sendMessage(makeFailReply(code, msg).dump());
}

