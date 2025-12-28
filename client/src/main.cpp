#include <iostream>
#include <string>
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "args.hpp"
#include "client.hpp"
#include "minidrive/version.hpp"

using asio::ip::tcp;


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

    std::optional<Args> args = parseArgs(argv[1]);
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

    Client client(io, std::move(socket), std::move(*args));
    client.run();

    
    spdlog::info("client exited");
    return 0;
}
