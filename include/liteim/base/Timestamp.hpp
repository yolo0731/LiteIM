#pragma once

#include <chrono>
#include <cstdint>
#include <string>

/*封装成 Timestamp，统一规则：

内部用 std::chrono::system_clock::time_point
数字输出用毫秒
字符串输出用 UTC ISO8601 */
namespace liteim
{

    class Timestamp
    {
    public:
        using Clock = std::chrono::system_clock; // 是 C++ 标准库里的一个时钟类型，表示“系统真实时间”

        Timestamp();
        explicit Timestamp(Clock::time_point time_point);

        static Timestamp now();
        std::int64_t millisecondsSinceEpoch() const;
        std::string toIso8601String() const;

    private:
        Clock::time_point time_point_;
    };

} // namespace liteim
