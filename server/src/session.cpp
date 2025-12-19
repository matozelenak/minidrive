#include "session.hpp"
#include <asio.hpp>
#include <memory>
#include <string>
#include <istream>
#include <mutex>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

using asio::ip::tcp;
using nlohmann::json;

Session::Session(MiniDriveServer &server, tcp::socket &&socket)
    : _isDead(false), _server(server), _socket(std::move(socket)) {
    
}

bool Session::isDead() const {
    return _isDead;
}

void Session::doRead() {
    asio::async_read_until(_socket, _readBuffer, '\n',
        [this](const asio::error_code& ec, std::size_t bytes_transferred) {
        if (!ec) {
            std::istream is(&_readBuffer);
            std::string line;
            std::getline(is, line); // removes '\n'
            spdlog::debug("IP: {}, port: {}, msg: \"{}\"",
                _socket.remote_endpoint().address().to_string(), _socket.remote_endpoint().port(), line);
            processMessage(line);
            doRead();
        }
        else if (ec == asio::error::eof) {
            spdlog::info("client disconnected: IP: {}, port: {}",
                _socket.remote_endpoint().address().to_string(), _socket.remote_endpoint().port());
            _isDead = true;
        } else {
            spdlog::error("client error occurred: IP: {}, port: {}",
                _socket.remote_endpoint().address().to_string(), _socket.remote_endpoint().port());
            _isDead = true;
        }
    });
}

void Session::doWrite() {
    _writeQueueMutex.lock();
    if (_writeBusy) {
        _writeQueueMutex.unlock();
        return;
    }
    if (_writeQueue.empty()) {
        _writeQueueMutex.unlock();
        return;
    }
    auto msg = std::make_shared<std::string>(std::move(_writeQueue.front()));
    _writeQueue.pop();
    _writeBusy = true;
    _writeQueueMutex.unlock();

    asio::async_write(_socket, asio::buffer(*msg),
        [this, msg](const asio::error_code& ec, std::size_t bytes_transferred) {
        if (!ec) {
            spdlog::debug("data sent: {}", *msg);
        }
        else {
            spdlog::error("socket write: {}", ec.message());
        }

        _writeBusy = false;
        doWrite();
    });
}

// void Session::write(std::shared_ptr<std::string> msg) {
    
// }

void Session::processMessage(const std::string &msg) {
    json data;
    try {
        data = json::parse(msg);
    } catch (const json::parse_error &e) {
        spdlog::error("json parse failed: {}", e.what());
        spdlog::error("input: '{}'", msg);
        return;
    }
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
            sendReply("toto je odpoved!\n");
        }
        else {
            spdlog::error("unknown command: {}", cmd);
        }

    } catch (const json::type_error &e) {
        spdlog::error("type_error: {}", e.what());
    }
}

void Session::sendReply(std::string &&msg) {
    {
        std::lock_guard lock(_writeQueueMutex);
        _writeQueue.push(std::move(msg));
    }
    doWrite();
}