#pragma once
#include <asio.hpp>
#include <string>
#include <queue>
#include <mutex>
#include <nlohmann/json.hpp>
#include "minidrive/async_socket.hpp"
// #include "server.hpp"

class MiniDriveServer;

class Session {
public:
    Session(MiniDriveServer *server, asio::ip::tcp::socket &&cmdSocket);
    ~Session();
    bool isDead() const;
    void start();
    
    void processMessage(const MsgPayload &payload);
    void handleMessage(const std::string &cmd, const nlohmann::json &data);
    
    nlohmann::json makeOkReply(const std::string &msg);
    nlohmann::json makeFailReply(uint32_t code, const std::string &msg);

    void sendOkReply(const std::string &msg);
    void sendFailReply(uint32_t code, const std::string &msg);

    enum class mode {NOT_AUTHENTICATED, PUBLIC, PRIVATE};

private:
    mode _mode;
    std::string _username;

    MiniDriveServer *_server;
    AsyncSocket _cmdSocket;
};