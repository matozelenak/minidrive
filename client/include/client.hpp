#pragma once

#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <asio.hpp>
#include <nlohmann/json.hpp>

#include "args.hpp"
#include "minidrive/async_socket.hpp"
#include "minidrive/transfer.hpp"

class Client {
public:
    Client(asio::io_context &io, asio::ip::tcp::socket &&socket, Args &&args);

    void run();
    void stop();
    void processMessage(const MsgPayload &payload);
    void userThread();
    void reactToReply();
    void processUserInput();
    void processCommands();
    void sendMessage(const std::string &cmd, nlohmann::json &&args);
    void sendData(MsgPayload &&payload);
    void printLIST(const nlohmann::json &files);
    void printHelp();

    enum class State {AUTH, REG, COMMAND, UPLOAD, DOWNLOAD};

private:
    std::mutex _mutex;
    std::condition_variable _cv;
    std::atomic<bool> _waitForReply, _replyArrived;
    nlohmann::json _lastReply;
    State _state;
    std::atomic<bool> _running;
    std::string _uwd;

    asio::io_context &_io;
    AsyncSocket _client;
    std::thread _th;
    Args _args;

    Transfer _transfer;
};