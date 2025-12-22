#include "minidrive/async_socket.hpp"
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <limits>

using asio::ip::tcp;

MsgHeader::MsgHeader() : buffer(5) {}

// void MsgHeader::parse() {
//     type = static_cast<data_type>(buffer[0]);
//     len = buffer[1] << 0  |
//           buffer[2] << 8  |
//           buffer[3] << 16 |
//           buffer[4] << 24;
// }

// void MsgHeader::store() {
//     buffer[0] = static_cast<uint8_t>(type);
//     buffer[1] = (len >> 0)  & 0xff;
//     buffer[2] = (len >> 8)  & 0xff;
//     buffer[3] = (len >> 16) & 0xff;
//     buffer[4] = (len >> 24) & 0xff;
// }

void MsgHeader::setType(data_type t) {
    buffer[0] = static_cast<uint8_t>(t);
}
void MsgHeader::setLen(uint32_t len) {
    buffer[1] = (len >> 0)  & 0xff;
    buffer[2] = (len >> 8)  & 0xff;
    buffer[3] = (len >> 16) & 0xff;
    buffer[4] = (len >> 24) & 0xff;
}
data_type MsgHeader::getType() {
    return static_cast<data_type>(buffer[0]);
}
uint32_t MsgHeader::getLen() {
    return buffer[1] << 0  | buffer[2] << 8 |
           buffer[3] << 16 | buffer[4] << 24;
}


AsyncSocket::AsyncSocket(tcp::socket &&socket)
    :_isDead(false), _socket(std::move(socket)) {
    
}

AsyncSocket::~AsyncSocket() {
    spdlog::debug("~AsyncSocket()");
}

bool AsyncSocket::isDead() const {
    return _isDead;
}

void AsyncSocket::start(MessageHandler msgH, ReadErrorHandler readErrH, WriteErrorHandler writeErrH) {
    _messageHandler = msgH;
    _readErrorHandler = readErrH;
    _writeErrorHandler = writeErrH;
    _readHeader();
}

void AsyncSocket::_readHeader() {
    auto header = std::make_shared<MsgHeader>();
    asio::async_read(
        _socket,
        asio::buffer(header->getBuffer()),
        [this, header](const asio::error_code &ec, size_t) {
            if (!ec) {
                _readPayload(header->getType(), header->getLen());
            }
            else if (ec == asio::error::eof) {
                _readErrorHandler(ec);
                _isDead = true;
            }
            else {
                _readErrorHandler(ec);
                _isDead = true;
            }
        }
    );
}

void AsyncSocket::_readPayload(data_type type, uint32_t payloadLength) {
    auto payload = std::make_shared<MsgPayload>(payloadLength);
    asio::async_read(
        _socket,
        asio::buffer(*payload),
        [this, type, payload](const asio::error_code &ec, size_t) {
            if (!ec) {
                _messageHandler(type, payload);
                _readHeader();
            }
            else if (ec == asio::error::eof) {
                _readErrorHandler(ec);
                _isDead = true;
            }
            else {
                _readErrorHandler(ec);
                _isDead = true;
            }
        }
    );
}


void AsyncSocket::doWrite() {
    _writeQueueMutex.lock();
    if (_writeBusy) {
        _writeQueueMutex.unlock();
        return;
    }
    if (_writeQueue.empty()) {
        _writeQueueMutex.unlock();
        return;
    }
    auto payload = std::make_shared<MsgPayload>(std::move(_writeQueue.front()));
    _writeQueue.pop();
    _writeBusy = true;
    _writeQueueMutex.unlock();

    asio::async_write(
        _socket,
        asio::buffer(*payload),
        [this, payload](const asio::error_code &ec, size_t) {
            if (!ec) {
                _writeBusy = false;
                doWrite();
            }
            else {
                _writeErrorHandler(ec);
                _isDead = true;
            }
        }
    );
}

void AsyncSocket::sendMessage(data_type type, MsgPayload &&payload) {
    if (payload.size() > std::numeric_limits<uint32_t>::max()) {
        spdlog::error("sendMessage(): payload too large! ({}B)", payload.size());
        return;
    }

    MsgHeader header;
    header.setType(type);
    header.setLen(payload.size());
    payload.reserve(payload.size() + header.getBuffer().size());
    payload.insert(payload.begin(), header.getBuffer().begin(), header.getBuffer().end());

    {
        std::lock_guard lock(_writeQueueMutex);
        _writeQueue.push(std::move(payload));
    }
    doWrite();
}

void AsyncSocket::sendMessage(const std::string &msg) {
    MsgPayload p(msg.begin(), msg.end());
    sendMessage(data_type::COMMAND, std::move(p));
}