#include "Benchmark.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

TEST(BenchmarkOptionsTest, ParsesCustomArguments) {
    const char* argv[] = {"liteim_bench", "--host", "127.0.0.1", "--port", "9000",
                          "--connections", "10", "--message-size", "256",
                          "--interval-ms", "5", "--duration-sec", "2",
                          "--format", "markdown", "--username-prefix", "load"};
    liteim::bench::BenchmarkOptions options;

    const auto status = liteim::bench::parseBenchmarkOptions(17, argv, options);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(options.host, "127.0.0.1");
    EXPECT_EQ(options.port, 9000);
    EXPECT_EQ(options.connections, 10U);
    EXPECT_EQ(options.message_size, 256U);
    EXPECT_EQ(options.send_interval_ms, 5U);
    EXPECT_EQ(options.duration_seconds, 2U);
    EXPECT_EQ(options.format, liteim::bench::ReportFormat::Markdown);
    EXPECT_EQ(options.username_prefix, "load");
}

TEST(BenchmarkOptionsTest, RejectsSingleConnectionPrivateBenchmark) {
    const char* argv[] = {"liteim_bench", "--connections", "1"};
    liteim::bench::BenchmarkOptions options;

    const auto status = liteim::bench::parseBenchmarkOptions(3, argv, options);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
}

TEST(BenchmarkOptionsTest, RejectsUsernamePrefixThatCanExceedMysqlUsernameLimit) {
    const char* argv[] = {"liteim_bench", "--username-prefix", "abcdefghijklmnopqrstuvwxyz"};
    liteim::bench::BenchmarkOptions options;

    const auto status = liteim::bench::parseBenchmarkOptions(3, argv, options);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
}

TEST(BenchmarkOptionsTest, RejectsMessageSizeAboveServiceTextLimit) {
    const char* argv[] = {"liteim_bench", "--message-size", "8193"};
    liteim::bench::BenchmarkOptions options;

    const auto status = liteim::bench::parseBenchmarkOptions(3, argv, options);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
}

TEST(BenchmarkStatsTest, ComputesNearestRankPercentiles) {
    std::vector<std::chrono::microseconds> latencies;
    for (std::uint64_t value = 1; value <= 100; ++value) {
        latencies.emplace_back(value);
    }

    const auto summary = liteim::bench::summarizeLatencies(latencies);

    EXPECT_EQ(summary.count, 100U);
    EXPECT_DOUBLE_EQ(summary.average_us, 50.5);
    EXPECT_EQ(summary.p50_us, 50U);
    EXPECT_EQ(summary.p95_us, 95U);
    EXPECT_EQ(summary.p99_us, 99U);
}

TEST(BenchmarkReportTest, RendersJsonReportWithRequiredMetrics) {
    liteim::bench::BenchmarkResult result;
    result.options.connections = 10;
    result.options.message_size = 128;
    result.connection_attempts = 10;
    result.connection_success = 10;
    result.request_success = 42;
    result.error_count = 1;
    result.elapsed_ms = 2000;
    result.latency.count = 42;
    result.latency.average_us = 1200.5;
    result.latency.p50_us = 1000;
    result.latency.p95_us = 2200;
    result.latency.p99_us = 3000;
    result.qps = 21.0;
    result.cpu_percent = 12.5;
    result.memory_kb = 4096;

    const auto report = liteim::bench::renderReport(result, liteim::bench::ReportFormat::Json);

    EXPECT_NE(report.find("\"connection_success\": 10"), std::string::npos);
    EXPECT_NE(report.find("\"qps\": 21"), std::string::npos);
    EXPECT_NE(report.find("\"p99_us\": 3000"), std::string::npos);
    EXPECT_NE(report.find("\"memory_kb\": 4096"), std::string::npos);
}

TEST(BenchmarkPayloadTest, BuildsPayloadWithExactSize) {
    const auto payload = liteim::bench::makePayload(12);

    EXPECT_EQ(payload.size(), 12U);
    EXPECT_EQ(payload, "liteim-load-");
}

TEST(BenchmarkHelpTest, DocumentsAcceptedFriendshipSetup) {
    const auto help = liteim::bench::benchmarkHelpText();

    EXPECT_NE(help.find("establish accepted friendship"), std::string::npos);
    EXPECT_NE(help.find("PrivateMessageRequest"), std::string::npos);
}
