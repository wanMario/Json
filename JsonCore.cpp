#include "JsonCore.hpp"
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cmath>
#include <limits>
#include <charconv>

namespace JSON {

    // -------- OrderedObject --------

    void OrderedObject::ensureValidCache() {
        if (!m_validDirty) return;
        m_valid.clear();
        m_valid.reserve(effectiveSize());
        for (size_t i = 0; i < m_values.size(); ++i) {
            if (!m_deleted[i])
                m_valid.push_back(i);
        }
        m_validDirty = false;
    }

    void OrderedObject::compact() {
        std::vector<std::string>        newKeys;
        std::vector<JsonElement>        newValues;
        std::vector<unsigned char>      newDeleted;
        std::unordered_map<std::string, size_t> newIndex;

        newKeys.reserve(effectiveSize());
        newValues.reserve(effectiveSize());
        newIndex.reserve(effectiveSize() * 2U);

        for (size_t i = 0; i < m_keys.size(); ++i) {
            if (m_deleted[i]) continue;
            newIndex.emplace(m_keys[i], newKeys.size());
            newKeys.push_back(std::move(m_keys[i]));
            newValues.push_back(std::move(m_values[i]));
        }
        m_keys.swap(newKeys);
        m_values.swap(newValues);
        m_keyIndex.swap(newIndex);
        m_deleted.assign(m_values.size(), 0U);
        m_tombstoneCount = 0U;
        m_validDirty = true;
    }

    JsonElement& OrderedObject::operator[](const std::string& key) {
        auto it = m_keyIndex.find(key);
        if (it != m_keyIndex.end()) {
            size_t idx = it->second;
            if (!m_deleted[idx])
                return m_values[idx];
            // 墓碑复用：直接复活该槽位，无需重建缓存
            m_deleted[idx] = 0U;
            --m_tombstoneCount;
            m_validDirty = true;
            return m_values[idx];
        }
        m_keyIndex.emplace(key, m_values.size());
        m_keys.push_back(key);
        m_values.emplace_back();   // 默认 null
        m_deleted.push_back(0U);
        m_validDirty = true;
        return m_values.back();
    }

    void OrderedObject::erase(const std::string& key) {
        auto it = m_keyIndex.find(key);
        if (it == m_keyIndex.end()) return;
        size_t pos = it->second;
        if (m_deleted[pos]) return;

        m_deleted[pos] = 1U;
        ++m_tombstoneCount;
        m_keyIndex.erase(it);
        m_validDirty = true;

        if (m_tombstoneCount * TOMBSTONE_TRIGGER_RATIO >= m_keys.size())
            compact();
    }

    void OrderedObject::clear() {
        m_keys.clear();
        m_values.clear();
        m_deleted.clear();
        m_keyIndex.clear();
        m_valid.clear();
        m_validDirty = false;
        m_tombstoneCount = 0U;
    }

    OrderedObject::iterator OrderedObject::find(const std::string& key) {
        auto it = m_keyIndex.find(key);
        if (it == m_keyIndex.end()) return end();
        return { this, it->second };
    }

    OrderedObject::const_iterator OrderedObject::find(const std::string& key) const {
        auto it = m_keyIndex.find(key);
        if (it == m_keyIndex.end()) return end();
        return { this, it->second };
    }

    // 范围删除 [first, last)：将区间内有效项标记墓碑并从索引移除；返回 last
    OrderedObject::iterator OrderedObject::erase(iterator first, iterator last) {
        if (!(first.index() <= last.index()) || last.index() > m_values.size())
            return end();
        size_t p = first.index();
        const size_t stop = last.index();
        while (p < stop) {
            if (!m_deleted[p]) {
                m_deleted[p] = 1U;
                ++m_tombstoneCount;
                m_keyIndex.erase(m_keys[p]);
            }
            ++p;
        }
        m_validDirty = true;
        if (m_tombstoneCount * TOMBSTONE_TRIGGER_RATIO >= m_keys.size())
            compact();
        return last;
    }

    JsonElement& OrderedObject::at(size_t idx) {
        if (idx >= size())
            throw std::out_of_range("OrderedObject::at index out of range");
        ensureValidCache();
        return m_values[m_valid[idx]];
    }

    const JsonElement& OrderedObject::at(size_t idx) const {
        return const_cast<OrderedObject*>(this)->at(idx);
    }

    // iterator
    void OrderedObject::iterator::advance() {
        while (m_obj && m_pos < m_obj->m_values.size() && m_obj->m_deleted[m_pos])
            m_pos++;
    }

    auto OrderedObject::iterator::operator*() -> std::pair<const std::string&, JsonElement&> {
        return { m_obj->m_keys[m_pos], m_obj->m_values[m_pos] };
    }

    auto OrderedObject::iterator::operator++() -> iterator& {
        m_pos++;
        advance();
        return *this;
    }

    auto OrderedObject::iterator::operator--() -> iterator& {
        while (m_pos > 0U) {
            --m_pos;
            if (!m_obj->m_deleted[m_pos]) break;
        }
        return *this;
    }

    void OrderedObject::const_iterator::advance() {
        while (m_obj && m_pos < m_obj->m_values.size() && m_obj->m_deleted[m_pos])
            m_pos++;
    }

    auto OrderedObject::const_iterator::operator*() const -> std::pair<const std::string&, const JsonElement&> {
        return { m_obj->m_keys[m_pos], m_obj->m_values[m_pos] };
    }

    auto OrderedObject::const_iterator::operator++() -> const_iterator& {
        m_pos++;
        advance();
        return *this;
    }

    auto OrderedObject::const_iterator::operator--() -> const_iterator& {
        while (m_pos > 0U) {
            --m_pos;
            if (!m_obj->m_deleted[m_pos]) break;
        }
        return *this;
    }

    OrderedObject::const_iterator OrderedObject::begin() const { return { this, 0U }; }
    OrderedObject::const_iterator OrderedObject::end() const { return { this, m_values.size() }; }

    // -------- JsonElement --------
    JsonKind JsonElement::kind() const noexcept {
        struct Visitor {
            JsonKind operator()(std::nullptr_t) const { return JsonKind::Null; }
            JsonKind operator()(bool) const { return JsonKind::Bool; }
            JsonKind operator()(const Number&) const { return JsonKind::Number; }
            JsonKind operator()(const std::string&) const { return JsonKind::String; }
            JsonKind operator()(const JsonArray&) const { return JsonKind::Array; }
            JsonKind operator()(const UnorderedObject&) const { return JsonKind::UnorderedObject; }
            JsonKind operator()(const OrderedObject&) const { return JsonKind::OrderedObject; }
        };
        return std::visit(Visitor{}, m_data);
    }

    JsonArray* JsonElement::asArray() {
        return std::get_if<JsonArray>(&m_data);
    }
    const JsonArray* JsonElement::asArray() const {
        return std::get_if<JsonArray>(&m_data);
    }
    UnorderedObject* JsonElement::asUnorderedObject() {
        return std::get_if<UnorderedObject>(&m_data);
    }
    const UnorderedObject* JsonElement::asUnorderedObject() const {
        return std::get_if<UnorderedObject>(&m_data);
    }
    OrderedObject* JsonElement::asOrderedObject() {
        return std::get_if<OrderedObject>(&m_data);
    }
    const OrderedObject* JsonElement::asOrderedObject() const {
        return std::get_if<OrderedObject>(&m_data);
    }

    bool JsonElement::getBool() const {
        return std::get<bool>(m_data);
    }
    double JsonElement::getNumber() const {
        return numberValue().asDouble();
    }
    const std::string& JsonElement::getString() const {
        return std::get<std::string>(m_data);
    }

    // -------- 数值精确读取与便利 API --------
    const Number& JsonElement::numberValue() const {
        if (!isNumber())
            throw std::runtime_error("numberValue() on non-number JsonElement");
        return std::get<Number>(m_data);
    }

    std::int64_t JsonElement::getInt64() const { return numberValue().asInt64(); }
    std::uint64_t JsonElement::getUInt64() const { return numberValue().asUInt64(); }

    int JsonElement::getInt() const {
        const std::int64_t v = getInt64();
        if (v < INT_MIN || v > INT_MAX)
            throw std::overflow_error("Number out of int range");
        return static_cast<int>(v);
    }

    long double JsonElement::getLongDouble() const { return numberValue().asLongDouble(); }

    const std::string& JsonElement::getBigIntString() const {
        return numberValue().asBigIntString();
    }

    const JsonElement& JsonElement::value(const std::string& key, const JsonElement& def) const {
        if (isUnorderedObject()) {
            const auto* o = asUnorderedObject();
            auto it = o->find(key);
            if (it != o->end()) return it->second;
            return def;
        }
        if (isOrderedObject()) {
            for (const auto& p : *asOrderedObject())
                if (p.first == key) return p.second;
        }
        return def;
    }

    std::string JsonElement::getStringOr(const std::string& def) const {
        return isString() ? getString() : def;
    }

    std::string JsonElement::dump(int indent) const {
        return Serialize(*this, indent);
    }

    std::string JsonElement::dump(const PrettyOptions& opt) const {
        std::string out;
        SerializeTo(*this, out, opt);
        return out;
    }

    // -------- 链式下标访问 --------
    JsonElement& JsonElement::operator[](const std::string& key) {
        if (isUnorderedObject()) return (*asUnorderedObject())[key];
        if (isOrderedObject())   return (*asOrderedObject())[key];
        if (isNull()) {           // 自动升级为无序对象，支持链式构建 obj["a"]["b"] = ...
            *this = UnorderedObject{};
            return (*asUnorderedObject())[key];
        }
        throw std::runtime_error("operator[](string) on non-object JsonElement");
    }

    JsonElement& JsonElement::operator[](size_t idx) {
        if (isArray()) return (*asArray())[idx];
        if (isNull()) {           // 自动升级为数组
            *this = JsonArray{};
            return (*asArray())[idx];
        }
        throw std::runtime_error("operator[](size_t) on non-array JsonElement");
    }

    const JsonElement& JsonElement::operator[](const std::string& key) const {
        if (isUnorderedObject()) {
            auto* o = asUnorderedObject();
            auto it = o->find(key);
            if (it == o->end()) throw std::out_of_range("key not found: " + key);
            return it->second;
        }
        if (isOrderedObject()) {
            auto* o = asOrderedObject();
            for (auto&& p : *o)
                if (p.first == key) return p.second;
            throw std::out_of_range("key not found: " + key);
        }
        throw std::runtime_error("const operator[](string) on non-object JsonElement");
    }

    const JsonElement& JsonElement::operator[](size_t idx) const {
        if (isArray()) return (*asArray())[idx];
        if (isOrderedObject()) return (*asOrderedObject()).at(idx);
        throw std::runtime_error("const operator[](size_t) on non-array JsonElement");
    }

    // -------- 互转函数 --------
    UnorderedObject ToUnordered(const OrderedObject& src) {
        UnorderedObject out;
        out.underlyingMap().reserve(src.size());
        for (auto&& p : src) {
            out[p.first] = p.second;
        }
        return out;
    }

    OrderedObject ToOrdered(const UnorderedObject& src) {
        OrderedObject out;
        for (auto&& p : src) {
            out[p.first] = p.second;
        }
        return out;
    }

    // ===================== 解析器（RFC 8259 递归下降） =====================

    namespace {

        class Parser {
        public:
            Parser(const std::string& text, ParseOption opt)
                : m_text(text), m_opt(opt) {}

            JsonElement parseDocument() {
                JsonElement v = parseValue();
                skipWs();
                if (m_pos != m_text.size())
                    fail("unexpected trailing data after top-level value");
                return v;
            }

        private:
            const std::string& m_text;
            size_t             m_pos{ 0U };
            ParseOption        m_opt;

            // 失败：抛 ParseError，携带字节偏移、1 起始行列号与期望符号
            [[noreturn]] void fail(const std::string& msg, const std::string& expected = "") const {
                size_t line = 1U, col = 1U;
                const size_t n = (std::min)(m_pos, m_text.size());
                for (size_t i = 0; i < n; ++i) {
                    if (m_text[i] == '\n') { ++line; col = 1U; }
                    else ++col;
                }
                throw ParseError(m_pos, line, col, expected,
                                 "JSON parse error at offset " + std::to_string(m_pos) + ": " + msg);
            }

            void skipWs() {
                while (m_pos < m_text.size()) {
                    char c = m_text[m_pos];
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++m_pos; continue; }
                    if (m_opt == ParseOption::None) break;
                    if (m_opt == ParseOption::Lenient ||
                        (static_cast<unsigned>(m_opt) & static_cast<unsigned>(ParseOption::Lenient))) {
                        // 行注释 // 与 #
                        if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
                            m_pos += 2;
                            while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
                            continue;
                        }
                        // 块注释 /* ... */（未闭合则容忍到结尾）
                        if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '*') {
                            m_pos += 2;
                            while (m_pos + 1 < m_text.size() &&
                                   !(m_text[m_pos] == '*' && m_text[m_pos + 1] == '/')) ++m_pos;
                            if (m_pos + 1 < m_text.size()) m_pos += 2;
                            continue;
                        }
                        if (c == '#') {
                            while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
                            continue;
                        }
                    }
                    break;
                }
            }

            bool lenient() const {
                return (static_cast<unsigned>(m_opt) & static_cast<unsigned>(ParseOption::Lenient)) != 0U;
            }

            bool match(char c) {
                if (m_pos < m_text.size() && m_text[m_pos] == c) { ++m_pos; return true; }
                return false;
            }

            void expect(char c) {
                if (!match(c)) fail(std::string("expected '") + c + "'", std::string("'") + c + "'");
            }

            void expectKw(const char* kw) {
                const size_t n = std::strlen(kw);
                if (m_text.compare(m_pos, n, kw) != 0)
                    fail(std::string("expected ") + kw, kw);
                m_pos += n;
            }

            static bool isDigit(char c) { return c >= '0' && c <= '9'; }

            JsonElement parseValue() {
                skipWs();
                if (m_pos >= m_text.size()) fail("unexpected end of input");
                switch (m_text[m_pos]) {
                    case '{': return parseObject();
                    case '[': return parseArray();
                    case '"': return JsonElement(parseString());
                    case 't': expectKw("true");  return JsonElement(true);
                    case 'f': expectKw("false"); return JsonElement(false);
                    case 'n': expectKw("null");  return JsonElement(nullptr);
                    default:
                        if (m_text[m_pos] == '-' || isDigit(m_text[m_pos]))
                            return JsonElement(parseNumber());
                        fail(std::string("unexpected character '") + m_text[m_pos] + "'");
                }
            }

            JsonArray parseArray() {
                expect('[');
                JsonArray arr;
                skipWs();
                if (match(']')) return arr;
                while (true) {
                    // Lenient：容忍尾逗号（[,] 在值后直接闭括号）
                    if (lenient()) { skipWs(); if (match(']')) return arr; }
                    arr.push_back(parseValue());
                    skipWs();
                    if (match(']')) return arr;
                    expect(',');
                }
            }

            JsonElement parseObject() {
                expect('{');
                skipWs();
                if (match('}')) {
                    if (m_opt == ParseOption::KeepObjectOrder)
                        return JsonElement(OrderedObject{});
                    return JsonElement(UnorderedObject{});
                }

                if (hasFlag(m_opt, ParseOption::KeepObjectOrder)) {
                    OrderedObject obj;
                    while (true) {
                        // Lenient：容忍尾逗号（{...,} 直接闭括号）
                        if (lenient()) { skipWs(); if (match('}')) return JsonElement(std::move(obj)); }
                        skipWs();
                        if (m_pos >= m_text.size() || m_text[m_pos] != '"')
                            fail("expected object key string");
                        std::string key = parseString();
                        skipWs();
                        expect(':');
                        obj[key] = parseValue();
                        skipWs();
                        if (match('}')) return JsonElement(std::move(obj));
                        expect(',');
                    }
                } else {
                    UnorderedObject obj;
                    while (true) {
                        if (lenient()) { skipWs(); if (match('}')) return JsonElement(std::move(obj)); }
                        skipWs();
                        if (m_pos >= m_text.size() || m_text[m_pos] != '"')
                            fail("expected object key string");
                        std::string key = parseString();
                        skipWs();
                        expect(':');
                        obj[key] = parseValue();
                        skipWs();
                        if (match('}')) return JsonElement(std::move(obj));
                        expect(',');
                    }
                }
            }

            // 解析数字：整数按 int64 → uint64 → 字符串大整数 逐级保精度；
            // 含 . / e / E 的浮点用 strtold 提升为 long double。
            Number parseNumber() {
                const size_t start = m_pos;
                if (m_pos < m_text.size() && m_text[m_pos] == '-') ++m_pos;
                // 整数部分
                if (m_pos >= m_text.size()) fail("invalid number");
                if (m_text[m_pos] == '0') {
                    ++m_pos;
                } else if (isDigit(m_text[m_pos])) {
                    while (m_pos < m_text.size() && isDigit(m_text[m_pos])) ++m_pos;
                } else {
                    fail("invalid number");
                }
                bool isFloat = false;
                // 小数部分
                if (m_pos < m_text.size() && m_text[m_pos] == '.') {
                    isFloat = true;
                    ++m_pos;
                    if (m_pos >= m_text.size() || !isDigit(m_text[m_pos]))
                        fail("invalid number: expected digit after decimal point");
                    while (m_pos < m_text.size() && isDigit(m_text[m_pos])) ++m_pos;
                }
                // 指数部分
                if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
                    isFloat = true;
                    ++m_pos;
                    if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) ++m_pos;
                    if (m_pos >= m_text.size() || !isDigit(m_text[m_pos]))
                        fail("invalid number: expected digit in exponent");
                    while (m_pos < m_text.size() && isDigit(m_text[m_pos])) ++m_pos;
                }

                const char* beg = m_text.data() + start;
                const char* end = beg + (m_pos - start);

                if (!isFloat) {
                    std::int64_t i64 = 0;
                    auto r1 = std::from_chars(beg, end, i64);
                    if (r1.ec == std::errc() && r1.ptr == end) return Number(i64);
                    if (*beg != '-') {
                        std::uint64_t u64 = 0;
                        auto r2 = std::from_chars(beg, end, u64);
                        if (r2.ec == std::errc() && r2.ptr == end) return Number(u64);
                    }
                    // 超出 int64/uint64：存规范化大整数字符串
                    std::string big = m_text.substr(start, m_pos - start);
                    normalizeBigInt(big);
                    return Number(std::move(big));
                }

                // 浮点：strtold 全精度（GCC from_chars 不支持 long double）
                std::string tok = m_text.substr(start, m_pos - start);
                char* endp = nullptr;
                const long double v = std::strtold(tok.c_str(), &endp);
                if (endp == tok.c_str()) fail("invalid number");
                return Number(v);
            }

            // 规范化大整数字符串：去前导零；"-000"→"0"
            static void normalizeBigInt(std::string& s) {
                size_t i = 0;
                if (!s.empty() && s[0] == '-') i = 1;
                const size_t start = i;
                while (i < s.size() && s[i] == '0') ++i;
                if (i == s.size()) { s = "0"; return; }
                if (i > start) s.erase(start, i - start);
            }

            uint32_t parseHex4() {
                if (m_pos + 4 > m_text.size()) fail("truncated \\u escape");
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i) {
                    char c = m_text[m_pos++];
                    v <<= 4;
                    if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
                    else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
                    else fail("invalid \\u escape");
                }
                return v;
            }

            static void appendUtf8(std::string& out, uint32_t cp) {
                if (cp <= 0x7F) {
                    out.push_back(char(cp));
                } else if (cp <= 0x7FF) {
                    out.push_back(char(0xC0 | (cp >> 6)));
                    out.push_back(char(0x80 | (cp & 0x3F)));
                } else if (cp <= 0xFFFF) {
                    out.push_back(char(0xE0 | (cp >> 12)));
                    out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(char(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(char(0xF0 | (cp >> 18)));
                    out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(char(0x80 | (cp & 0x3F)));
                }
            }

            std::string parseString() {
                expect('"');
                std::string out;
                while (true) {
                    if (m_pos >= m_text.size()) fail("unterminated string");
                    char c = m_text[m_pos++];
                    if (c == '"') break;
                    if (c == '\\') {
                        if (m_pos >= m_text.size()) fail("unterminated escape");
                        char e = m_text[m_pos++];
                        switch (e) {
                            case '"':  out.push_back('"');  break;
                            case '\\': out.push_back('\\'); break;
                            case '/':  out.push_back('/');  break;
                            case 'b':  out.push_back('\b'); break;
                            case 'f':  out.push_back('\f'); break;
                            case 'n':  out.push_back('\n'); break;
                            case 'r':  out.push_back('\r'); break;
                            case 't':  out.push_back('\t'); break;
                            case 'u': {
                                uint32_t cp = parseHex4();
                                if (cp >= 0xD800 && cp <= 0xDBFF) {
                                    // 高代理，期待低代理
                                    if (m_pos + 1 < m_text.size() && m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u') {
                                        m_pos += 2;
                                        uint32_t lo = parseHex4();
                                        if (lo < 0xDC00 || lo > 0xDFFF)
                                            fail("invalid low surrogate in \\u escape");
                                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                    } else {
                                        fail("unpaired high surrogate in \\u escape");
                                    }
                                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                                    fail("unpaired low surrogate in \\u escape");
                                }
                                appendUtf8(out, cp);
                                break;
                            }
                            default: fail("invalid escape sequence");
                        }
                    } else {
                        if (static_cast<unsigned char>(c) < 0x20)
                            fail("unescaped control character in string");
                        out.push_back(c);
                    }
                }
                return out;
            }
        };

    } // namespace

    JsonElement Parse(const std::string& text, ParseOption opt) {
        Parser p(text, opt);
        return p.parseDocument();
    }

    bool TryParse(const std::string& text, JsonElement& out, std::string* err) {
        try {
            out = Parse(text);
            return true;
        } catch (const std::exception& e) {
            if (err) *err = e.what();
            return false;
        }
    }

    bool TryParseIssues(const std::string& text, JsonElement& out, std::vector<ParseError>& issues) {
        try {
            out = Parse(text);
            return true;
        } catch (const ParseError& e) {
            issues.push_back(e);
            return false;
        } catch (const std::exception& e) {
            issues.emplace_back(0U, 1U, 1U, "", e.what());
            return false;
        }
    }

    JsonElement Parse(std::istream& is, ParseOption opt) {
        std::string text;
        char buf[4096];
        while (is.good() && !is.eof()) {
            is.read(buf, sizeof(buf));
            const std::streamsize n = is.gcount();
            if (n > 0) text.append(buf, static_cast<size_t>(n));
        }
        return Parse(text, opt);
    }

    JsonElement ParseFile(const char* path, ParseOption opt) {
        if (!path) throw std::runtime_error("ParseFile: null path");
        FILE* f = std::fopen(path, "rb");
        if (!f) throw std::runtime_error(std::string("ParseFile: cannot open ") + path);
        std::string text;
        char buf[4096];
        size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
            text.append(buf, n);
        const bool err = std::ferror(f) != 0;
        std::fclose(f);
        if (err) throw std::runtime_error(std::string("ParseFile: read error on ") + path);
        return Parse(text, opt);
    }

    // ===================== 序列化 =====================

    namespace {

        // 紧凑模式是否生效：indent < 0
        inline bool compactMode(const PrettyOptions& o) { return o.indent < 0; }

        void appendIndent(std::string& out, const PrettyOptions& o, int depth) {
            for (int i = 0; i < depth; ++i) out += o.indentStr;
        }

        void appendNumber(std::string& out, const Number& v) {
            if (v.isInt()) {
                char buf[24];
                auto r = std::to_chars(buf, buf + sizeof(buf), std::get<std::int64_t>(v.m));
                out.append(buf, r.ptr);
                return;
            }
            if (v.isUInt()) {
                char buf[24];
                auto r = std::to_chars(buf, buf + sizeof(buf), std::get<std::uint64_t>(v.m));
                out.append(buf, r.ptr);
                return;
            }
            if (v.isBigInt()) {
                out += std::get<std::string>(v.m);   // 大整数原样输出（已规范化）
                return;
            }
            // long double：若可无损表示为 double 走 to_chars 最短往返；否则 %.21Lg 保精度
            const long double ld = std::get<long double>(v.m);
            const double d = static_cast<double>(ld);
            if (static_cast<long double>(d) == ld) {
                char buf[64];
                auto r = std::to_chars(buf, buf + sizeof(buf), d);
                out.append(buf, r.ptr);
            } else {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%.21Lg", ld);
                out += buf;
            }
        }

        void appendString(std::string& out, const std::string& s) {
            static const char* hex = "0123456789abcdef";
            out.push_back('"');
            for (unsigned char c : s) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b";  break;
                    case '\f': out += "\\f";  break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:
                        if (c < 0x20) {
                            out += "\\u00";
                            out.push_back(hex[c >> 4]);
                            out.push_back(hex[c & 0xF]);
                        } else {
                            out.push_back(char(c));
                        }
                }
            }
            out.push_back('"');
        }

        // 写数组 / 对象共用的美化换行逻辑
        void newlineIndent(std::string& out, const PrettyOptions& o, int depth) {
            if (compactMode(o)) return;
            out.push_back('\n');
            appendIndent(out, o, depth);
        }

        void writeJson(const JsonElement& el, std::string& out, const PrettyOptions& o, int depth);

        void writeArray(const JsonArray& a, std::string& out, const PrettyOptions& o, int depth) {
            if (a.empty()) {
                out += o.emptyContainerCompact || compactMode(o) ? "[]" : "[\n";
                if (!o.emptyContainerCompact && !compactMode(o)) { appendIndent(out, o, depth); out.push_back(']'); }
                return;
            }
            out.push_back('[');
            size_t i = 0;
            for (const auto& item : a) {
                if (i++ > 0) out.push_back(',');
                newlineIndent(out, o, depth + 1);
                writeJson(item, out, o, depth + 1);
            }
            newlineIndent(out, o, depth);
            out.push_back(']');
        }

        // 写对象：迭代任意 (key, value) 范围；排序模式下用 kvs 向量
        template <typename It>
        void writeObjectRange(It first, It last, std::string& out, const PrettyOptions& o, int depth) {
            if (first == last) {
                out += (o.emptyContainerCompact || compactMode(o)) ? "{}" : "{\n";
                if (!o.emptyContainerCompact && !compactMode(o)) { appendIndent(out, o, depth); out.push_back('}'); }
                return;
            }
            out.push_back('{');
            size_t i = 0;
            for (; first != last; ++first) {
                if (i++ > 0) out.push_back(',');
                newlineIndent(out, o, depth + 1);
                appendString(out, first->first);
                out.push_back(':');
                if (!compactMode(o)) out.push_back(' ');
                writeJson(first->second, out, o, depth + 1);
            }
            newlineIndent(out, o, depth);
            out.push_back('}');
        }

        void writeJson(const JsonElement& el, std::string& out, const PrettyOptions& o, int depth) {
            switch (el.kind()) {
                case JsonKind::Null:
                    out += "null";
                    return;
                case JsonKind::Bool:
                    out += el.getBool() ? "true" : "false";
                    return;
                case JsonKind::Number:
                    appendNumber(out, el.numberValue());
                    return;
                case JsonKind::String:
                    appendString(out, el.getString());
                    return;
                case JsonKind::Array: {
                    writeArray(*el.asArray(), out, o, depth);
                    return;
                }
                case JsonKind::UnorderedObject: {
                    const auto* src = el.asUnorderedObject();
                    if (o.sortKeys) {
                        std::vector<std::pair<std::string, JsonElement>> kvs;
                        kvs.reserve(src->size());
                        for (const auto& kv : *src) kvs.emplace_back(kv.first, kv.second);
                        std::sort(kvs.begin(), kvs.end(),
                                  [](const auto& a, const auto& b) { return a.first < b.first; });
                        writeObjectRange(kvs.begin(), kvs.end(), out, o, depth);
                    } else {
                        writeObjectRange(src->begin(), src->end(), out, o, depth);
                    }
                    return;
                }
                case JsonKind::OrderedObject: {
                    const auto* src = el.asOrderedObject();
                    if (o.sortKeys) {
                        std::vector<std::pair<std::string, JsonElement>> kvs;
                        kvs.reserve(src->size());
                        for (const auto& kv : *src) kvs.emplace_back(kv.first, kv.second);
                        std::sort(kvs.begin(), kvs.end(),
                                  [](const auto& a, const auto& b) { return a.first < b.first; });
                        writeObjectRange(kvs.begin(), kvs.end(), out, o, depth);
                    } else {
                        writeObjectRange(src->begin(), src->end(), out, o, depth);
                    }
                    return;
                }
            }
        }

    } // namespace

    std::string Serialize(const JsonElement& el, int indent) {
        std::string out;
        out.reserve(128);
        PrettyOptions o;
        o.indent = indent;
        if (indent >= 0) o.indentStr.assign(static_cast<size_t>(indent), ' ');
        writeJson(el, out, o, 0);
        return out;
    }

    void SerializeTo(const JsonElement& el, std::string& out, int indent) {
        out.clear();
        PrettyOptions o;
        o.indent = indent;
        if (indent >= 0) o.indentStr.assign(static_cast<size_t>(indent), ' ');
        writeJson(el, out, o, 0);
    }

    void SerializeTo(const JsonElement& el, std::string& out, const PrettyOptions& opt) {
        out.clear();
        writeJson(el, out, opt, 0);
    }

    bool SerializeTo(FILE* f, const JsonElement& el, int indent) {
        if (!f) return false;
        std::string buf;
        SerializeTo(el, buf, indent);
        return std::fwrite(buf.data(), 1, buf.size(), f) == buf.size();
    }

    bool SerializeTo(FILE* f, const JsonElement& el, const PrettyOptions& opt) {
        if (!f) return false;
        std::string buf;
        SerializeTo(el, buf, opt);
        return std::fwrite(buf.data(), 1, buf.size(), f) == buf.size();
    }

    bool SerializeToFile(const JsonElement& el, const char* path, int indent) {
        if (!path) return false;
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        const bool ok = SerializeTo(f, el, indent);
        std::fclose(f);
        return ok;
    }

    bool SerializeToFile(const JsonElement& el, const char* path, const PrettyOptions& opt) {
        if (!path) return false;
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        const bool ok = SerializeTo(f, el, opt);
        std::fclose(f);
        return ok;
    }

} // namespace JSON
