#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace liteim {

class Timestamp {
public:
    using Clock = std::chrono::system_clock;

    Timestamp();
    explicit Timestamp(Clock::time_point time_point);

    static Timestamp now();
    std::int64_t millisecondsSinceEpoch() const;
    std::string toIso8601String() const;

private:
    Clock::time_point time_point_;
};

}  // namespace liteim
