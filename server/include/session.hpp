#pragma once
#include <asio.hpp>
#include <string>
#include <queue>
#include <mutex>
#include "minidrive/async_socket.hpp"

class MiniDriveServer;

class Session {
public:
    Session(MiniDriveServer *server, asio::ip::tcp::socket &&cmdSocket);
    ~Session();
    bool isDead() const;
    void start();
    
    void processMessage(const MsgPayload &payload);
    // void sendReply(std::string &&msg);

private:
    
    MiniDriveServer *_server;
    AsyncSocket _cmdSocket;
};