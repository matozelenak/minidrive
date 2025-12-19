#pragma once
#include "session.hpp"
#include <asio.hpp>
#include <string>
#include <list>
#include <memory>
#include <mutex>

class MiniDriveServer {
public:
    MiniDriveServer(asio::io_context &io, uint16_t port, std::string &rootDir);
    void start();
    void stop();
    void accept();

private:
    void _addSession(std::unique_ptr<Session> session);
    void _cleanupSessions();
    void _timerHandler(const asio::error_code& ec);


    inline const static int TIMER_PERIOD = 3;

    std::atomic<bool> _running;
    uint16_t _port;
    std::string _rootDir;
    asio::io_context &_io;
    asio::ip::tcp::acceptor _acceptor;
    std::list<std::unique_ptr<Session>> _sessions;
    std::mutex _listMutex;

    asio::steady_timer _timer;
    std::function<void(const asio::error_code&)> _timerFunc;
};