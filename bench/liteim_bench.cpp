#include "Benchmark.hpp"

#include <iostream>

int main(int argc, const char* argv[]) {
    liteim::bench::BenchmarkOptions options;
    auto status = liteim::bench::parseBenchmarkOptions(argc, argv, options);
    if (!status.isOk()) {
        std::cerr << "liteim_bench: " << status.message() << '\n';
        std::cerr << liteim::bench::benchmarkHelpText() << '\n';
        return 1;
    }

    if (options.help) {
        std::cout << liteim::bench::benchmarkHelpText() << '\n';
        return 0;
    }

    liteim::bench::BenchmarkResult result;
    status = liteim::bench::runBenchmark(options, result);
    if (!status.isOk()) {
        std::cerr << "liteim_bench: " << status.message() << '\n';
        return 1;
    }

    std::cout << liteim::bench::renderReport(result, options.format) << '\n';
    return 0;
}
