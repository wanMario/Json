#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <optional>
#include <type_traits>

using index_t = std::ptrdiff_t;

// Slice 统一为 Python 切片语义，避免歧义：
//  1) 数字形式  Slice(start, end, step)  —— 完整 Python 语义，负边界按容器长度归一化：
//        Slice(-2, 0) == Python a[-2:0] == 空切片；Slice(1, -1) == 去首尾；Slice(0,10,2) == 0,2,4,6,8。
//  2) 字符串形式 Slice("[start:end:step]") —— 完整 Python 语义（省略边界 / 负步长反转 / 负边界混用）。
//  3) 显式索引列表 Slice{1,3,5} / Slice(vector) —— 表示确定的一组下标（负下标仍逐元素解析，从尾部计数）。
// 要“末尾 N 个”，用 Slice("[-N:]") 或 Slice{-N, ...}；不要用 Slice(-N, 0)（它是 Python 空切片）。
class Slice : public std::vector<index_t> {
public:
    static std::vector<index_t> build_indices(index_t start, index_t end, index_t step) {
        if (step == 0)
            throw std::invalid_argument("slice step cannot be zero");
        std::vector<index_t> res;
        if (step > 0) {
            for (index_t i = start; i < end; i += step)
                res.push_back(i);
        } else {
            for (index_t i = start; i > end; i += step)
                res.push_back(i);
        }
        return res;
    }

    // 兼容旧入口：只接受完整边界（不省略），省略边界请走字符串构造的新 Python 语义。
    static std::vector<index_t> parse_slice_string(const std::string& s) {
        std::string str;
        for (char ch : s) {
            if (ch != '[' && ch != ']')
                str.push_back(ch);
        }

        std::vector<std::string> parts;
        std::string temp;
        for (char ch : str) {
            if (ch == ':') {
                parts.push_back(temp);
                temp.clear();
            } else {
                temp.push_back(ch);
            }
        }
        parts.push_back(temp);

        auto str2num = [](const std::string& t) -> std::optional<index_t> {
            if (t.empty()) return std::nullopt;
            return static_cast<index_t>(std::stoll(t));
            };

        std::optional<index_t> opt_start, opt_end, opt_step;
        if (parts.size() == 1) {
            opt_start = str2num(parts[0]);
            opt_end = opt_start.value() + 1;
            opt_step = 1;
        } else if (parts.size() == 2) {
            opt_start = str2num(parts[0]);
            opt_end = str2num(parts[1]);
            opt_step = 1;
        } else if (parts.size() >= 3) {
            opt_start = str2num(parts[0]);
            opt_end = str2num(parts[1]);
            opt_step = str2num(parts[2]);
        } else {
            throw std::invalid_argument("invalid slice string");
        }

        if (!opt_start || !opt_end) {
            throw std::runtime_error("Empty slice bounds like [:] must bind to container length, cannot pre‑generate indices");
        }
        index_t start = *opt_start;
        index_t end = *opt_end;
        index_t step = opt_step.value_or(1);
        return build_indices(start, end, step);
    }

    template<typename Container>
    static std::vector<size_t> resolve_indices(const std::vector<index_t>& raw, const Container& c) {
        const auto len = std::ptrdiff_t(std::size(c));
        std::vector<size_t> out;
        for (auto idx : raw) {
            index_t real = idx;
            if (real < 0)
                real = len + real;
            if (real >= 0 && real < len)
                out.push_back(static_cast<size_t>(real));
        }
        return out;
    }

    // ===================== 字符串形式的 Python 切片语义 =====================
    // 保留“原始 start/end/step + 是否省略”的字段，在 iter(len) 时才按长度归一化。
    struct PyRange {
        bool has_start = false, has_end = false, has_step = false;
        index_t start = 0, end = 0, step = 1;
    };

    // 将 Python 风格切片（支持省略边界、负边界、负步长）解析为 [0,len) 有效下标。
    static std::vector<size_t> python_resolve(const PyRange& r, size_t len) {
        const index_t L = static_cast<index_t>(len);
        index_t step = r.has_step ? r.step : 1;
        if (step == 0)
            throw std::invalid_argument("slice step cannot be zero");

        auto norm_start = [&]() -> index_t {
            if (!r.has_start) return step > 0 ? 0 : L - 1;
            index_t s = r.start;
            if (s < 0) s += L;
            if (s < 0)  return step > 0 ? 0 : -1;
            if (s >= L) return step > 0 ? L : L - 1;
            return s;
            };
        auto norm_end = [&]() -> index_t {
            if (!r.has_end) return step > 0 ? L : -1;
            index_t e = r.end;
            if (e < 0) e += L;
            if (e < 0)  return step > 0 ? 0 : -1;
            if (e >= L) return step > 0 ? L : L - 1;
            return e;
            };

        const index_t start = norm_start();
        const index_t end = norm_end();
        std::vector<size_t> out;
        if (step > 0) {
            for (index_t i = start; i < end; i += step)
                if (i >= 0 && i < L) out.push_back(static_cast<size_t>(i));
        } else {
            for (index_t i = start; i > end; i += step)
                if (i >= 0 && i < L) out.push_back(static_cast<size_t>(i));
        }
        return out;
    }

    // 解析切片字符串，返回：单下标 → 直接索引列表；多段 → Python 范围（可能为 NULL 表示无需 Python 语义）。
    // 返回 true 表示走 Python 范围语义（结果存于 outRange）。
    static bool parse_slice_str(const std::string& s, std::vector<index_t>& outList, PyRange& outRange) {
        std::string str;
        for (char ch : s) {
            if (ch != '[' && ch != ']')
                str.push_back(ch);
        }

        std::vector<std::string> parts;
        std::string temp;
        for (char ch : str) {
            if (ch == ':') { parts.push_back(temp); temp.clear(); } else { temp.push_back(ch); }
        }
        parts.push_back(temp);

        auto str2num = [](const std::string& t) -> std::optional<index_t> {
            if (t.empty()) return std::nullopt;
            try { return static_cast<index_t>(std::stoll(t)); }
            catch (...) { throw std::invalid_argument("invalid slice string"); }
            };

        if (parts.size() == 1) {
            // 单下标：正负均可，表示为 [x, x+1)
            auto v = str2num(parts[0]);
            if (!v) throw std::invalid_argument("invalid slice string");
            outList = build_indices(*v, *v + 1, 1);
            return false;
        }
        if (parts.size() >= 2) {
            auto ps = str2num(parts[0]);
            auto pe = str2num(parts[1]);
            auto pstep = parts.size() >= 3 ? str2num(parts[2]) : std::optional<index_t>(1);

            // 需要 Python 语义：省略了边界，或步长为负，或任一显式边界为负
            bool need_python = !ps || !pe || (pstep && *pstep < 0) || (ps && *ps < 0) || (pe && *pe < 0);
            if (need_python) {
                outRange = PyRange{};
                outRange.has_start = ps.has_value(); if (ps) outRange.start = *ps;
                outRange.has_end = pe.has_value(); if (pe) outRange.end = *pe;
                outRange.has_step = pstep.has_value(); if (pstep) outRange.step = *pstep;
                if (outRange.has_step && outRange.step == 0)
                    throw std::invalid_argument("slice step cannot be zero");
                return true;
            }
            outList = build_indices(*ps, *pe, pstep ? *pstep : 1);
            return false;
        }
        throw std::invalid_argument("invalid slice string");
    }

    // 将本切片解析为 [0,len) 的有效下标。
    // 字符串形式的 Python 范围按长度归一化；数字形式 / 显式下标列表逐元素解析（负索引从尾部计数）。
    std::vector<size_t> iter(size_t len) const {
        if (m_pyrange) return python_resolve(*m_pyrange, len);
        std::vector<size_t> out;
        const auto ilen = static_cast<index_t>(len);
        for (auto idx : *this) {
            index_t real = idx;
            if (real < 0)
                real = ilen + real;
            if (real >= 0 && real < ilen)
                out.push_back(static_cast<size_t>(real));
        }
        return out;
    }

    // 由一组（已解析好的）非负下标构造切片，供下游拼接复合切片使用。
    static Slice FromIndices(const std::vector<size_t>& indices) {
        std::vector<index_t> tmp(indices.begin(), indices.end());
        return Slice(std::move(tmp));
    }

    // 1. start, end, step（数字形式）—— 完整 Python 切片语义。
    //    负边界在 iter(len) 时按容器长度归一化：
    //      Slice(-2, 0) == Python a[-2:0] == 空切片；
    //      Slice(1, -1) == Python a[1:-1] == 去首尾；
    //      Slice(0, 10, 2) == 0,2,4,6,8。
    //    要“末尾 N 个”请用字符串形式 Slice("[-N:]")，或显式列表 Slice{-N, ...}。
    Slice(index_t start, index_t end, index_t step = 1) {
        if (step == 0)
            throw std::invalid_argument("slice step cannot be zero");
        m_pyrange = PyRange{};
        m_pyrange->has_start = true; m_pyrange->start = start;
        m_pyrange->has_end   = true; m_pyrange->end   = end;
        m_pyrange->has_step  = true; m_pyrange->step  = step;
    }

    // 2. 切片字符串 "[1:4]" / "[1:]" / "[:5]" / "[::-1]" / "[1:-1]" / "[-2:]" / "[3]"
    explicit Slice(const std::string& slice_str) {
        std::vector<index_t> list;
        PyRange range;
        if (parse_slice_str(slice_str, list, range)) {
            m_pyrange = range;              // Python 语义，iter(len) 时再解析
        } else {
            this->assign(list.begin(), list.end());
        }
    }

    // 3. vector 构造 & 保留 initializer_list
    Slice(std::vector<index_t> indices)
        : std::vector<index_t>(std::move(indices)) {}

    Slice(std::initializer_list<index_t> indices)
        : std::vector<index_t>(std::move(indices)) {}

    // 迭代器区间构造（供交并集内部返回结果用）。
    // 【重要】必须用 SFINAE 排除整型实参：否则 Slice(a, b)（如 Slice(-2, 0)）在
    // index_t 与 int 不同（64 位平台 index_t=long）时，会优先匹配到本模板，把整数
    // 当成“迭代器对”调 std::vector(size, val)，用 -2 分配一个天文数字大小的 vector 而崩溃。
    template<typename Iter, typename = std::enable_if_t<!std::is_integral<Iter>::value>>
    Slice(Iter first, Iter last)
        : std::vector<index_t>(first, last) {}

    // 交集 &
    Slice operator&(const Slice& other) const {
        std::vector<index_t> a{ *this };
        std::vector<index_t> b{ other };
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        std::vector<index_t> inter;
        std::set_intersection(a.begin(), a.end(),
            b.begin(), b.end(),
            std::back_inserter(inter));
        return Slice(inter.begin(), inter.end());
    }

    // 并集 |
    Slice operator|(const Slice& other) const {
        std::vector<index_t> a{ *this };
        std::vector<index_t> b{ other };
        std::vector<index_t> un;
        std::merge(a.begin(), a.end(),
            b.begin(), b.end(),
            std::back_inserter(un));
        auto last = std::unique(un.begin(), un.end());
        un.erase(last, un.end());
        return Slice(un.begin(), un.end());
    }

    // 容器采样（支持字符串形式的 Python 切片）
    template<typename Container>
    friend Container operator&(const Slice& sl, const Container& src) {
        auto real_idx = sl.iter(std::size(src));
        Container res;
        for (auto pos : real_idx) {
            res.push_back(src[pos]);
        }
        return res;
    }

private:
    std::optional<PyRange> m_pyrange;
};
