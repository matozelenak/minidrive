#include <iostream>
#include <string>
#include <optional>
#include <sstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <nlohmann/json.hpp>

#include "minidrive/version.hpp"
#include "minidrive/error_codes.hpp"
#include "minidrive/async_socket.hpp"

using json = nlohmann::json;
using asio::ip::tcp;

struct Args {
    std::string username{};
    std::string host{};
    std::string port{};
};

enum class State {AUTH, REG, COMMAND};


State state;
std::atomic<bool> _running;
std::mutex _mutex;
std::condition_variable _cv;
std::atomic<bool> _waitForReply, _replyArrived;
json _lastReply;

asio::io_context *_io;

void stop() {
    _running = false;
    _io->stop();
}

void sendMessage(AsyncSocket &client, const std::string &cmd, json &&args) {
    json msg;
    msg["cmd"] = cmd;
    msg["args"] = std::move(args);
    client.sendMessage(msg.dump());
}

void processMessage(const MsgPayload &payload) {
    json data;
    try {
        data = json::parse(payload.begin(), payload.end());
    } catch (const json::parse_error &e) {
        spdlog::error("json parse failed: {}", e.what());
        return;
    }
    
    spdlog::debug("msg: '{}'", data.dump());

    std::lock_guard lock(_mutex);
    if (!_waitForReply) {
        spdlog::warn("unwanted message arrived");
        return;
    }

    _replyArrived = true;
    _waitForReply = false;
    _lastReply = std::move(data);
    _cv.notify_one();
}

// bool handleAuth(AsyncSocket &client, const Args &args) {
//     if (args.username.empty()) { // public mode
//         spdlog::warn("operating in public mode - files are visible to everyone");

//     }
// }

void userThread(AsyncSocket &client, const Args &args) {
    while (_running) {

        std::unique_lock lock(_mutex);
        if (_waitForReply) {
            _cv.wait(lock, [] {return _replyArrived.operator bool();});
        }

        if (_replyArrived) {
            // react to reply
            _replyArrived = false;

            std::string status = _lastReply["status"];
            int code = _lastReply["code"];
            std::string message = _lastReply["message"];

            if (code == 0) {
                std::cout << "OK\n";
                std::cout << message << std::endl;
            } else {
                std::cout << "ERROR: " << code << '\n';
                std::cout << "primary message\n";
                std::cout << message << std::endl;
            }

            switch(state) {
            case State::AUTH:
            {
                if (code == 0) {
                    spdlog::info("auth success");
                    state = State::COMMAND;
                } else if (code == minidrive::error::USER_NOT_FOUND.code()) {
                    spdlog::info("user not found");
                    state = State::REG;   
                }
            }
                break;
            
            
            case State::COMMAND:
                spdlog::info("received this shit: {}", _lastReply.dump());
                break;

            
            }
            

        }
        else {
            // just proccess commands
            switch(state) {
            case State::AUTH:
            {
                json msg;
                msg["cmd"] = "AUTH";
                msg["mode"] = "private";
                msg["args"] = { {"username", args.username}, {"password", "1234"} };
                client.sendMessage(msg.dump());
                _waitForReply = true;
            }
                break;
            case State::REG:
            {
                std::cout << "user not found, register?" << std::endl;
                state = State::COMMAND;
            }
                break;

            case State::COMMAND:
            {
                std::cout << "> ";

                std::string line;
                std::getline(std::cin, line);
                    
                std::stringstream ss;
                ss << line;
        
                std::string cmd;
                ss >> cmd;
                spdlog::debug("requested cmd: {}", cmd);
                if (cmd == "LIST") {
                    json msg;
                    msg["cmd"] = "LIST";
                    msg["args"] = { {"path", ""}};
                    client.sendMessage(msg.dump());
                    _waitForReply = true;
                }
                else if (cmd == "EXIT") {
                    stop();
                }
                else {
                    spdlog::warn("unknown command");
                }

            }
                break;
            
            }
        }




    }
}

static std::optional<Args> parseArgs(const std::string& input) {
    auto colon = input.rfind(':');
    if (colon == std::string::npos) return std::optional<Args>();

    std::string username_host = input.substr(0, colon);
    std::string port_str = input.substr(colon + 1);
    if (username_host.empty() || port_str.empty()) return std::optional<Args>();

    Args result;
    result.port = std::move(port_str);

    auto at = username_host.rfind('@');
    if (at == std::string::npos) {
        result.host = std::move(username_host);
        return std::optional<Args>(std::move(result));
    }
    
    std::string username = username_host.substr(0, at);
    std::string host = username_host.substr(at + 1);
    if (username.empty() || host.empty()) return std::optional<Args>();

    result.host = std::move(host);
    result.username = std::move(username);
    return std::optional<Args>(std::move(result));
}

int main(int argc, char* argv[]) {
    spdlog::set_default_logger(spdlog::stdout_color_mt("my_logger"));
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%H:%M:%S] [%^%L%$] %v");

    std::cout << "[cmd]";
    for (int i = 0; i < argc; ++i) {
        std::cout << " \"" << argv[i] << '"';
    }
    std::cout << std::endl;

    if (argc < 2) {
        spdlog::error("Usage: {} <host>:<port>", argv[0]);
        return 1;
    }

    auto args = parseArgs(argv[1]);
    if (!args) {
        spdlog::error("Invalid endpoint format: {}", argv[1]);
        return 1;
    }

    spdlog::info("MiniDrive client (version {})", minidrive::version());
    spdlog::info("Connecting to {}:{}", args->host, args->port);

    asio::io_context io;
    tcp::resolver resolver(io);
    tcp::resolver::results_type results;
    try {
        results = resolver.resolve(args->host, args->port);
    } catch (const asio::system_error &e) {
        spdlog::error("failed to resolve endpoint: {}", e.what());
        return 1;
    }
    tcp::socket socket(io);
    try {
        asio::connect(socket, results);
    } catch (const asio::system_error &e) {
        spdlog::error("failed to connect: {}", e.what());
        return 1;
    }
    spdlog::info("connected");
    _running = true;
    _io = &io;

    AsyncSocket client(std::move(socket));
    client.start(
        [&](data_type type, std::shared_ptr<MsgPayload> payload) {
            switch(type) {
            case data_type::COMMAND:
                spdlog::debug("msg type: {}, payload length: {}", static_cast<uint32_t>(type), payload->size());
                processMessage(*payload);
                break;
            default:
                break;
            }
        },
        [&](const asio::error_code &ec) {
            if (ec == asio::error::eof) {
                spdlog::info("server disconnected");
            } else {
                spdlog::error("{}", ec.message());
            }
            stop();
        },
        [&](const asio::error_code &ec) {
            if (ec == asio::error::eof) {
                spdlog::info("server disconnected");
            } else {
                spdlog::error("{}", ec.message());
            }
            stop();
        }
    );


    std::thread th([&]() {userThread(client, *args);});

    io.run();
    th.join();
    spdlog::info("client exited");
    return 0;
}
