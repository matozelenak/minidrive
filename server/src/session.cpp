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
    if (_transfer.active) { // save
        saveTransfer();
    }
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
                break;
            }
            case data_type::DATA:
            {
                processData(*payload);
                break;
            }
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
        else if (cmd == "UPLOAD") handleUPLOAD(cmd, args, data);
        else {
            spdlog::error("unknown command: {}", cmd);
            sendFailReply(minidrive::error::UNKNOWN_COMMAND.code(), cmd);
        }

    } catch (const json::type_error &e) {
        spdlog::error("type_error: {}", e.what());
        sendFailReply(minidrive::error::JSON_TYPE_ERROR.code(), e.what());
    }
}

void Session::processData(const MsgPayload &payload) {
    if (!_transfer.active) {
        spdlog::error("received data but there are no active transfers");
        return;
    }
    _transfer.stream.write(reinterpret_cast<const char*>(payload.data()), payload.size()); // TODO handle errors
    _transfer.sequenceNum += payload.size();

    if (_transfer.sequenceNum > _transfer.size) {
        spdlog::error("for some reason this happened, {} > {}", _transfer.sequenceNum, _transfer.size);
    }

    if (_transfer.sequenceNum == _transfer.size) {
        // finished
        _transfer.active = false;
        _transfer.stream.flush();
        _transfer.stream.close();
        fs::rename(_transfer.resolvedPathTmp, _transfer.resolvedPath); // TODO handle errors
        deleteTransferFile();
        spdlog::info("transfer finished");
    }
    sendOkReply("", { {"seq", _transfer.sequenceNum}, {"chunk_size", _transfer.chunkSize} });
}



void Session::saveTransfer() {
    std::fstream out(USERDATA_DIR_PATH / _transfer.jsonFilename, std::ios_base::out);
    json j;
    j["command"] = _transfer.command;
    j["resolvedPath"] = _transfer.resolvedPath.string();
    j["resolvedPathTmp"] = _transfer.resolvedPathTmp.string();
    j["size"] = _transfer.size;
    j["chunkSize"] = _transfer.chunkSize;
    j["sequenceNum"] = _transfer.sequenceNum;
    
    out << j.dump();
    if (out.fail()) {
        spdlog::error("failed to save transfer file for user '{}'", _username);
    } else {
        spdlog::info("transfer file for user '{}' saved", _username);
    }
    out.close();
}

bool Session::loadTransfer() {
    std::fstream in(USERDATA_DIR_PATH / _transfer.jsonFilename, std::ios_base::in);
    if (in.fail()) {
        spdlog::info("no transfer file for user '{}'", _username);
        return false;
    }
    json j;
    try {
        json j = json::parse(in);
    } catch (const json::parse_error &e) {
        spdlog::error("failed to parse transfer file for user '{}', {}", _username, e.what());
        return false;
    }
    try {
        if (j.contains("command")) _transfer.command = j["command"];
        if (j.contains("resolvedPath")) _transfer.resolvedPath = j["resolvedPath"].get<std::string>();
        if (j.contains("resolvedPathTmp")) _transfer.resolvedPathTmp = j["resolvedPathTmp"].get<std::string>();
        if (j.contains("size")) _transfer.size = j["size"];
        if (j.contains("chunkSize")) _transfer.chunkSize = j["chunkSize"];
        if (j.contains("sequenceNum")) _transfer.sequenceNum = j["sequenceNum"];
    } catch (const json::type_error &e) {
        spdlog::error("type error: user '{}', {}", _username, e.what());
        return false;
    }
    spdlog::info("transfer file for user '{}' loaded", _username);
    return true;
}

void Session::deleteTransferFile() {
    try {
        fs::remove(USERDATA_DIR_PATH / _transfer.jsonFilename);
    } catch (const fs::filesystem_error &e) {
        spdlog::error("failed to remove transfer file: {}", e.what());
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

