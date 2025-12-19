#include <iostream>
#include <string>
#include <cstdint>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <asio.hpp>

#include "minidrive/version.hpp"
#include "server.hpp"

int main(int argc, char* argv[]) {
    spdlog::set_default_logger(spdlog::stdout_color_mt("my_logger"));
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%H:%M:%S %t] [%^%L%$] %v");

    std::cout << "[cmd]";
    for (int i = 0; i < argc; ++i) {
        std::cout << " \"" << argv[i] << '"';
    }
    std::cout << std::endl;

    // parse arguments
    uint16_t port = 9000;
    std::string rootDir = "server_root";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            try {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            } catch (const std::exception &e) {
                spdlog::error(e.what());
                return 1;
            }
            
        }
        else if (arg == "--root" && i + 1 < argc) {
            rootDir = argv[++i];
        }
    }

    asio::io_context io;

    // create the server
    MiniDriveServer server(io, port, rootDir);

    // set up signal handler
    asio::signal_set signals(io, SIGINT, SIGTERM, SIGHUP);
    signals.async_wait([&io, &server](const asio::error_code& ec, int sig) {
        if (!ec) {
            spdlog::info("Received signal {}, shutting down gracefully...", sig);
            io.stop();
            server.stop();
        }
        else {
            spdlog::error("async_wait for signals: {}", ec.message());
        }
    });

    spdlog::info("Starting MiniDrive server (version {}) on port {}", minidrive::version(), port);
    
    // set up thread pool
    const auto n_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    threads.reserve(n_threads);
    for (unsigned int i = 0; i < n_threads; ++i) {
        threads.emplace_back([&io] { io.run(); });
    }
    spdlog::info("Running with {} threads. Press Ctrl+C to stop.", n_threads);

    // run the server
    server.start();

    // wait for all threads to stop
    for (auto& t : threads) t.join();
    spdlog::info("Server exited.");

    return 0;
}
