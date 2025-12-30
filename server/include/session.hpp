#pragma once
#include <asio.hpp>
#include <string>
#include <queue>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "minidrive/async_socket.hpp"
#include "minidrive/transfer.hpp"
// #include "server.hpp"

class MiniDriveServer;

class Session {
public:
    Session(MiniDriveServer *server, asio::ip::tcp::socket &&cmdSocket);
    ~Session();
    bool isDead() const;
    void start();
    
    void processMessage(const MsgPayload &payload);
    void processData(const MsgPayload &payload);
    
    void handleLIST(const nlohmann::json &args);
    void handleREMOVE(const nlohmann::json &args);
    void handleCD(const nlohmann::json &args);
    void handleMKDIR(const nlohmann::json &args);
    void handleRMDIR(const nlohmann::json &args);
    void handleCOPY(const nlohmann::json &args, bool move);
    
    void handleAUTH(const nlohmann::json &args);
    void handleREGISTER(const nlohmann::json &args);

    void handleUPLOAD(const nlohmann::json &args, const nlohmann::json &data);
    void handleDOWNLOAD(const nlohmann::json &args, const nlohmann::json &data);

    void saveTransfer();
    bool loadTransfer();
    void deleteTransferFile();

    

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

    Transfer _transfer;
};