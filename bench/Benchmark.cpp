#include "Benchmark.hpp"

#include "ClientCli.hpp"
#include "liteim/base/ErrorCode.hpp"
#include "liteim/protocol/MessageLimits.hpp"
#include "liteim/protocol/MessageType.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace liteim::bench {
namespace {

struct ResourceSample {
    double cpu_seconds{0.0};
    std::uint64_t memory_kb{0};
};

Status invalidOption(const std::string& message) {
    return Status::error(ErrorCode::InvalidArgument, message);
}

Status parseUnsigned(const std::string& value, const char* field, std::uint64_t& output) {
    if (value.empty() || value.front() == '-') {
        return invalidOption(std::string(field) + " must be an unsigned integer");
    }

    char* end = nullptr;
    errno = 0;
    const auto parsed = std::strtoull(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0') {
        return invalidOption(std::string(field) + " must be an unsigned integer");
    }
    output = static_cast<std::uint64_t>(parsed);
    return Status::ok();
}

Status requireValue(int argc, const char* const argv[], int& index, const char* option,
                    std::string& value) {
    if (index + 1 >= argc) {
        return invalidOption(std::string(option) + " requires a value");
    }
    ++index;
    value = argv[index];
    return Status::ok();
}

Status validateOptions(const BenchmarkOptions& options) {
    if (options.port == 0) {
        return invalidOption("port must be positive");
    }
    if (options.connections < 2) {
        return invalidOption("connections must be at least 2 for private-message benchmark");
    }
    if (options.message_size == 0) {
        return invalidOption("message size must be positive");
    }
    if (options.message_size > liteim::kMaxMessageTextBytes) {
        return invalidOption("message size must be <= 8192 bytes");
    }
    if (options.duration_seconds == 0) {
        return invalidOption("duration seconds must be positive");
    }
    if (options.username_prefix.empty()) {
        return invalidOption("username prefix must not be empty");
    }
    if (options.username_prefix.size() > 24U) {
        return invalidOption("username prefix must be <= 24 characters");
    }
    if (options.password.empty()) {
        return invalidOption("password must not be empty");
    }
    return Status::ok();
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream output;
    for (const auto ch : value) {
        switch (ch) {
        case '\\':
            output << "\\\\";
            break;
        case '"':
            output << "\\\"";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            output << ch;
            break;
        }
    }
    return output.str();
}

std::string makeRunId() {
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream stream;
    stream << static_cast<long long>(::getpid()) << '_' << now;
    return stream.str();
}

std::string makeUsername(const BenchmarkOptions& options, const std::string& run_id,
                         const char* role, std::uint32_t index) {
    std::ostringstream stream;
    stream << options.username_prefix << '_' << role << '_' << index << '_' << run_id;
    return stream.str();
}

Status makeRegisterPacket(const std::string& username, const std::string& password,
                          std::uint64_t seq_id, Packet& packet) {
    packet = Packet{};
    packet.header.msg_type = MessageType::RegisterRequest;
    packet.header.seq_id = seq_id;

    auto status = appendString(TlvType::Username, username, packet.body);
    if (!status.isOk()) {
        return status;
    }
    status = appendString(TlvType::Password, password, packet.body);
    if (!status.isOk()) {
        return status;
    }
    return appendString(TlvType::Nickname, username, packet.body);
}

Status makeLoginPacket(const std::string& username, const std::string& password,
                       std::uint64_t seq_id, Packet& packet) {
    packet = Packet{};
    packet.header.msg_type = MessageType::LoginRequest;
    packet.header.seq_id = seq_id;

    auto status = appendString(TlvType::Username, username, packet.body);
    if (!status.isOk()) {
        return status;
    }
    return appendString(TlvType::Password, password, packet.body);
}

Status makePrivatePacket(std::uint64_t receiver_id, const std::string& payload,
                         std::uint64_t seq_id, Packet& packet) {
    packet = Packet{};
    packet.header.msg_type = MessageType::PrivateMessageRequest;
    packet.header.seq_id = seq_id;

    auto status = appendUint64(TlvType::ReceiverId, receiver_id, packet.body);
    if (!status.isOk()) {
        return status;
    }
    return appendString(TlvType::MessageText, payload, packet.body);
}

Status makeOneIdPacket(MessageType msg_type, TlvType tlv_type, std::uint64_t value,
                       std::uint64_t seq_id, Packet& packet) {
    packet = Packet{};
    packet.header.msg_type = msg_type;
    packet.header.seq_id = seq_id;
    return appendUint64(tlv_type, value, packet.body);
}

Status waitForResponse(cli::ProtocolClient& client, std::uint64_t seq_id, MessageType expected_type,
                       Packet& response) {
    for (;;) {
        Packet packet;
        auto status = client.readPacket(packet);
        if (!status.isOk()) {
            return status;
        }
        if (packet.header.seq_id != seq_id) {
            continue;
        }
        if (packet.header.msg_type == MessageType::ErrorResponse) {
            return Status::error(ErrorCode::InternalError, cli::describePacket(packet));
        }
        if (packet.header.msg_type != expected_type) {
            return Status::error(ErrorCode::InternalError,
                                 std::string("unexpected response type: ") +
                                     toString(packet.header.msg_type));
        }
        response = std::move(packet);
        return Status::ok();
    }
}

Status request(cli::ProtocolClient& client, const Packet& packet, MessageType expected_type,
               Packet& response) {
    auto status = client.sendPacket(packet);
    if (!status.isOk()) {
        return status;
    }
    return waitForResponse(client, packet.header.seq_id, expected_type, response);
}

Status extractUserId(const Packet& packet, std::uint64_t& user_id) {
    TlvMap fields;
    auto status = parseTlvMap(packet.body, fields);
    if (!status.isOk()) {
        return status;
    }
    return getUint64(fields, TlvType::UserId, user_id);
}

Status establishAcceptedFriendship(cli::ProtocolClient& requester,
                                   cli::ProtocolClient& target,
                                   std::uint64_t requester_id,
                                   std::uint64_t target_user_id,
                                   std::uint64_t& next_seq) {
    Packet packet;
    auto status = makeOneIdPacket(MessageType::AddFriendRequest, TlvType::TargetUserId,
                                  target_user_id, next_seq++, packet);
    if (!status.isOk()) {
        return status;
    }
    Packet response;
    status = request(requester, packet, MessageType::AddFriendResponse, response);
    if (!status.isOk()) {
        return status;
    }

    status = makeOneIdPacket(MessageType::AcceptFriendRequest, TlvType::TargetUserId,
                             requester_id, next_seq++, packet);
    if (!status.isOk()) {
        return status;
    }
    return request(target, packet, MessageType::AcceptFriendResponse, response);
}

Status registerAndLogin(cli::ProtocolClient& client, const BenchmarkOptions& options,
                        const std::string& username, std::uint64_t& next_seq,
                        std::uint64_t& user_id) {
    Packet packet;
    auto status = makeRegisterPacket(username, options.password, next_seq++, packet);
    if (!status.isOk()) {
        return status;
    }

    Packet response;
    status = request(client, packet, MessageType::RegisterResponse, response);
    if (!status.isOk()) {
        return status;
    }
    status = extractUserId(response, user_id);
    if (!status.isOk()) {
        return status;
    }

    status = makeLoginPacket(username, options.password, next_seq++, packet);
    if (!status.isOk()) {
        return status;
    }
    return request(client, packet, MessageType::LoginResponse, response);
}

// 采样当前进程的 CPU 和内存使用情况，基于 Linux 的 /proc/self/stat 和 /proc/self/status 实现
ResourceSample sampleProcessResources() {
    ResourceSample sample;

    // 读取 VmRSS 行获取内存使用量，单位 KB
    std::ifstream status_file("/proc/self/status");
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream stream(line.substr(6));
            stream >> sample.memory_kb;
            break;
        }
    }

    // 读取 utime 和 stime 字段获取 CPU 时间
    std::ifstream stat_file("/proc/self/stat");
    std::string stat_line;
    if (std::getline(stat_file, stat_line)) {
        const auto close_paren = stat_line.rfind(')');
        if (close_paren != std::string::npos && close_paren + 2 < stat_line.size()) {
            std::istringstream stream(stat_line.substr(close_paren + 2));
            std::vector<std::string> fields;
            std::string field;
            while (stream >> field) {
                fields.push_back(field);
            }
            if (fields.size() > 12) {
                const auto utime = std::strtoull(fields[11].c_str(), nullptr, 10);
                const auto stime = std::strtoull(fields[12].c_str(), nullptr, 10);
                const auto ticks_per_second = static_cast<double>(::sysconf(_SC_CLK_TCK));
                if (ticks_per_second > 0.0) {
                    sample.cpu_seconds = static_cast<double>(utime + stime) / ticks_per_second;
                }
            }
        }
    }

    return sample;
}

}  // namespace

// 解析命令行参数
Status parseBenchmarkOptions(int argc, const char* const argv[], BenchmarkOptions& options) {
    options = BenchmarkOptions{};

    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help" || option == "-h") {
            options.help = true;
            return Status::ok();
        }

        std::string value;
        auto status = requireValue(argc, argv, index, option.c_str(), value);
        if (!status.isOk()) {
            return status;
        }

        std::uint64_t parsed = 0;
        if (option == "--host") {
            options.host = value;
        } else if (option == "--port") {
            status = parseUnsigned(value, "port", parsed);
            if (!status.isOk()) {
                return status;
            }
            if (parsed == 0 || parsed > std::numeric_limits<std::uint16_t>::max()) {
                return invalidOption("port must be in 1..65535");
            }
            options.port = static_cast<std::uint16_t>(parsed);
        } else if (option == "--connections") {
            status = parseUnsigned(value, "connections", parsed);
            if (!status.isOk()) {
                return status;
            }
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                return invalidOption("connections is too large");
            }
            options.connections = static_cast<std::uint32_t>(parsed);
        } else if (option == "--message-size") {
            status = parseUnsigned(value, "message-size", parsed);
            if (!status.isOk()) {
                return status;
            }
            options.message_size = static_cast<std::size_t>(parsed);
        } else if (option == "--interval-ms") {
            status = parseUnsigned(value, "interval-ms", parsed);
            if (!status.isOk()) {
                return status;
            }
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                return invalidOption("interval-ms is too large");
            }
            options.send_interval_ms = static_cast<std::uint32_t>(parsed);
        } else if (option == "--duration-sec") {
            status = parseUnsigned(value, "duration-sec", parsed);
            if (!status.isOk()) {
                return status;
            }
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                return invalidOption("duration-sec is too large");
            }
            options.duration_seconds = static_cast<std::uint32_t>(parsed);
        } else if (option == "--format") {
            if (value == "json") {
                options.format = ReportFormat::Json;
            } else if (value == "markdown") {
                options.format = ReportFormat::Markdown;
            } else {
                return invalidOption("format must be json or markdown");
            }
        } else if (option == "--username-prefix") {
            options.username_prefix = value;
        } else if (option == "--password") {
            options.password = value;
        } else {
            return invalidOption("unknown option: " + option);
        }
    }

    return validateOptions(options);
}

// 统计延迟
LatencySummary summarizeLatencies(std::vector<std::chrono::microseconds> latencies) {
    LatencySummary summary;
    summary.count = static_cast<std::uint64_t>(latencies.size());
    if (latencies.empty()) {
        return summary;
    }

    std::sort(latencies.begin(), latencies.end());
    std::uint64_t total = 0;
    for (const auto latency : latencies) {
        total += static_cast<std::uint64_t>(latency.count());
    }
    summary.average_us = static_cast<double>(total) / static_cast<double>(latencies.size());

    const auto percentile = [&latencies](double quantile) {
        const auto rank =
            static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(latencies.size())));
        const auto index = std::max<std::size_t>(1, rank) - 1U;
        return static_cast<std::uint64_t>(
            latencies[std::min(index, latencies.size() - 1U)].count());
    };

    summary.p50_us = percentile(0.50);
    summary.p95_us = percentile(0.95);
    summary.p99_us = percentile(0.99);
    return summary;
}

// 生成指定大小的消息内容
std::string makePayload(std::size_t size) {
    static constexpr const char* kSeed = "liteim-load-";
    std::string payload;
    payload.reserve(size);
    while (payload.size() < size) {
        payload.push_back(kSeed[payload.size() % 12U]);
    }
    return payload;
}

// 生成压测报告
std::string renderReport(const BenchmarkResult& result, ReportFormat format) {
    std::ostringstream stream;
    stream << std::setprecision(6);

    if (format == ReportFormat::Markdown) {
        stream << "| Metric | Value |\n";
        stream << "| --- | --- |\n";
        stream << "| host | `" << result.options.host << ':' << result.options.port << "` |\n";
        stream << "| connections | " << result.options.connections << " |\n";
        stream << "| message_size_bytes | " << result.options.message_size << " |\n";
        stream << "| connection_success | " << result.connection_success << " / "
               << result.connection_attempts << " |\n";
        stream << "| request_success | " << result.request_success << " |\n";
        stream << "| errors | " << result.error_count << " |\n";
        stream << "| qps | " << result.qps << " |\n";
        stream << "| avg_latency_us | " << result.latency.average_us << " |\n";
        stream << "| p50_us | " << result.latency.p50_us << " |\n";
        stream << "| p95_us | " << result.latency.p95_us << " |\n";
        stream << "| p99_us | " << result.latency.p99_us << " |\n";
        stream << "| cpu_percent | " << result.cpu_percent << " |\n";
        stream << "| memory_kb | " << result.memory_kb << " |\n";
        return stream.str();
    }

    stream << "{\n";
    stream << "  \"host\": \"" << jsonEscape(result.options.host) << "\",\n";
    stream << "  \"port\": " << result.options.port << ",\n";
    stream << "  \"connections\": " << result.options.connections << ",\n";
    stream << "  \"message_size_bytes\": " << result.options.message_size << ",\n";
    stream << "  \"send_interval_ms\": " << result.options.send_interval_ms << ",\n";
    stream << "  \"duration_seconds\": " << result.options.duration_seconds << ",\n";
    stream << "  \"connection_attempts\": " << result.connection_attempts << ",\n";
    stream << "  \"connection_success\": " << result.connection_success << ",\n";
    stream << "  \"request_success\": " << result.request_success << ",\n";
    stream << "  \"error_count\": " << result.error_count << ",\n";
    stream << "  \"elapsed_ms\": " << result.elapsed_ms << ",\n";
    stream << "  \"qps\": " << result.qps << ",\n";
    stream << "  \"average_latency_us\": " << result.latency.average_us << ",\n";
    stream << "  \"p50_us\": " << result.latency.p50_us << ",\n";
    stream << "  \"p95_us\": " << result.latency.p95_us << ",\n";
    stream << "  \"p99_us\": " << result.latency.p99_us << ",\n";
    stream << "  \"cpu_percent\": " << result.cpu_percent << ",\n";
    stream << "  \"memory_kb\": " << result.memory_kb << "\n";
    stream << "}";
    return stream.str();
}

std::string benchmarkHelpText() {
    return R"(usage: liteim_bench [options]

options:
  --host <host>                 server host, default 127.0.0.1
  --port <port>                 server port, default 9000
  --connections <n>             total long connections, minimum 2, default 10
  --message-size <bytes>        private message text size, default 128
  --interval-ms <ms>            send interval per sender, default 10
  --duration-sec <seconds>      measurement duration, default 10
  --format <json|markdown>      report format, default json
  --username-prefix <prefix>    generated account prefix, default bench
  --password <password>         generated account password, default bench_secret
  --help                        show this help

The benchmark uses one receiver connection and connections-1 sender connections.
All clients register unique users, log in, establish accepted friendship with
the receiver, and send ordinary PrivateMessageRequest packets.)";
}

// 执行压测并返回结果
Status runBenchmark(const BenchmarkOptions& options, BenchmarkResult& result) {
    auto status = validateOptions(options);
    if (!status.isOk()) {
        return status;
    }
    // 初始化结果
    result = BenchmarkResult{};
    result.options = options;
    result.connection_attempts = options.connections;
    // 生成运行 ID，用于构造唯一用户名
    const auto run_id = makeRunId();
    const auto payload = makePayload(options.message_size);
    std::uint64_t next_setup_seq = 1;

    // 建立一个接收连接，负责接收所有发件人的消息，确保消息能够正确送达并计算延迟
    cli::ProtocolClient receiver;
    status = receiver.connectTo(options.host, options.port);
    if (!status.isOk()) {
        result.error_count = 1;
        return status;
    }

    std::uint64_t receiver_id = 0;
    status = registerAndLogin(receiver, options, makeUsername(options, run_id, "receiver", 0),
                              next_setup_seq, receiver_id);
    if (!status.isOk()) {
        result.error_count = 1;
        return status;
    }
    ++result.connection_success;

    // 多个用户同时给同一个用户发私聊消息
    std::vector<std::unique_ptr<cli::ProtocolClient>> senders;
    senders.reserve(options.connections - 1U);
    for (std::uint32_t index = 1; index < options.connections; ++index) {
        auto client = std::make_unique<cli::ProtocolClient>();
        status = client->connectTo(options.host, options.port);
        if (!status.isOk()) {
            ++result.error_count;
            continue;
        }

        std::uint64_t user_id = 0;
        status = registerAndLogin(*client, options, makeUsername(options, run_id, "sender", index),
                                  next_setup_seq, user_id);
        if (!status.isOk()) {
            ++result.error_count;
            continue;
        }

        status = establishAcceptedFriendship(*client, receiver, user_id, receiver_id,
                                             next_setup_seq);
        if (!status.isOk()) {
            ++result.error_count;
            continue;
        }

        ++result.connection_success;
        senders.push_back(std::move(client));
    }

    if (senders.empty()) {
        receiver.close();
        return Status::error(ErrorCode::IoError, "no sender connection is available");
    }

    // 启动 receiver_thread 不断读取消息直到压测结束，避免服务器端发送的消息导致发送线程阻塞无法继续压测
    std::atomic<bool> drain_receiver{true};
    std::thread receiver_thread([&receiver, &drain_receiver]() {
        while (drain_receiver.load()) {
            Packet packet;
            const auto read_status = receiver.readPacket(packet);
            if (!read_status.isOk()) {
                break;
            }
        }
    });

    std::mutex latency_mutex;
    std::vector<std::chrono::microseconds> latencies;
    std::atomic<std::uint64_t> request_success{0};
    std::atomic<std::uint64_t> request_errors{0};
    std::atomic<std::uint64_t> next_seq{next_setup_seq};

    const auto resource_begin = sampleProcessResources();
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(options.duration_seconds);

    std::vector<std::thread> threads;
    threads.reserve(senders.size());
    for (auto& sender : senders) {
        threads.emplace_back([&, client = sender.get()]() {
            std::vector<std::chrono::microseconds> local_latencies;
            while (std::chrono::steady_clock::now() < deadline) {
                const auto seq_id = next_seq.fetch_add(1);
                Packet packet;
                auto local_status = makePrivatePacket(receiver_id, payload, seq_id, packet);
                if (!local_status.isOk()) {
                    ++request_errors;
                    break;
                }

                Packet response;
                const auto request_start = std::chrono::steady_clock::now();
                local_status =
                    request(*client, packet, MessageType::PrivateMessageResponse, response);
                const auto request_end = std::chrono::steady_clock::now();
                if (!local_status.isOk()) {
                    ++request_errors;
                    break;
                }

                ++request_success;
                local_latencies.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
                    request_end - request_start));
                if (options.send_interval_ms > 0) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(options.send_interval_ms));
                }
            }

            std::lock_guard<std::mutex> lock(latency_mutex);
            latencies.insert(latencies.end(), local_latencies.begin(), local_latencies.end());
        });
    }

    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const auto resource_end = sampleProcessResources();

    for (auto& sender : senders) {
        sender->close();
    }
    drain_receiver.store(false);
    receiver.close();
    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }

    // 统计结果
    result.request_success = request_success.load();
    result.error_count += request_errors.load();
    result.elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    const auto elapsed_seconds = std::max(0.001, static_cast<double>(result.elapsed_ms) / 1000.0);
    result.qps = static_cast<double>(result.request_success) / elapsed_seconds;
    result.latency = summarizeLatencies(std::move(latencies));
    result.memory_kb = resource_end.memory_kb;
    result.cpu_percent =
        ((resource_end.cpu_seconds - resource_begin.cpu_seconds) / elapsed_seconds) * 100.0;

    return Status::ok();
}

}  // namespace liteim::bench
