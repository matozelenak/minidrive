#include "server.hpp"
#include <asio.hpp>
#include <string>
#include <memory>
#include <chrono>
#include <functional>
#include <spdlog/spdlog.h>

using asio::ip::tcp;

MiniDriveServer::MiniDriveServer(asio::io_context &io, uint16_t port, std::string &rootDir)
    : _port(port), _rootDir(rootDir), _io(io), _acceptor(io, tcp::endpoint(tcp::v4(), port)),
      _timer(io) {
        
    _timer.expires_after(std::chrono::seconds(TIMER_PERIOD));
    _timerFunc = [this](const asio::error_code& ec) {_timerHandler(ec);};
}

void MiniDriveServer::start() {
    if (_running) return;
    _running = true;

    accept();

    _timer.async_wait(_timerFunc);
}

void MiniDriveServer::stop() {
    if (!_running) return;
    _running = false;

    _timer.cancel();
}

void MiniDriveServer::accept() {
    _acceptor.async_accept([this](const asio::error_code& ec, tcp::socket socket) {
        if (ec) {
            spdlog::error("accept(): {}", ec.message());
            accept();
            return;
        }

        spdlog::info("new client connection: IP: {}, port: {}",
            socket.remote_endpoint().address().to_string(),
            socket.remote_endpoint().port());

        auto session = std::make_unique<Session>(this, std::move(socket));
        session->start();
        _addSession(std::move(session));

        accept();
    });
}




void MiniDriveServer::_addSession(std::unique_ptr<Session> session) {
    std::lock_guard g(_listMutex);
    _sessions.push_back(std::move(session));
}

void MiniDriveServer::_cleanupSessions() {
    std::lock_guard g(_listMutex);
    spdlog::debug("list size: {}", _sessions.size());
    for (auto it = _sessions.begin(); it != _sessions.end();) {
        if ((*it)->isDead()) it = _sessions.erase(it);
        else ++it;
    }

}

void MiniDriveServer::_timerHandler(const asio::error_code& ec) {
    if (ec == asio::error::operation_aborted) {
        spdlog::info("timer aborted: {}", ec.message());
        return;
    }
    else if (ec) {
        spdlog::error("timer: {}", ec.message());
        return;
    }
    // spdlog::debug("timer!");
    _cleanupSessions();
    _timer.expires_after(std::chrono::seconds(TIMER_PERIOD));
    _timer.async_wait(_timerFunc);
}