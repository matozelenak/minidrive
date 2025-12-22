#pragma once
#include <asio.hpp>
#include <string>
#include <queue>
#include <mutex>
#include <memory>
#include <functional>

enum class data_type {COMMAND = 0, DATA};

struct MsgHeader {
    MsgHeader();
    void setType(data_type t);
    void setLen(uint32_t len);
    data_type getType();
    uint32_t getLen();
    inline std::vector<uint8_t>& getBuffer() {return buffer;}

    private:
        std::vector<uint8_t> buffer;
};

using MsgPayload = std::vector<uint8_t>;

using MessageHandler = std::function<void(data_type, std::shared_ptr<MsgPayload>)>;
using ReadErrorHandler = std::function<void(const asio::error_code&)>;
using WriteErrorHandler = std::function<void(const asio::error_code&)>;

class AsyncSocket {
public:
    AsyncSocket(asio::ip::tcp::socket &&socket);
    ~AsyncSocket();
    bool isDead() const;
    void start(MessageHandler msgH, ReadErrorHandler readErrH, WriteErrorHandler writeErrH);
    void doWrite();
    void sendMessage(data_type type, MsgPayload &&payload);
    void sendMessage(const std::string &msg);

    inline const asio::ip::tcp::socket& getSocket() const {return _socket;}
    
private:
    void _readHeader();
    void _readPayload(data_type type, uint32_t payloadLength);


    bool _isDead;
    asio::ip::tcp::socket _socket;

    std::queue<MsgPayload> _writeQueue;
    std::mutex _writeQueueMutex;
    std::atomic<bool> _writeBusy;

    MessageHandler _messageHandler;
    ReadErrorHandler _readErrorHandler;
    WriteErrorHandler _writeErrorHandler;
};
