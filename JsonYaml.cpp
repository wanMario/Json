// ============================================================================
// JsonYaml.cpp — JsonLib YAML 导入 / 导出实现（见 JsonYaml.hpp 头注释）
//
// 结构：行预处理（缩进 / 注释 / 文档标记）→ 递归下降解析
// （parseBlock / parseMapping / parseSequence / parseFlow / parseScalar）
// → 标量类型推断（复用 Number 保精度）。序列化 ToYaml 递归输出，
// 字符串自动判定是否需要引号，保证可往返还原。
// ============================================================================
#include "JsonYaml.hpp"

// GCC 14 在 -O2 下对 std::variant 内联展开 JsonElement/OrderedObject 的移动构造时，
// 产生 -Wmaybe-uninitialized 误报（JsonCore.hpp 中 m_tombstoneCount 等均有类内初始化，
// 无真实未初始化路径）。该警告源于被包含的 JsonCore.hpp 而非本文件代码，此处按本 TU 抑制。
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <vector>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <charconv>
#include <fstream>
#include <sstream>
#include <limits>

namespace JSON {
namespace {

    // ---------------- 小工具 ----------------
    std::string& ltrim(std::string& s) {
        size_t p = 0;
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
        s.erase(0, p);
        return s;
    }
    std::string& rtrim(std::string& s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        return s;
    }
    std::string trim(std::string s) { ltrim(s); rtrim(s); return s; }

    [[noreturn]] void fail(size_t off, size_t line, size_t col, const char* exp) {
        std::string msg = "YAML parse error at offset " + std::to_string(off) +
                          " (line " + std::to_string(line) + ", column " + std::to_string(col) +
                          "): expected " + exp;
        throw YamlParseError(off, line, col, exp, std::move(msg));
    }

    // ---------------- 输入行 ----------------
    struct Line {
        int         indent;   // 前导空格数
        std::string content;  // 去缩进、去注释后的内容
        size_t      lineNo;   // 1 起始
        size_t      offset;   // 本行在原文中的字节偏移
    };

    // ---------------- 标量类型判定 ----------------
    enum class NumKind { NotNumber, Int, Float };

    // 扫描是否数字：整数 [+-]?digits；浮点 [+-]?(digits(.digits?)?|.digits)([eE][+-]?digits)?
    NumKind classifyNumber(const std::string& s) {
        size_t i = 0, n = s.size();
        if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
        if (i >= n) return NumKind::NotNumber;
        bool hasDigits = false;
        while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) { hasDigits = true; ++i; }
        bool isFloat = false;
        if (i < n && s[i] == '.') {
            isFloat = true; ++i;
            while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) { hasDigits = true; ++i; }
        }
        if (i < n && (s[i] == 'e' || s[i] == 'E')) {
            isFloat = true; ++i;
            if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
            bool expDigits = false;
            while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) { expDigits = true; ++i; }
            if (!expDigits) return NumKind::NotNumber;
        }
        if (i != n || !hasDigits) return NumKind::NotNumber;
        return isFloat ? NumKind::Float : NumKind::Int;
    }

    // 整数字面量 → Number（int64 → uint64 → 字符串大整数，复用 JsonCore 保精度）
    JsonElement numberFromInteger(const std::string& s) {
        std::int64_t i64 = 0;
        auto r1 = std::from_chars(s.data(), s.data() + s.size(), i64);
        if (r1.ec == std::errc() && r1.ptr == s.data() + s.size())
            return JsonElement(Number(i64));
        std::uint64_t u64 = 0;
        auto r2 = std::from_chars(s.data(), s.data() + s.size(), u64);
        if (r2.ec == std::errc() && r2.ptr == s.data() + s.size())
            return JsonElement(Number(u64));
        // 超出 int64/uint64 或带符号的负大整数 → 字符串大整数（只存储/比较/序列化）
        return JsonElement(Number(std::string(s)));
    }

    JsonElement numberFromFloat(const std::string& s) {
        char* end = nullptr;
        long double v = std::strtold(s.c_str(), &end);
        (void)end;
        return JsonElement(Number(v));
    }

    bool isNullish(const std::string& s) {
        return s == "~" || s == "null" || s == "Null" || s == "NULL";
    }
    bool isBoolish(const std::string& s) {
        return s == "true" || s == "True" || s == "TRUE" ||
               s == "false" || s == "False" || s == "FALSE" ||
               s == "yes" || s == "Yes" || s == "YES" ||
               s == "no" || s == "No" || s == "NO" ||
               s == "on" || s == "On" || s == "ON" ||
               s == "off" || s == "Off" || s == "OFF";
    }
    bool boolValue(const std::string& s) {
        return s[0] == 't' || s[0] == 'T' || s[0] == 'y' || s[0] == 'Y' || s[0] == 'o' || s[0] == 'O' || s[0] == 'n' || s[0] == 'N';
    }

    // ---------------- 引号字符串 ----------------
    // 单引号：'' 转义为 '，其余字面
    std::string parseSingleQuoted(const std::string& s, size_t& i, size_t baseOff, size_t lineNo) {
        // i 指向开头 '
        ++i; std::string out;
        while (i < s.size()) {
            char c = s[i];
            if (c == '\'') {
                if (i + 1 < s.size() && s[i + 1] == '\'') { out.push_back('\''); i += 2; continue; }
                ++i; return out;
            }
            out.push_back(c); ++i;
        }
        fail(baseOff + i, lineNo, i + 1, "'");
    }

    // 双引号：标准 JSON 转义（含 \uXXXX 代理对 → UTF-8）
    void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    unsigned hexVal(char c) {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
        return 0xFFFFFFFFU;
    }
    std::string parseDoubleQuoted(const std::string& s, size_t& i, size_t baseOff, size_t lineNo) {
        ++i; std::string out;
        while (i < s.size()) {
            char c = s[i];
            if (c == '"') { ++i; return out; }
            if (c == '\\') {
                if (i + 1 >= s.size()) fail(baseOff + i, lineNo, i + 1, "escape");
                char e = s[i + 1];
                switch (e) {
                    case '"': out.push_back('"'); i += 2; break;
                    case '\\': out.push_back('\\'); i += 2; break;
                    case '/': out.push_back('/'); i += 2; break;
                    case 'b': out.push_back('\b'); i += 2; break;
                    case 'f': out.push_back('\f'); i += 2; break;
                    case 'n': out.push_back('\n'); i += 2; break;
                    case 'r': out.push_back('\r'); i += 2; break;
                    case 't': out.push_back('\t'); i += 2; break;
                    case 'u': {
                        if (i + 6 > s.size()) fail(baseOff + i, lineNo, i + 1, "\\uXXXX");
                        unsigned cp = 0;
                        for (int k = 1; k <= 4; ++k) {
                            unsigned h = hexVal(s[i + 1 + k]);
                            if (h == 0xFFFFFFFFU) fail(baseOff + i, lineNo, i + 1, "\\uXXXX");
                            cp = (cp << 4) | h;
                        }
                        i += 6;
                        // 代理对
                        if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                            unsigned lo = 0;
                            bool ok = true;
                            for (int k = 1; k <= 4; ++k) {
                                unsigned h = hexVal(s[i + 1 + k]);
                                if (h == 0xFFFFFFFFU) { ok = false; break; }
                                lo = (lo << 4) | h;
                            }
                            if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                i += 6;
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: fail(baseOff + i, lineNo, i + 1, "valid escape");
                }
                continue;
            }
            out.push_back(c); ++i;
        }
        fail(baseOff + i, lineNo, i + 1, "\"");
    }

    // ---------------- 序列化辅助（输出） ----------------
    void appendJsonEscape(std::string& out, const std::string& s) {
        out.push_back('"');
        for (char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(c)));
                        out += buf;
                    } else out.push_back(c);
            }
        }
        out.push_back('"');
    }

    // 数字 → YAML 文本（int64/uint64 最短、long double 无损转 double 最短、否则 %.21Lg、bigint 原串）
    std::string numberToYamlText(const Number& n) {
        if (n.isInt()) {
            char b[24]; auto r = std::to_chars(b, b + sizeof b, n.asInt64());
            return std::string(b, r.ptr);
        }
        if (n.isUInt()) {
            char b[24]; auto r = std::to_chars(b, b + sizeof b, n.asUInt64());
            return std::string(b, r.ptr);
        }
        if (n.isBigInt()) return n.asBigIntString();
        long double v = n.asLongDouble();
        if (std::isfinite(v)) {
            double dv = static_cast<double>(v);
            if (static_cast<long double>(dv) == v) {
                char b[40]; auto r = std::to_chars(b, b + sizeof b, dv);
                return std::string(b, r.ptr);
            }
        }
        char b[128];
        std::snprintf(b, sizeof b, "%.21Lg", v);
        return b;
    }

    // 字符串是否需要引号（防止被 YAML 重新解析成其他类型 / 破坏结构）
    bool needsQuote(const std::string& s) {
        if (s.empty()) return true;
        if (s != trim(s)) return true;              // 首尾空白
        if (s.find('\n') != std::string::npos) return true;
        char first = s.front();
        if (first == '-' || first == '?' || first == ':' || first == ',' ||
            first == '[' || first == ']' || first == '{' || first == '}' ||
            first == '#' || first == '&' || first == '*' || first == '!' ||
            first == '|' || first == '>' || first == '\'' || first == '"' ||
            first == '%' || first == '@' || first == '`' || first == ' ')
            return true;
        if (s.find(": ") != std::string::npos || s.back() == ':') return true;
        if (s.find(" #") != std::string::npos) return true;
        if (isNullish(s) || isBoolish(s)) return true;
        if (classifyNumber(s) != NumKind::NotNumber) return true;
        return false;
    }

} // namespace

// ============================================================================
// YAML 解析器
// ============================================================================
namespace {

    class YamlParser {
        std::vector<Line> m_lines;
        size_t m_idx{ 0 };

    public:
        explicit YamlParser(std::vector<Line> lines) : m_lines(std::move(lines)) {}

        JsonElement parseDocument() {
            if (m_lines.empty()) return JsonElement(nullptr);
            return parseBlock(m_lines[m_idx].indent);
        }

    private:
        bool atEnd() const { return m_idx >= m_lines.size(); }

        // 解析以 anchorCol 列开始的整个块（映射 / 序列 / 单标量）
        JsonElement parseBlock(int anchorCol) {
            if (atEnd()) return JsonElement(nullptr);
            const Line& l = m_lines[m_idx];
            if (l.indent != anchorCol)
                fail(l.offset, l.lineNo, static_cast<size_t>(l.indent) + 1, "aligned indentation");
            if (isSeqItem(l.content)) return parseSequence(anchorCol);
            if (isMappingItem(l.content)) return parseMapping(anchorCol);
            // 单标量（顶层文档或块标量值）
            return parseScalar(l.content, l.offset, l.lineNo);
        }

        static bool isSeqItem(const std::string& c) {
            if (c == "-") return true;
            return c.size() >= 2 && c[0] == '-' && c[1] == ' ';
        }
        static std::string seqRest(const std::string& c) {
            if (c == "-") return std::string();
            std::string r = c.substr(2);
            return trim(r);
        }

        // 拆映射项：返回 (key, rest, 是否映射)。key 去引号。
        static std::tuple<std::string, std::string, bool> splitMapping(const std::string& c) {
            size_t n = c.size();
            char quote = 0;
            for (size_t i = 0; i < n; ++i) {
                char ch = c[i];
                if (quote) {
                    if (ch == quote) {
                        if (quote == '\'' && i + 1 < n && c[i + 1] == '\'') { ++i; continue; }
                        quote = 0;
                    }
                    continue;
                }
                if (ch == '\'' || ch == '"') { quote = ch; continue; }
                if (ch == ':' && (i + 1 == n || c[i + 1] == ' ' || c[i + 1] == '\t')) {
                    std::string key = c.substr(0, i);
                    rtrim(key);
                    std::string rest = trim(c.substr(i + 1));
                    // 键去引号
                    if (key.size() >= 2 && key.front() == key.back() &&
                        (key.front() == '"' || key.front() == '\'')) {
                        size_t kp = 0;
                        std::string unq = (key.front() == '"')
                            ? parseDoubleQuoted(key, kp, 0, 0)
                            : parseSingleQuoted(key, kp, 0, 0);
                        key = unq;
                    }
                    return { std::move(key), std::move(rest), true };
                }
            }
            return { c, std::string(), false };
        }
        static bool isMappingItem(const std::string& c) { return std::get<2>(splitMapping(c)); }

        // 给对象赋一个键的值（rest 为空→嵌套块或 null；flow→流式；否则标量）
        void assignValue(UnorderedObject& obj, std::string key, const std::string& rest,
                         size_t restOff, size_t lineNo, int anchorCol) {
            if (rest.empty()) {
                if (!atEnd() && m_lines[m_idx].indent > anchorCol)
                    obj[std::move(key)] = parseBlock(m_lines[m_idx].indent);
                else
                    obj[std::move(key)] = JsonElement(nullptr);
            } else if (rest[0] == '[' || rest[0] == '{') {
                obj[std::move(key)] = parseFlow(rest, restOff, lineNo);
            } else if (rest[0] == '"') {
                size_t p = 0;
                obj[std::move(key)] = JsonElement(parseDoubleQuoted(rest, p, restOff, lineNo));
            } else if (rest[0] == '\'') {
                size_t p = 0;
                obj[std::move(key)] = JsonElement(parseSingleQuoted(rest, p, restOff, lineNo));
            } else {
                obj[std::move(key)] = parseScalar(rest, restOff, lineNo);
            }
        }

        // 块映射：键列 == anchorCol
        JsonElement parseMapping(int anchorCol) {
            UnorderedObject obj;
            while (!atEnd()) {
                const Line& l = m_lines[m_idx];
                if (l.indent != anchorCol) break;
                if (!isMappingItem(l.content)) break;   // 序列项或标量 → 块结束
                auto [key, rest, ok] = splitMapping(l.content);
                size_t lineNo = l.lineNo;
                size_t restOff = l.offset + (l.content.size() - rest.size());
                m_idx++;
                if (!ok) break;
                assignValue(obj, key, rest, restOff, lineNo, anchorCol);
            }
            return obj;
        }

        // 块序列：'-' 列 == itemCol
        JsonElement parseSequence(int itemCol) {
            JsonArray arr;
            while (!atEnd()) {
                const Line& l = m_lines[m_idx];
                if (l.indent != itemCol || !isSeqItem(l.content)) break;
                size_t lineNo = l.lineNo;
                size_t lineOff = l.offset;
                std::string rest = seqRest(l.content);
                m_idx++;
                if (rest.empty()) {
                    if (!atEnd() && m_lines[m_idx].indent > itemCol)
                        arr.push_back(parseBlock(m_lines[m_idx].indent));
                    else
                        arr.push_back(JsonElement(nullptr));
                } else if (rest[0] == '[' || rest[0] == '{') {
                    arr.push_back(parseFlow(rest, lineOff, lineNo));
                } else {
                    auto [key, r2, isMap] = splitMapping(rest);
                    if (isMap) {
                        // "- key: value"：行内映射，键列 = itemCol + 2；后续同缩进映射行并入
                        UnorderedObject obj;
                        size_t anchor = static_cast<size_t>(itemCol) + 2;
                        assignValue(obj, key, r2, lineOff, lineNo, static_cast<int>(anchor));
                        while (!atEnd()) {
                            const Line& l2 = m_lines[m_idx];
                            if (l2.indent != static_cast<int>(anchor) || !isMappingItem(l2.content)) break;
                            auto [k2, r3, ok2] = splitMapping(l2.content);
                            size_t lno = l2.lineNo;
                            size_t roff = l2.offset + (l2.content.size() - r3.size());
                            m_idx++;
                            if (!ok2) break;
                            assignValue(obj, k2, r3, roff, lno, static_cast<int>(anchor));
                        }
                        arr.push_back(std::move(obj));
                    } else if (rest[0] == '"' || rest[0] == '\'') {
                        arr.push_back(parseQuotedScalar(rest, lineOff, lineNo));
                    } else {
                        arr.push_back(parseScalar(rest, lineOff, lineNo));
                    }
                }
            }
            return arr;
        }

        JsonElement parseQuotedScalar(const std::string& s, size_t baseOff, size_t lineNo) {
            size_t p = 0;
            if (s[p] == '"') return JsonElement(parseDoubleQuoted(s, p, baseOff, lineNo));
            return JsonElement(parseSingleQuoted(s, p, baseOff, lineNo));
        }

        // 裸标量 / 引号标量 → JsonElement
        JsonElement parseScalar(const std::string& s, size_t baseOff, size_t lineNo) {
            if (s.empty()) return JsonElement(nullptr);
            if (s[0] == '"') return parseQuotedScalar(s, baseOff, lineNo);
            if (s[0] == '\'') return parseQuotedScalar(s, baseOff, lineNo);
            if (isNullish(s)) return JsonElement(nullptr);
            if (isBoolish(s)) return JsonElement(boolValue(s));
            NumKind k = classifyNumber(s);
            if (k == NumKind::Int) return numberFromInteger(s);
            if (k == NumKind::Float) return numberFromFloat(s);
            return JsonElement(s);   // 裸字符串
        }

        // ---------------- 流式内联 [ ... ] / { ... } ----------------
        JsonElement parseFlow(const std::string& s, size_t baseOff, size_t lineNo) {
            size_t pos = 0;
            JsonElement v = parseFlowValue(s, pos, baseOff, lineNo);
            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
            if (pos < s.size()) fail(baseOff + pos, lineNo, pos + 1, "end of flow");
            return v;
        }

        JsonElement parseFlowValue(const std::string& s, size_t& pos, size_t baseOff, size_t lineNo) {
            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
            if (pos >= s.size()) fail(baseOff + pos, lineNo, pos + 1, "flow value");
            char c = s[pos];
            if (c == '{') return parseFlowMap(s, pos, baseOff, lineNo);
            if (c == '[') return parseFlowSeq(s, pos, baseOff, lineNo);
            if (c == '"') return JsonElement(parseDoubleQuoted(s, pos, baseOff, lineNo));
            if (c == '\'') return JsonElement(parseSingleQuoted(s, pos, baseOff, lineNo));
            size_t start = pos;
            while (pos < s.size() && s[pos] != ',' && s[pos] != '}' && s[pos] != ']') ++pos;
            std::string t = trim(s.substr(start, pos - start));
            if (t.empty()) fail(baseOff + pos, lineNo, pos + 1, "flow value");
            return parseScalar(t, baseOff + start, lineNo);
        }

        JsonElement parseFlowMap(const std::string& s, size_t& pos, size_t baseOff, size_t lineNo) {
            UnorderedObject obj;
            ++pos; // '{'
            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
            if (pos < s.size() && s[pos] == '}') { ++pos; return obj; }
            while (true) {
                while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
                if (pos >= s.size()) fail(baseOff + pos, lineNo, pos + 1, "'}'");
                std::string key;
                if (s[pos] == '"') key = parseDoubleQuoted(s, pos, baseOff, lineNo);
                else if (s[pos] == '\'') key = parseSingleQuoted(s, pos, baseOff, lineNo);
                else {
                    size_t ks = pos;
                    while (pos < s.size() && s[pos] != ':' && s[pos] != ',' && s[pos] != '}' && s[pos] != '[' && s[pos] != ']' && s[pos] != '{')
                        ++pos;
                    key = trim(s.substr(ks, pos - ks));
                    if (key.empty()) fail(baseOff + pos, lineNo, pos + 1, "flow key");
                }
                while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
                if (pos >= s.size() || s[pos] != ':') fail(baseOff + pos, lineNo, pos + 1, "':'");
                ++pos;
                while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
                JsonElement val;
                if (pos < s.size() && (s[pos] == ',' || s[pos] == '}')) val = JsonElement(nullptr);
                else val = parseFlowValue(s, pos, baseOff, lineNo);
                obj[std::move(key)] = std::move(val);
                while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
                if (pos >= s.size()) fail(baseOff + pos, lineNo, pos + 1, "'}'");
                if (s[pos] == ',') {
                    ++pos;
                    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
                    if (pos < s.size() && s[pos] == '}') { ++pos; return obj; }
                    continue;
                }
                if (s[pos] == '}') { ++pos; return obj; }
                fail(baseOff + pos, lineNo, pos + 1, "',' or '}'");
            }
        }

        JsonElement parseFlowSeq(const std::string& s, size_t& pos, size_t baseOff, size_t lineNo) {
            JsonArray arr;
            ++pos; // '['
            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
            if (pos < s.size() && s[pos] == ']') { ++pos; return arr; }
            while (true) {
                while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
                if (pos >= s.size()) fail(baseOff + pos, lineNo, pos + 1, "']'");
                if (s[pos] == ']') { ++pos; return arr; }
                arr.push_back(parseFlowValue(s, pos, baseOff, lineNo));
                while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
                if (pos >= s.size()) fail(baseOff + pos, lineNo, pos + 1, "']'");
                if (s[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (s[pos] == ']') { ++pos; return arr; }
                fail(baseOff + pos, lineNo, pos + 1, "',' or ']'");
            }
        }
    };

    // ---------------- 行预处理 ----------------
    // 去行内注释：仅处理引号外的 " #"（# 前为空白）
    void stripInlineComment(std::string& content) {
        char quote = 0;
        for (size_t i = 0; i < content.size(); ++i) {
            char c = content[i];
            if (quote) {
                if (c == quote) {
                    if (quote == '\'' && i + 1 < content.size() && content[i + 1] == '\'') { ++i; continue; }
                    quote = 0;
                }
                continue;
            }
            if (c == '\'' || c == '"') { quote = c; continue; }
            if (c == '#' && (i == 0 || content[i - 1] == ' ')) {
                content.resize(i);
                break;
            }
        }
        rtrim(content);
    }

    std::vector<Line> buildLines(const std::string& text) {
        std::vector<Line> lines;
        size_t off = 0, n = text.size();
        size_t lineNo = 1;
        while (off < n) {
            size_t lineStart = off;
            size_t eol = text.find('\n', off);
            std::string raw;
            if (eol == std::string::npos) { raw = text.substr(off); off = n; }
            else { raw = text.substr(off, eol - off); off = eol + 1; }
            if (!raw.empty() && raw.back() == '\r') raw.pop_back();

            size_t p = 0;
            while (p < raw.size() && raw[p] == ' ') ++p;
            if (p < raw.size() && raw[p] == '\t')
                fail(lineStart + p, lineNo, p + 1, "spaces (tab indentation not supported)");
            if (p == raw.size()) { ++lineNo; continue; }          // 空行
            if (raw[p] == '#') { ++lineNo; continue; }            // 注释行
            std::string content = raw.substr(p);
            if (content == "---" || content == "...") { ++lineNo; continue; } // 文档标记
            stripInlineComment(content);
            if (content.empty()) { ++lineNo; continue; }
            lines.push_back(Line{ static_cast<int>(p), std::move(content), lineNo, lineStart });
            ++lineNo;
        }
        return lines;
    }

} // namespace

// ============================================================================
// 公开 API
// ============================================================================
JsonElement ParseYaml(const std::string& text) {
    YamlParser parser(buildLines(text));
    return parser.parseDocument();
}

JsonElement ParseYaml(std::istream& is) {
    std::ostringstream ss;
    ss << is.rdbuf();
    return ParseYaml(ss.str());
}

JsonElement ParseYamlFile(const char* path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error(std::string("failed to open file: ") + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ParseYaml(ss.str());
}

bool TryParseYaml(const std::string& text, JsonElement& out, std::string* err) {
    try {
        out = ParseYaml(text);
        return true;
    } catch (const YamlParseError& e) {
        if (err) *err = e.what();
        return false;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

// ============================================================================
// 序列化 ToYaml
// ============================================================================
namespace {

    std::string indentStr(int level, int step) {
        return std::string(static_cast<size_t>(level > 0 ? level * step : 0), ' ');
    }

    std::string scalarToYamlText(const JsonElement& el) {
        switch (el.kind()) {
            case JsonKind::Null: return "null";
            case JsonKind::Bool: return el.getBool() ? "true" : "false";
            case JsonKind::Number: return numberToYamlText(el.numberValue());
            case JsonKind::String: {
                const std::string& s = el.getString();
                if (needsQuote(s)) { std::string o; appendJsonEscape(o, s); return o; }
                return s;
            }
            default: return "";
        }
    }

    std::string flowObject(const JsonElement& el);

    std::string flowArray(const JsonElement& el) {
        std::string out = "[";
        bool first = true;
        for (const auto& e : *el.asArray()) {
            if (!first) out += ", ";
            first = false;
            switch (e.kind()) {
                case JsonKind::Array: out += flowArray(e); break;
                case JsonKind::UnorderedObject: out += flowObject(e); break;
                case JsonKind::OrderedObject: out += flowObject(e); break;
                default: out += scalarToYamlText(e); break;
            }
        }
        out += "]";
        return out;
    }
    std::string flowObject(const JsonElement& el) {
        std::string out = "{";
        bool first = true;
        if (const UnorderedObject* u = el.asUnorderedObject()) {
            for (const auto& kv : *u) {
                if (!first) out += ", ";
                first = false;
                std::string ks;
                { std::string t; appendJsonEscape(t, kv.first); ks = t; }
                out += ks + ": ";
                const JsonElement& v = kv.second;
                if (v.isArray()) out += flowArray(v);
                else if (v.isUnorderedObject() || v.isOrderedObject()) out += flowObject(v);
                else out += scalarToYamlText(v);
            }
        } else {
            for (const auto& kv : *el.asOrderedObject()) {
                if (!first) out += ", ";
                first = false;
                std::string ks;
                { std::string t; appendJsonEscape(t, kv.first); ks = t; }
                out += ks + ": ";
                const JsonElement& v = kv.second;
                if (v.isArray()) out += flowArray(v);
                else if (v.isUnorderedObject() || v.isOrderedObject()) out += flowObject(v);
                else out += scalarToYamlText(v);
            }
        }
        out += "}";
        return out;
    }

    // 递归写一个节点。inlinePrefix：节点所在行已输出的前缀（"" 或缩进 + "- "）
    void writeNode(const JsonElement& el, std::string& out, int step, int level,
                   const std::string& inlinePrefix) {
        switch (el.kind()) {
            case JsonKind::Null:
            case JsonKind::Bool:
            case JsonKind::Number:
            case JsonKind::String:
                out += inlinePrefix + scalarToYamlText(el) + "\n";
                return;

            case JsonKind::Array: {
                const JsonArray* arr = el.asArray();
                if (arr->empty()) { out += inlinePrefix + "[]\n"; return; }
                bool first = true;
                for (const auto& e : *arr) {
                    std::string prefix = first ? (inlinePrefix + "- ") : (indentStr(level, step) + "- ");
                    first = false;
                    if (e.isArray()) {
                        out += prefix + flowArray(e) + "\n";
                    } else if (e.isUnorderedObject() || e.isOrderedObject()) {
                        writeNode(e, out, step, level, prefix);
                    } else {
                        out += prefix + scalarToYamlText(e) + "\n";
                    }
                }
                return;
            }

            case JsonKind::UnorderedObject:
            case JsonKind::OrderedObject: {
                auto writeMapping = [&](const auto& firstIt, const auto& lastIt) {
                    bool empty = (firstIt == lastIt);
                    if (empty) { out += inlinePrefix + "{}\n"; return; }
                    bool first = true;
                    for (auto it = firstIt; it != lastIt; ++it) {
                        std::string prefix = first ? inlinePrefix : indentStr(level, step);
                        first = false;
                        out += prefix;
                        appendJsonEscape(out, it->first);
                        out += ":";
                        const JsonElement& v = it->second;
                        switch (v.kind()) {
                            case JsonKind::Null:
                            case JsonKind::Bool:
                            case JsonKind::Number:
                            case JsonKind::String:
                                out += " " + scalarToYamlText(v) + "\n";
                                break;
                            case JsonKind::Array:
                                if (v.asArray()->empty()) { out += " []\n"; }
                                else { out += "\n"; writeNode(v, out, step, level + 1, indentStr(level + 1, step)); }
                                break;
                            case JsonKind::UnorderedObject:
                            case JsonKind::OrderedObject: {
                                bool oempty = (v.isUnorderedObject())
                                    ? v.asUnorderedObject()->empty()
                                    : v.asOrderedObject()->empty();
                                if (oempty) { out += " {}\n"; }
                                else { out += "\n"; writeNode(v, out, step, level + 1, indentStr(level + 1, step)); }
                                break;
                            }
                        }
                    }
                };
                if (el.isUnorderedObject()) writeMapping(el.asUnorderedObject()->begin(), el.asUnorderedObject()->end());
                else writeMapping(el.asOrderedObject()->begin(), el.asOrderedObject()->end());
                return;
            }
        }
    }

} // namespace

std::string ToYaml(const JsonElement& el, int indent) {
    int step = indent < 0 ? 0 : indent;
    std::string out;
    writeNode(el, out, step, 0, "");
    return out;
}

} // namespace JSON

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
