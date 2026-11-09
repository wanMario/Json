#include "Number.hpp"

#include <stdexcept>
#include <limits>
#include <cmath>
#include <cerrno>
#include <cstdlib>

namespace JSON {

    // -------- 十进制有符号整数串比较（用于大整数精确比较） --------
    namespace {
        // 要求字符串已规范化（无前导零，"-0" 已归一为 "0"）
        int cmpBigStr(const std::string& a, const std::string& b) {
            const bool na = !a.empty() && a[0] == '-';
            const bool nb = !b.empty() && b[0] == '-';
            if (na != nb) return na ? -1 : 1;
            const size_t ia = na ? 1 : 0;
            const size_t ib = nb ? 1 : 0;
            const size_t la = a.size() - ia;
            const size_t lb = b.size() - ib;
            if (la != lb) {
                const int r = la < lb ? -1 : 1;
                return na ? -r : r;
            }
            for (size_t i = 0; i < la; ++i) {
                const char ca = a[ia + i];
                const char cb = b[ib + i];
                if (ca != cb) {
                    const int r = ca < cb ? -1 : 1;
                    return na ? -r : r;
                }
            }
            return 0;
        }

        // 把 int64/uint64 分支转成十进制字符串（与字符串大整数精确比较用）
        std::string numToDecStr(const Number& n) {
            if (auto* p = std::get_if<std::int64_t>(&n.m)) return std::to_string(*p);
            return std::to_string(std::get<std::uint64_t>(n.m));
        }
    } // namespace

    double Number::asDouble() const {
        if (auto* p = std::get_if<std::int64_t>(&m))  return static_cast<double>(*p);
        if (auto* p = std::get_if<std::uint64_t>(&m)) return static_cast<double>(*p);
        if (auto* p = std::get_if<long double>(&m))   return static_cast<double>(*p);
        return std::strtold(std::get<std::string>(m).c_str(), nullptr);
    }

    long double Number::asLongDouble() const {
        if (auto* p = std::get_if<std::int64_t>(&m))  return static_cast<long double>(*p);
        if (auto* p = std::get_if<std::uint64_t>(&m)) return static_cast<long double>(*p);
        if (auto* p = std::get_if<long double>(&m))   return *p;
        return std::strtold(std::get<std::string>(m).c_str(), nullptr);
    }

    std::int64_t Number::asInt64() const {
        if (auto* p = std::get_if<std::int64_t>(&m)) return *p;
        if (auto* p = std::get_if<std::uint64_t>(&m)) {
            if (*p > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
                throw std::overflow_error("Number too large for int64");
            return static_cast<std::int64_t>(*p);
        }
        if (auto* p = std::get_if<long double>(&m)) {
            const long double v = *p;
            if (!std::isfinite(static_cast<double>(v)) || std::fmod(v, 1.0L) != 0.0L)
                throw std::overflow_error("Number is not an integer value");
            if (v < static_cast<long double>((std::numeric_limits<std::int64_t>::min)()) ||
                v > static_cast<long double>((std::numeric_limits<std::int64_t>::max)()))
                throw std::overflow_error("Number out of int64 range");
            return static_cast<std::int64_t>(v);
        }
        errno = 0;
        const char* s = std::get<std::string>(m).c_str();
        char* end = nullptr;
        const long long v = std::strtoll(s, &end, 10);
        if (errno == ERANGE || end == s || *end != '\0')
            throw std::overflow_error("big integer out of int64 range");
        return static_cast<std::int64_t>(v);
    }

    std::uint64_t Number::asUInt64() const {
        if (auto* p = std::get_if<std::uint64_t>(&m)) return *p;
        if (auto* p = std::get_if<std::int64_t>(&m)) {
            if (*p < 0) throw std::overflow_error("negative Number cannot fit uint64");
            return static_cast<std::uint64_t>(*p);
        }
        if (auto* p = std::get_if<long double>(&m)) {
            const long double v = *p;
            if (!std::isfinite(static_cast<double>(v)) || std::fmod(v, 1.0L) != 0.0L)
                throw std::overflow_error("Number is not an integer value");
            if (v < 0.0L ||
                v > static_cast<long double>((std::numeric_limits<std::uint64_t>::max)()))
                throw std::overflow_error("Number out of uint64 range");
            return static_cast<std::uint64_t>(v);
        }
        const std::string& s = std::get<std::string>(m);
        if (!s.empty() && s[0] == '-')
            throw std::overflow_error("negative big integer cannot fit uint64");
        errno = 0;
        const char* cstr = s.c_str();
        char* end = nullptr;
        const unsigned long long v = std::strtoull(cstr, &end, 10);
        if (errno == ERANGE || end == cstr || *end != '\0')
            throw std::overflow_error("big integer out of uint64 range");
        return static_cast<std::uint64_t>(v);
    }

    // -------- 跨类型比较 --------
    bool operator==(const Number& a, const Number& b) {
        if (a.m.index() == b.m.index()) {
            switch (a.m.index()) {
                case 0: return std::get<0>(a.m) == std::get<0>(b.m);
                case 1: return std::get<1>(a.m) == std::get<1>(b.m);
                case 2: return std::get<2>(a.m) == std::get<2>(b.m);
                default: return cmpBigStr(std::get<3>(a.m), std::get<3>(b.m)) == 0;
            }
        }
        // 大整数参与比较
        if (a.isBigInt() || b.isBigInt()) {
            if (a.isFloat() || b.isFloat())                       // 大整数 vs 浮点：长双精度近似
                return a.asLongDouble() == b.asLongDouble();
            if (a.isBigInt())
                return cmpBigStr(std::get<3>(a.m), numToDecStr(b)) == 0;
            return cmpBigStr(numToDecStr(a), std::get<3>(b.m)) == 0;
        }
        // int64/uint64/long double 之间：80 位扩展精度下整数→long double 无损
        return a.asLongDouble() == b.asLongDouble();
    }

    bool operator<(const Number& a, const Number& b) {
        if (a.m.index() == b.m.index()) {
            switch (a.m.index()) {
                case 0: return std::get<0>(a.m) < std::get<0>(b.m);
                case 1: return std::get<1>(a.m) < std::get<1>(b.m);
                case 2: return std::get<2>(a.m) < std::get<2>(b.m);
                default: return cmpBigStr(std::get<3>(a.m), std::get<3>(b.m)) < 0;
            }
        }
        if (a.isBigInt() || b.isBigInt()) {
            if (a.isFloat() || b.isFloat())
                return a.asLongDouble() < b.asLongDouble();
            if (a.isBigInt())
                return cmpBigStr(std::get<3>(a.m), numToDecStr(b)) < 0;
            return cmpBigStr(numToDecStr(a), std::get<3>(b.m)) < 0;
        }
        return a.asLongDouble() < b.asLongDouble();
    }

    bool operator!=(const Number& a, const Number& b) { return !(a == b); }
    bool operator<=(const Number& a, const Number& b) { return a == b || a < b; }
    bool operator>(const Number& a, const Number& b)  { return b < a; }
    bool operator>=(const Number& a, const Number& b) { return !(a < b); }

} // namespace JSON
