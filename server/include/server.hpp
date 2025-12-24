#pragma once
#include <asio.hpp>
#include <string>
#include <list>
#include <memory>
#include <mutex>
#include "auth.hpp"
#include "session.hpp"

// class Session;

class MiniDriveServer {
public:
    MiniDriveServer(asio::io_context &io, uint16_t port);
    void start();
    void stop();
    void accept();

    inline bool auth_userExists(const std::string &username) const {return _authModule.userExists(username);}
    inline bool auth_verifyPassword(const std::string &username, const std::string &password) const {return _authModule.verifyPassword(username, password);}
    inline bool auth_createUser(const std::string &username, const std::string &password) {return _authModule.createUser(username, password);}

private:
    void _addSession(std::unique_ptr<Session> session);
    void _cleanupSessions();
    void _timerHandler(const asio::error_code& ec);


    inline const static int TIMER_PERIOD = 60;

    std::atomic<bool> _running;
    uint16_t _port;
    asio::io_context &_io;
    asio::ip::tcp::acceptor _acceptor;
    std::list<std::unique_ptr<Session>> _sessions;
    std::mutex _listMutex;

    asio::steady_timer _timer;
    std::function<void(const asio::error_code&)> _timerFunc;

    AuthModule _authModule;
};