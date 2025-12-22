#include "session.hpp"
#include <asio.hpp>
#include <memory>
#include <string>
#include <istream>
#include <mutex>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <cstdint>

using asio::ip::tcp;
using nlohmann::json;

Session::Session(MiniDriveServer *server, tcp::socket &&cmdSocket)
    :  _server(server), _cmdSocket(std::move(cmdSocket)) {
    
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
        return;
    }
    
    spdlog::debug("msg: \"{}\"", data.dump());

    if (!data.contains("cmd")) {
        spdlog::error("request did not contain 'cmd'");
        return;
    }
    
    try {
        const std::string &cmd = data["cmd"];
        const json &args = data.contains("args") ? data["args"] : json::object();
        if (cmd == "LIST") {
            std::string path;
            if (args.contains("path")) {
                path = args["path"];
            }
            // if path is absolute, prepend only the USER directory
            // otherwise prepend USER and CWD
            // ask server to list files
            _cmdSocket.sendMessage("toto je odpoved!");
        }
        else {
            spdlog::error("unknown command: {}", cmd);
        }

    } catch (const json::type_error &e) {
        spdlog::error("type_error: {}", e.what());
    }
}

