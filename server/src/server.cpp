#include "server.hpp"
#include <asio.hpp>
#include <string>
#include <memory>
#include <chrono>
#include <functional>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "session.hpp"
#include "globals.hpp"
#include "fs_module.hpp"

using asio::ip::tcp;
namespace fs = std::filesystem;
using json = nlohmann::json;

MiniDriveServer::MiniDriveServer(asio::io_context &io, uint16_t port)
    : _port(port), _io(io), _acceptor(io/*, tcp::endpoint(tcp::v4(), port)*/),
      _timer(io) {
        
    _timer.expires_after(std::chrono::seconds(TIMER_PERIOD));
    _timerFunc = [this](const asio::error_code& ec) {_timerHandler(ec);};
}

void MiniDriveServer::start() {
    if (_running) return;
    _running = true;
    if (!fs::exists(PUBLIC_DIR_PATH)) {
        spdlog::info("creating public directory");
        try {
            fs::create_directories(PUBLIC_DIR_PATH);
        } catch (const fs::filesystem_error &e) {
            spdlog::error("create_directories: {}", e.what());
            stop();
            return;
        }
    }
    if (!fs::exists(USERDATA_DIR_PATH)) {
        spdlog::info("creating user_data directory");
        try {
            fs::create_directories(USERDATA_DIR_PATH);
        } catch (const fs::filesystem_error &e) {
            spdlog::error("create_directories: {}", e.what());
            stop();
            return;
        }
    }
    if (!_authModule.loadConfig()) {
        stop();
        return;
    }

    // open and bind the socket
    tcp::endpoint addr(tcp::v4(), _port);
    try {
        _acceptor.open(addr.protocol());
    } catch (const asio::system_error &e) {
        spdlog::critical("open(): could not open socket: {}", e.what());
        stop();
        return;
    }
    try {
        _acceptor.bind(addr);
    } catch (const asio::system_error &e) {
        spdlog::critical("bind(): could not bind socket: {}", e.what());
        stop();
        return;
    }
    try {
        _acceptor.listen();
    } catch (const asio::system_error &e) {
        spdlog::critical("listen(): could not start listening for connections: {}", e.what());
        stop();
        return;
    }

    accept();
    _timer.async_wait(_timerFunc);
}

void MiniDriveServer::stop() {
    if (!_running) return;
    _running = false;
    _authModule.saveConfig();
    _io.stop();
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


std::pair<fs::path, bool> MiniDriveServer::fs_resolvePath(Session *session, std::string other) {
    std::pair<fs::path, bool> result;
    fs::path startPath, uwd;
    if (session->getMode() == Session::mode::PUBLIC) {
        startPath = PUBLIC_DIR_PATH;
    } else if (session->getMode() == Session::mode::PRIVATE) {
        startPath = USERDATA_DIR_PATH / session->getUsername();
        uwd = session->getUWD();
    }
    else {
        spdlog::warn("NOT_AUTHENTICATED session tried to resolve a path");
        return result;
    }
    result.first = ::fs_resolvePath(startPath, uwd, other);
    result.second = ::fs_validatePath(startPath, result.first);
    return result;
}

json MiniDriveServer::fs_listFiles(fs::path path, bool includeHash) {
    json result = json::array();
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(path, ec)) {
        try {
            auto status = entry.status();
            json file;
            file["name"] = entry.path();
            file["type"] = status.type();
            file["size"] = (status.type() == fs::file_type::regular ? entry.file_size() : 0);
            result.push_back(file);
        } catch (const fs::filesystem_error &e) {
            spdlog::error("listFiles(): {}", e.what());
        }
    }
    if (ec) {
        spdlog::error("listFiles(): {}", ec.message());
    }
    return result;
}

bool MiniDriveServer::fs_createDir(fs::path path, bool createParent) {
    try {
        createParent ? fs::create_directories(path) : fs::create_directory(path);
    } catch (const fs::filesystem_error &e) {
        return false;
    }
    return true;
}

bool MiniDriveServer::fs_removeDir(fs::path path) {
    try {
        fs::remove_all(path);
    } catch (const fs::filesystem_error &e) {
        return false;
    }
    return true;
}

bool MiniDriveServer::fs_exists(fs::path path) {
    return fs::exists(path);
}

bool MiniDriveServer::fs_remove(fs::path path) {
    try {
        fs::remove(path);
    } catch (const fs::filesystem_error &e) {
        return false;
    }
    return true;
}

fs::file_type MiniDriveServer::fs_getFileType(fs::path path) {
    try {
        auto status = fs::status(path);
        return status.type();
    } catch (const fs::filesystem_error &e) {
        
    }
    return fs::file_type::none;
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