#pragma once
#include <asio.hpp>
#include <string>
#include <queue>
#include <mutex>
#include <filesystem>
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
    // void handleMessage(const std::string &cmd, const nlohmann::json &data);
    
    void handleLIST(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data);
    void handleREMOVE(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data);
    void handleCD(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data);
    void handleMKDIR(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data);
    void handleRMDIR(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data);
    void handleAUTH(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data);
    void handleREGISTER(const std::string &cmd, const nlohmann::json &args, const nlohmann::json &data);
    

    nlohmann::json makeOkReply(const std::string &msg, const nlohmann::json &data = nlohmann::json::object());
    nlohmann::json makeFailReply(uint32_t code, const std::string &msg);

    void sendOkReply(const std::string &msg, const nlohmann::json &data = nlohmann::json::object());
    void sendFailReply(uint32_t code, const std::string &msg);

    enum class mode {NOT_AUTHENTICATED, PUBLIC, PRIVATE};

    inline mode getMode() const {return _mode;}
    inline std::string getUsername() const {return _username;}
    inline std::filesystem::path getUWD() const {return _uwd;}

private:
    mode _mode;
    std::string _username;
    std::filesystem::path _uwd;

    MiniDriveServer *_server;
    AsyncSocket _cmdSocket;
};