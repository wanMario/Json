#pragma once

#include <variant>
#include <string>
#include <cstdint>
#include <type_traits>

namespace JSON {

    // ===================== Number：数值变体（整数保真） =====================
    // 存储 int64 / uint64 / long double / 任意精度整数（十进制字符串，规范化：无前导零）。
    //   - 解析整数字面量：int64 → uint64 → 字符串大整数，逐级保精度；
    //   - 浮点字面量与 double 构造提升为 long double 存储；
    //   - asDouble() 会截断，精确读取用 asInt64()/asUInt64()/asLongDouble()/asBigIntString()。
    // 大整数只做存储/比较/序列化（不做算术）；与 int64/uint64 比较是精确的，与 long double 比较按长双精度近似。
    class Number {
    public:
        std::variant<std::int64_t, std::uint64_t, long double, std::string> m;

        Number() : m(std::int64_t(0)) {}

        // 浮点：double/float 经浮点提升唯一匹配（GCC 对 整型 conversion 与
        // 浮点 promotion 同 rank，故整型走下方模板，避免 Number(3.14) 歧义）
        Number(long double v) : m(v) {}

        // 整型：按符号分派到 int64 / uint64（bool 排除）
        template <typename T,
                  std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T> &&
                                       !std::is_same_v<std::decay_t<T>, bool>, int> = 0>
        Number(T v) : m(static_cast<std::int64_t>(v)) {}
        template <typename T,
                  std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>, int> = 0>
        Number(T v) : m(static_cast<std::uint64_t>(v)) {}

        explicit Number(std::string bigint) : m(std::move(bigint)) {}

        [[nodiscard]] bool isInt() const noexcept { return std::holds_alternative<std::int64_t>(m); }
        [[nodiscard]] bool isUInt() const noexcept { return std::holds_alternative<std::uint64_t>(m); }
        [[nodiscard]] bool isFloat() const noexcept { return std::holds_alternative<long double>(m); }
        [[nodiscard]] bool isBigInt() const noexcept { return std::holds_alternative<std::string>(m); }
        [[nodiscard]] bool isInteger() const noexcept { return !isFloat(); }

        [[nodiscard]] double asDouble() const;
        [[nodiscard]] long double asLongDouble() const;
        [[nodiscard]] std::int64_t asInt64() const;    // 溢出抛 std::overflow_error
        [[nodiscard]] std::uint64_t asUInt64() const;  // 溢出抛 std::overflow_error
        [[nodiscard]] const std::string& asBigIntString() const { return std::get<std::string>(m); }

        // 常规性适配：跨类型比较（大整数与 int64/uint64 精确；与 long double 近似）
        friend bool operator==(const Number& a, const Number& b);
        friend bool operator!=(const Number& a, const Number& b);
        friend bool operator<(const Number& a, const Number& b);
        friend bool operator<=(const Number& a, const Number& b);
        friend bool operator>(const Number& a, const Number& b);
        friend bool operator>=(const Number& a, const Number& b);
    };

} // namespace JSON
