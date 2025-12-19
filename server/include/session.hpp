#pragma once
#include <asio.hpp>
#include <string>
#include <queue>
#include <mutex>

// constexpr size_t READBUFFER_SIZE = 1024;

class MiniDriveServer;

class Session {
public:
    Session(MiniDriveServer &server, asio::ip::tcp::socket &&socket);
    bool isDead() const;
    void doRead();
    void doWrite();
    // void write(std::shared_ptr<std::string> msg);
    void processMessage(const std::string &msg);
    void sendReply(std::string &&msg);

private:
    
    bool _isDead;
    MiniDriveServer &_server;
    asio::ip::tcp::socket _socket;
    asio::streambuf _readBuffer;
    std::string _readData;

    std::queue<std::string> _writeQueue;
    std::mutex _writeQueueMutex;
    std::atomic<bool> _writeBusy;
};