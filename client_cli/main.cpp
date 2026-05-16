#include "ClientCli.hpp"

#include "liteim/base/Status.hpp"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr std::uint16_t kDefaultPort = 9000;
constexpr auto kHeartbeatInterval = std::chrono::seconds(30);

void printUsage(const char* program) {
    std::cout << "usage: " << program << " [--host HOST] [--port PORT]\n";
}

bool parseArgs(int argc, char** argv, std::string& host, std::uint16_t& port, bool& show_help) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            show_help = true;
            return false;
        }
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
            continue;
        }
        if (arg == "--port" && i + 1 < argc) {
            const auto port_text = std::string(argv[++i]);
            char* end = nullptr;
            errno = 0;
            const auto parsed = std::strtoull(port_text.c_str(), &end, 10);
            if (errno != 0 || end == port_text.c_str() || *end != '\0' || parsed == 0 ||
                parsed > 65535) {
                std::cerr << "invalid port: " << port_text << '\n';
                return false;
            }
            port = static_cast<std::uint16_t>(parsed);
            continue;
        }
        std::cerr << "unknown argument: " << arg << '\n';
        printUsage(argv[0]);
        return false;
    }
    return true;
}

void printWithLock(std::mutex& mutex, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex);
    std::cout << text << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    std::uint16_t port = kDefaultPort;
    bool show_help = false;
    if (!parseArgs(argc, argv, host, port, show_help)) {
        if (show_help) {
            return 0;
        }
        return 1;
    }

    liteim::cli::ProtocolClient client;
    const auto connect_status = client.connectTo(host, port);
    if (!connect_status.isOk()) {
        std::cerr << "connect failed: " << connect_status.message() << '\n';
        return 1;
    }

    std::atomic<bool> running{true};
    std::atomic<std::uint64_t> next_seq{1};
    std::mutex output_mutex;

    printWithLock(output_mutex, "connected to " + host + ":" + std::to_string(port));
    printWithLock(output_mutex, liteim::cli::helpText());

    std::thread receiver([&]() {
        while (running.load()) {
            liteim::Packet packet;
            const auto status = client.readPacket(packet);
            if (!status.isOk()) {
                if (running.load()) {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    std::cerr << "receive failed: " << status.message() << '\n';
                }
                running.store(false);
                break;
            }
            printWithLock(output_mutex, "< " + liteim::cli::describePacket(packet));
        }
    });

    std::thread heartbeat([&]() {
        while (running.load()) {
            for (int i = 0; i < kHeartbeatInterval.count() && running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!running.load()) {
                break;
            }
            liteim::Packet packet;
            const auto build_status =
                liteim::cli::buildPacketFromLine("heartbeat", next_seq.fetch_add(1), packet);
            if (!build_status.isOk()) {
                continue;
            }
            const auto send_status = client.sendPacket(packet);
            if (!send_status.isOk()) {
                std::lock_guard<std::mutex> lock(output_mutex);
                std::cerr << "heartbeat failed: " << send_status.message() << '\n';
                running.store(false);
                break;
            }
        }
    });

    const bool interactive = ::isatty(STDIN_FILENO) == 1;
    std::string line;
    while (running.load()) {
        if (interactive) {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::cout << "> " << std::flush;
        }
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line == "quit" || line == "exit") {
            break;
        }
        if (line == "help") {
            printWithLock(output_mutex, liteim::cli::helpText());
            continue;
        }

        liteim::Packet packet;
        const auto build_status =
            liteim::cli::buildPacketFromLine(line, next_seq.fetch_add(1), packet);
        if (!build_status.isOk()) {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::cerr << "command error: " << build_status.message() << '\n';
            continue;
        }

        const auto send_status = client.sendPacket(packet);
        if (!send_status.isOk()) {
            std::lock_guard<std::mutex> lock(output_mutex);
            std::cerr << "send failed: " << send_status.message() << '\n';
            running.store(false);
            break;
        }
    }

    running.store(false);
    client.close();
    if (heartbeat.joinable()) {
        heartbeat.join();
    }
    if (receiver.joinable()) {
        receiver.join();
    }
    return 0;
}
