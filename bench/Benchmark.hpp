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

struct LatencySummary {
    std::uint64_t count{0};
    double average_us{0.0};
    std::uint64_t p50_us{0};
    std::uint64_t p95_us{0};
    std::uint64_t p99_us{0};
};

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

Status parseBenchmarkOptions(int argc, const char* const argv[], BenchmarkOptions& options);

LatencySummary summarizeLatencies(std::vector<std::chrono::microseconds> latencies);

std::string makePayload(std::size_t size);
std::string renderReport(const BenchmarkResult& result, ReportFormat format);
std::string benchmarkHelpText();

Status runBenchmark(const BenchmarkOptions& options, BenchmarkResult& result);

}  // namespace liteim::bench
