#pragma once

#include "liteim/base/Status.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace liteim::bench {

enum class ReportFormat {
    Json,
    Markdown,
};

struct BenchmarkOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{9000};
    std::uint32_t connections{10};
    std::size_t message_size{128};
    std::uint32_t send_interval_ms{10};
    std::uint32_t duration_seconds{10};
    ReportFormat format{ReportFormat::Json};
    std::string username_prefix{"bench"};
    std::string password{"bench_secret"};
    bool help{false};
};

// 延迟统计结果
struct LatencySummary {
    std::uint64_t count{0};   // 请求总数
    double average_us{0.0};   // 平均延迟，单位微秒
    std::uint64_t p50_us{0};  // 有50%请求延迟不超过该值，单位微秒
    std::uint64_t p95_us{0};
    std::uint64_t p99_us{0};
};

// 最终压测结果
struct BenchmarkResult {
    BenchmarkOptions options;
    std::uint64_t connection_attempts{0};
    std::uint64_t connection_success{0};
    std::uint64_t request_success{0};
    std::uint64_t error_count{0};
    std::uint64_t elapsed_ms{0};
    double qps{0.0};
    LatencySummary latency;
    double cpu_percent{0.0};
    std::uint64_t memory_kb{0};
};

// 解析命令行参数
Status parseBenchmarkOptions(int argc, const char* const argv[], BenchmarkOptions& options);

// 统计延迟
LatencySummary summarizeLatencies(std::vector<std::chrono::microseconds> latencies);

// 构造指定大小的消息文本
std::string makePayload(std::size_t size);
// 生成压测报告
std::string renderReport(const BenchmarkResult& result, ReportFormat format);
// 生成 help 文本
std::string benchmarkHelpText();
// 执行压测并返回结果
Status runBenchmark(const BenchmarkOptions& options, BenchmarkResult& result);

}  // namespace liteim::bench
