#include "liteim/base/Timestamp.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace liteim
{

    Timestamp::Timestamp() // 保存当前时间
        : time_point_(Clock::now())
    {
    }

    Timestamp::Timestamp(Clock::time_point time_point)
        : time_point_(time_point) {}

    Timestamp Timestamp::now()
    {
        return Timestamp{Clock::now()};
    }

    std::int64_t Timestamp::millisecondsSinceEpoch() const
    { // 返回自 Unix epoch 1970-01-01T00:00:00Z 以来的毫秒数
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   time_point_.time_since_epoch())
            .count();
    }

    std::string Timestamp::toIso8601String() const
    { // 转成 ISO8601 字符串 格式，形如 "2024-06-01T12:34:56Z"
        const auto time = Clock::to_time_t(time_point_);
        std::tm utc_time{};
        gmtime_r(&time, &utc_time);

        std::ostringstream output;
        output << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
        return output.str();
    }

} // namespace liteim
