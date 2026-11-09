#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <stdexcept>
#include <utility>
#include <istream>
#include <iterator>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include "Number.hpp"
#include "../Util/Slice.hpp"

namespace JSON {

    // 前向声明
    class JsonElement;
    class JsonArray;
    class UnorderedObject;
    class OrderedObject;

    template<class Underlying> class JsonSliceRef;
    template<class Underlying> class JsonSliceCRef;

    enum class JsonKind {
        Null,
        Bool,
        Number,
        String,
        Array,
        UnorderedObject,
        OrderedObject
    };

    // ===================== 切片代理 JsonSliceCRef(只读) / JsonSliceRef(可写) =====================
    // 注意：这两个模板定义在其宿主容器（JsonArray / OrderedObject）之前，
    // 类体内涉及 JsonElement 的成员（toContainer / setSource / atSource 等）
    // 均为模板成员，只有被真正实例化（odr-use）时才需要 JsonElement 完整，
    // 而完整定义已在本文件后部给出，故此处只声明、不实例化。
    template<class Underlying>
    class JsonSliceCRef {
        const Underlying* m_source{ nullptr };
        Slice             m_slice;

    public:
        JsonSliceCRef(const Underlying* src, const Slice& sl) noexcept
            : m_source(src), m_slice(sl) {}

        JsonSliceCRef operator[](const Slice& sub) const {
            if (!m_source) return { nullptr, Slice{} };
            std::vector<size_t> idxs;
            for (auto p : m_slice.iter(sourceSize())) idxs.push_back(p);
            std::vector<size_t> compound;
            for (auto sp : sub.iter(idxs.size())) compound.push_back(idxs[sp]);
            return { m_source, Slice::FromIndices(compound) };
        }

        [[nodiscard]] const JsonElement& operator[](size_t idx) const {
            if (!m_source) throw std::out_of_range("slice null source");
            auto indices = collectIndices();
            if (idx >= indices.size()) throw std::out_of_range("slice index out of range");
            return atSource(indices[idx]);
        }

        [[nodiscard]] std::vector<JsonElement> toContainer() const {
            std::vector<JsonElement> out;
            if (!m_source) return out;
            out.reserve(m_slice.size());
            for (auto pos : m_slice.iter(sourceSize()))
                out.push_back(atSource(pos));
            return out;
        }

    private:
        [[nodiscard]] size_t sourceSize() const;
        [[nodiscard]] const JsonElement& atSource(size_t pos) const;
        [[nodiscard]] std::vector<size_t> collectIndices() const {
            std::vector<size_t> res;
            for (auto p : m_slice.iter(sourceSize())) res.push_back(p);
            return res;
        }
    };

    template<class Underlying>
    class JsonSliceRef {
        Underlying* m_source{ nullptr };
        Slice       m_slice;

    public:
        JsonSliceRef(Underlying* src, const Slice& sl) noexcept
            : m_source(src), m_slice(sl) {}

        JsonSliceRef& operator=(const std::vector<JsonElement>& src) {
            if (!m_source) return *this;
            size_t srcIdx = 0;
            for (auto pos : m_slice.iter(sourceSize())) {
                if (srcIdx >= src.size()) break;
                setSource(pos, src[srcIdx++]);
            }
            return *this;
        }

        JsonSliceRef operator[](const Slice& sub) const {
            if (!m_source) return { nullptr, Slice{} };
            std::vector<size_t> idxs;
            for (auto p : m_slice.iter(sourceSize())) idxs.push_back(p);
            std::vector<size_t> compound;
            for (auto sp : sub.iter(idxs.size())) compound.push_back(idxs[sp]);
            return { m_source, Slice::FromIndices(compound) };
        }

        [[nodiscard]] JsonElement& operator[](size_t idx) {
            if (!m_source) throw std::out_of_range("slice null source");
            auto indices = collectIndices();
            if (idx >= indices.size()) throw std::out_of_range("slice index out of range");
            return atSource(indices[idx]);
        }

        [[nodiscard]] JsonSliceCRef<Underlying> asConst() const noexcept {
            return { m_source, m_slice };
        }

        [[nodiscard]] std::vector<JsonElement> toContainer() {
            std::vector<JsonElement> out;
            if (!m_source) return out;
            out.reserve(m_slice.size());
            for (auto pos : m_slice.iter(sourceSize()))
                out.push_back(atSource(pos));
            return out;
        }

    private:
        [[nodiscard]] size_t sourceSize() const;
        [[nodiscard]] JsonElement& atSource(size_t pos);
        void setSource(size_t pos, const JsonElement& val);
        [[nodiscard]] std::vector<size_t> collectIndices() const {
            std::vector<size_t> res;
            for (auto p : m_slice.iter(sourceSize())) res.push_back(p);
            return res;
        }
    };

    // ===================== JsonArray：组合vector，不继承，全部接口手动转发 =====================
    // 成员 std::vector<JsonElement> 使用尚不完整的 JsonElement 作为元素类型，
    // 这是 C++17 允许的（元素类型在使用前必须完整，见本文件后部 JsonElement 定义）。
    class JsonArray {
    private:
        std::vector<JsonElement> m_impl;
    public:
        // 构造
        JsonArray() = default;
        explicit JsonArray(size_t n) : m_impl(n) {}
        JsonArray(size_t n, const JsonElement& v) : m_impl(n, v) {}
        JsonArray(std::initializer_list<JsonElement> il) : m_impl(il) {}
        JsonArray(std::vector<JsonElement> v) : m_impl(std::move(v)) {}

        // 转发容器接口
        [[nodiscard]] size_t size() const noexcept { return m_impl.size(); }
        [[nodiscard]] bool empty() const noexcept { return m_impl.empty(); }
        void clear() { m_impl.clear(); }
        void reserve(size_t cap) { m_impl.reserve(cap); }
        void resize(size_t sz) { m_impl.resize(sz); }
        void resize(size_t sz, const JsonElement& fill) { m_impl.resize(sz, fill); }

        JsonElement& operator[](size_t i) { return m_impl[i]; }
        [[nodiscard]] const JsonElement& operator[](size_t i) const { return m_impl[i]; }
        JsonElement& at(size_t i) { return m_impl.at(i); }
        [[nodiscard]] const JsonElement& at(size_t i) const { return m_impl.at(i); }

        JsonElement& front() { return m_impl.front(); }
        [[nodiscard]] const JsonElement& front() const { return m_impl.front(); }
        JsonElement& back() { return m_impl.back(); }
        [[nodiscard]] const JsonElement& back() const { return m_impl.back(); }

        void push_back(const JsonElement& v) { m_impl.push_back(v); }
        void push_back(JsonElement&& v) { m_impl.push_back(std::move(v)); }
        template<typename... Args>
        JsonElement& emplace_back(Args&&... args) { return m_impl.emplace_back(std::forward<Args>(args)...); }
        void pop_back() { m_impl.pop_back(); }

        auto begin() noexcept { return m_impl.begin(); }
        auto end() noexcept { return m_impl.end(); }
        [[nodiscard]] auto begin() const noexcept { return m_impl.begin(); }
        [[nodiscard]] auto end() const noexcept { return m_impl.end(); }
        [[nodiscard]] auto cbegin() const noexcept { return m_impl.cbegin(); }
        [[nodiscard]] auto cend() const noexcept { return m_impl.cend(); }

        // 切片代理重载
        JsonSliceRef<JsonArray> operator[](const Slice& sl) {
            return { this, sl };
        }
        [[nodiscard]] JsonSliceCRef<JsonArray> operator[](const Slice& sl) const {
            return { this, sl };
        }
    };

    // ===================== UnorderedObject RFC‑8259标准无序对象 =====================
    class UnorderedObject {
    public:
        using Map = std::unordered_map<std::string, JsonElement>;
    private:
        Map m_map;
    public:
        UnorderedObject() = default;

        [[nodiscard]] size_t size() const noexcept { return m_map.size(); }
        [[nodiscard]] bool empty() const noexcept { return m_map.empty(); }

        JsonElement& operator[](const std::string& key) { return m_map[key]; }

        auto find(const std::string& key) { return m_map.find(key); }
        [[nodiscard]] auto find(const std::string& key) const { return m_map.find(key); }
        [[nodiscard]] bool contains(const std::string& key) const { return m_map.count(key) != 0; }

        void erase(const std::string& key) { m_map.erase(key); }
        void clear() { m_map.clear(); }

        auto begin() noexcept { return m_map.begin(); }
        auto end() noexcept { return m_map.end(); }
        [[nodiscard]] auto begin() const noexcept { return m_map.begin(); }
        [[nodiscard]] auto end() const noexcept { return m_map.end(); }
        [[nodiscard]] auto cbegin() const noexcept { return m_map.cbegin(); }
        [[nodiscard]] auto cend() const noexcept { return m_map.cend(); }

        [[nodiscard]] const Map& underlyingMap() const noexcept { return m_map; }
        Map& underlyingMap() noexcept { return m_map; }
    };

    // ===================== OrderedObject 有序扩展对象，墓碑惰性删除 =====================
    // 存储采用并行数组（key / value / 删除标记），避免在 JsonElement 完整定义之前
    // 直接持有包含 JsonElement 的结构体，从而打破循环依赖。
    // 有效下标缓存 m_valid 采用“脏标记 + 惰性重建”：insert/erase 只标记脏，
    // 仅在 at(idx) 真正需要时重建一次，使常规插入/删除保持摊还 O(1)，
    // 构建 N 个元素的对象从 O(N^2) 降为 O(N)。
    class OrderedObject {
    public:
        // 迭代元素为 (key, value) 对，顺序为插入顺序
        class iterator;
        class const_iterator;

    private:
        std::vector<std::string>              m_keys;
        std::vector<JsonElement>              m_values;
        std::vector<unsigned char>            m_deleted;        // 并行墓碑标记
        std::unordered_map<std::string, size_t> m_keyIndex;
        std::vector<size_t>                   m_valid;          // 有效下标缓存（惰性）
        bool                                  m_validDirty{ true };
        size_t                                m_tombstoneCount{ 0U };
        static constexpr size_t TOMBSTONE_TRIGGER_RATIO = 4U; // >25%墓碑触发compact

        [[nodiscard]] size_t effectiveSize() const noexcept { return m_values.size() - m_tombstoneCount; }
        void ensureValidCache();
        void compact();

    public:
        OrderedObject() = default;

        [[nodiscard]] size_t size() const noexcept { return effectiveSize(); }
        [[nodiscard]] bool empty() const noexcept { return size() == 0U; }

        JsonElement& operator[](const std::string& key);
        [[nodiscard]] bool contains(const std::string& key) const { return m_keyIndex.count(key); }
        void erase(const std::string& key);
        iterator erase(iterator first, iterator last);   // 范围删除 [first, last)
        void clear();

        // find：返回迭代器（未命中返回 end()）
        iterator find(const std::string& key);
        [[nodiscard]] const_iterator find(const std::string& key) const;

        JsonElement& at(size_t idx);
        [[nodiscard]] const JsonElement& at(size_t idx) const;

        // 切片代理重载
        JsonSliceRef<OrderedObject> operator[](const Slice& sl) {
            return { this, sl };
        }
        [[nodiscard]] JsonSliceCRef<OrderedObject> operator[](const Slice& sl) const {
            return { this, sl };
        }

        iterator begin() { return { this, 0U }; }
        iterator end() { return { this, m_values.size() }; }
        [[nodiscard]] const_iterator begin() const;
        [[nodiscard]] const_iterator end() const;

        // 反向迭代（跳过墓碑，手写适配避免 std::reverse_iterator 对 pair 引用的限制）
        class reverse_iterator;
        class const_reverse_iterator;
        reverse_iterator rbegin() { return reverse_iterator(this, m_values.size()); }
        reverse_iterator rend() { return reverse_iterator(this, 0U); }
        [[nodiscard]] const_reverse_iterator rbegin() const { return const_reverse_iterator(this, m_values.size()); }
        [[nodiscard]] const_reverse_iterator rend() const { return const_reverse_iterator(this, 0U); }

        // ===================== 迭代器（跳过墓碑，双向） =====================
        class iterator {
            OrderedObject* m_obj{ nullptr };
            size_t m_pos{ 0 };
            void advance();
        public:
            using value_type        = std::pair<const std::string, JsonElement>;
            using reference         = std::pair<const std::string&, JsonElement&>;
            using difference_type   = std::ptrdiff_t;
            using iterator_category = std::bidirectional_iterator_tag;

            iterator(OrderedObject* o, size_t p) : m_obj(o), m_pos(p) { advance(); }
            reference operator*();
            iterator& operator++();
            iterator& operator--();
            bool operator==(const iterator& other) const { return m_pos == other.m_pos; }
            bool operator!=(const iterator& other) const { return m_pos != other.m_pos; }
            [[nodiscard]] size_t index() const { return m_pos; }   // 底层槽位（诊断/调试用）

            // arrow 代理：解引用 pair 引用，->first / ->second 可用
            class arrow_proxy {
                reference m_r;
            public:
                explicit arrow_proxy(reference r) : m_r(r) {}
                std::pair<const std::string&, JsonElement&>* operator->() { return &m_r; }
            };
            arrow_proxy operator->() { return arrow_proxy(operator*()); }
        };
        class const_iterator {
            const OrderedObject* m_obj{ nullptr };
            size_t m_pos{ 0 };
            void advance();
        public:
            using value_type        = std::pair<const std::string, JsonElement>;
            using reference         = std::pair<const std::string&, const JsonElement&>;
            using difference_type   = std::ptrdiff_t;
            using iterator_category = std::bidirectional_iterator_tag;

            const_iterator(const OrderedObject* o, size_t p) : m_obj(o), m_pos(p) { advance(); }
            reference operator*() const;
            const_iterator& operator++();
            const_iterator& operator--();
            bool operator==(const const_iterator& other) const { return m_pos == other.m_pos; }
            bool operator!=(const const_iterator& other) const { return m_pos != other.m_pos; }
            [[nodiscard]] size_t index() const { return m_pos; }

            class arrow_proxy {
                reference m_r;
            public:
                explicit arrow_proxy(reference r) : m_r(r) {}
                std::pair<const std::string&, const JsonElement&>* operator->() { return &m_r; }
            };
            arrow_proxy operator->() const { return arrow_proxy(operator*()); }
        };

        // 反向迭代器：m_next 指向下一个待解引用槽位（SIZE_MAX = 开头之前的哨兵/rend）
        class reverse_iterator {
            const OrderedObject* m_obj{ nullptr };
            size_t m_pos{ 0 };     // 已遍历到的位置（不含）
            size_t m_next{ 0 };    // 解引用目标槽位；SIZE_MAX 表示已结束
            void findNext() {
                m_next = (std::numeric_limits<size_t>::max)();
                if (m_pos > 0U) {
                    size_t p = m_pos;
                    do { --p; } while (p > 0U && m_obj->m_deleted[p]);
                    if (!m_obj->m_deleted[p]) m_next = p;
                }
            }
        public:
            using value_type      = std::pair<const std::string, JsonElement>;
            using reference       = std::pair<const std::string&, JsonElement&>;
            using difference_type = std::ptrdiff_t;

            reverse_iterator(const OrderedObject* o, size_t p) : m_obj(o), m_pos(p) { findNext(); }
            reference operator*() const {
                return { m_obj->m_keys[m_next],
                         const_cast<OrderedObject*>(m_obj)->m_values[m_next] };
            }
            reverse_iterator& operator++() { m_pos = m_next; findNext(); return *this; }
            bool operator==(const reverse_iterator& other) const { return m_next == other.m_next; }
            bool operator!=(const reverse_iterator& other) const { return m_next != other.m_next; }
            [[nodiscard]] size_t index() const { return m_next; }

            class arrow_proxy {
                reference m_r;
            public:
                explicit arrow_proxy(reference r) : m_r(r) {}
                std::pair<const std::string&, JsonElement&>* operator->() { return &m_r; }
            };
            arrow_proxy operator->() const { return arrow_proxy(operator*()); }
        };
        class const_reverse_iterator {
            const OrderedObject* m_obj{ nullptr };
            size_t m_pos{ 0 };
            size_t m_next{ 0 };
            void findNext() {
                m_next = (std::numeric_limits<size_t>::max)();
                if (m_pos > 0U) {
                    size_t p = m_pos;
                    do { --p; } while (p > 0U && m_obj->m_deleted[p]);
                    if (!m_obj->m_deleted[p]) m_next = p;
                }
            }
        public:
            using value_type      = std::pair<const std::string, JsonElement>;
            using reference       = std::pair<const std::string&, const JsonElement&>;
            using difference_type = std::ptrdiff_t;

            const_reverse_iterator(const OrderedObject* o, size_t p) : m_obj(o), m_pos(p) { findNext(); }
            reference operator*() const {
                return { m_obj->m_keys[m_next], m_obj->m_values[m_next] };
            }
            const_reverse_iterator& operator++() { m_pos = m_next; findNext(); return *this; }
            bool operator==(const const_reverse_iterator& other) const { return m_next == other.m_next; }
            bool operator!=(const const_reverse_iterator& other) const { return m_next != other.m_next; }
            [[nodiscard]] size_t index() const { return m_next; }

            class arrow_proxy {
                reference m_r;
            public:
                explicit arrow_proxy(reference r) : m_r(r) {}
                std::pair<const std::string&, const JsonElement&>* operator->() { return &m_r; }
            };
            arrow_proxy operator->() const { return arrow_proxy(operator*()); }
        };
    };

    // ===================== 序列化增强：PrettyOptions =====================
    // 美化输出配置（JsonElement::dump / SerializeTo 使用）
    struct PrettyOptions {
        std::string indentStr = "  ";        // 每级缩进字符串（默认 2 空格）
        bool        sortKeys = false;        // 按键排序输出（对 Ordered/Unordered 对象均生效）
        bool        emptyContainerCompact = true; // 空容器单行输出 {} / []（false 时多行展开）
        int         indent = 2;              // <0 表示紧凑输出（忽略 indentStr）
    };

    // ===================== JsonElement 值语义变体 =====================
    using JsonVariant = std::variant<
        std::nullptr_t,
        bool,
        Number,
        std::string,
        JsonArray,
        UnorderedObject,
        OrderedObject
    >;

    class JsonElement {
    private:
        JsonVariant m_data;
    public:
        // 构造
        JsonElement() : m_data(nullptr) {}
        JsonElement(std::nullptr_t) : m_data(nullptr) {}
        JsonElement(bool v) : m_data(v) {}
        // 数值统一存入 Number（整数按 int64/uint64 保真，浮点提升为 long double）
        JsonElement(Number v) : m_data(std::move(v)) {}
        JsonElement(double v) : m_data(Number(v)) {}
        JsonElement(long double v) : m_data(Number(v)) {}
        JsonElement(int v) : m_data(Number(v)) {}
        JsonElement(long long v) : m_data(Number(v)) {}
        JsonElement(unsigned v) : m_data(Number(v)) {}
        JsonElement(unsigned long long v) : m_data(Number(v)) {}
        // 字符串字面量按 string 存储，避免被误判为 bool
        JsonElement(const char* v) : m_data(std::string(v)) {}
        JsonElement(std::string v) : m_data(std::move(v)) {}
        JsonElement(JsonArray v) : m_data(std::move(v)) {}
        JsonElement(UnorderedObject v) : m_data(std::move(v)) {}
        JsonElement(OrderedObject v) : m_data(std::move(v)) {}

        // 拷贝 / 移动构造
        JsonElement(const JsonElement&) = default;
        JsonElement(JsonElement&&) noexcept = default;

        // ========== 赋值运算符 ==========
        JsonElement& operator=(const JsonElement&) = default;
        JsonElement& operator=(JsonElement&&) noexcept = default;

        JsonElement& operator=(std::nullptr_t) {
            m_data = nullptr;
            return *this;
        }
        JsonElement& operator=(bool v) {
            m_data = v;
            return *this;
        }
        JsonElement& operator=(Number v) {
            m_data = std::move(v);
            return *this;
        }
        JsonElement& operator=(double v) {
            m_data = Number(v);
            return *this;
        }
        JsonElement& operator=(long double v) {
            m_data = Number(v);
            return *this;
        }
        JsonElement& operator=(int v) {
            m_data = Number(v);
            return *this;
        }
        JsonElement& operator=(long long v) {
            m_data = Number(v);
            return *this;
        }
        JsonElement& operator=(unsigned v) {
            m_data = Number(v);
            return *this;
        }
        JsonElement& operator=(unsigned long long v) {
            m_data = Number(v);
            return *this;
        }
        JsonElement& operator=(const char* v) {
            m_data = std::string(v);
            return *this;
        }
        JsonElement& operator=(std::string v) {
            m_data = std::move(v);
            return *this;
        }
        JsonElement& operator=(JsonArray v) {
            m_data = std::move(v);
            return *this;
        }
        JsonElement& operator=(UnorderedObject v) {
            m_data = std::move(v);
            return *this;
        }
        JsonElement& operator=(OrderedObject v) {
            m_data = std::move(v);
            return *this;
        }

        JsonKind kind() const noexcept;

        [[nodiscard]] bool CheckKind(JsonKind kd) const noexcept { return kind() == kd; }
        [[nodiscard]] bool isNull() const noexcept { return kind() == JsonKind::Null; }
        [[nodiscard]] bool isBool() const noexcept { return kind() == JsonKind::Bool; }
        [[nodiscard]] bool isNumber() const noexcept { return kind() == JsonKind::Number; }
        [[nodiscard]] bool isString() const noexcept { return kind() == JsonKind::String; }
        [[nodiscard]] bool isArray() const noexcept { return kind() == JsonKind::Array; }
        [[nodiscard]] bool isUnorderedObject() const noexcept { return kind() == JsonKind::UnorderedObject; }
        [[nodiscard]] bool isOrderedObject() const noexcept { return kind() == JsonKind::OrderedObject; }

        JsonArray* asArray();
        [[nodiscard]] const JsonArray* asArray() const;
        UnorderedObject* asUnorderedObject();
        [[nodiscard]] const UnorderedObject* asUnorderedObject() const;
        OrderedObject* asOrderedObject();
        [[nodiscard]] const OrderedObject* asOrderedObject() const;

        // 链式下标访问：支持 obj["a"]["b"]、arr[0]["x"] 式写法。
        // 非 const 版本：对象用 key、数组用下标；对 null 会原地升级为对应容器并创建子元素
        // （如 obj["post"] 返回 null 后 ["Invalid"] 会把该 null 升为 UnorderedObject 并插入 "Invalid"）。
        // const 版本只读：类型不符或 key 不存在抛 std::out_of_range / std::runtime_error，不会修改。
        JsonElement& operator[](const std::string& key);
        JsonElement& operator[](size_t idx);
        [[nodiscard]] const JsonElement& operator[](const std::string& key) const;
        [[nodiscard]] const JsonElement& operator[](size_t idx) const;

        [[nodiscard]] bool getBool() const;
        [[nodiscard]] double getNumber() const;
        [[nodiscard]] const std::string& getString() const;

        // ========== 数值精确读取（Number 保真） ==========
        [[nodiscard]] const Number& numberValue() const;      // 直接取 Number（非数值抛）
        [[nodiscard]] std::int64_t getInt64() const;          // 整数精确转 int64（溢出抛）
        [[nodiscard]] std::uint64_t getUInt64() const;        // 整数精确转 uint64（溢出抛）
        [[nodiscard]] int getInt() const;                     // 便捷：按 int 截取（溢出抛）
        [[nodiscard]] long double getLongDouble() const;      // 数值按 long double 返回
        [[nodiscard]] const std::string& getBigIntString() const; // 大整数原字符串（非大整数抛）

        // ========== 便利 API ==========
        // 对象按 key 取值；key 缺失或自身不是对象时返回 def（不修改、不抛）
        [[nodiscard]] const JsonElement& value(const std::string& key, const JsonElement& def) const;
        // 字符串安全读取：非 string 返回 def
        [[nodiscard]] std::string getStringOr(const std::string& def) const;
        // 序列化别名（紧凑 / 美化 / 自定义配置）
        [[nodiscard]] std::string dump(int indent = -1) const;
        [[nodiscard]] std::string dump(const PrettyOptions& opt) const;
    };

    // ===================== 容器切片代理特化 =====================
    // 特化体引用 JsonElement，故必须位于 JsonElement 完整定义之后。
    template<>
    [[nodiscard]] inline size_t JsonSliceRef<JsonArray>::sourceSize() const { return m_source->size(); }
    template<>
    [[nodiscard]] inline JsonElement& JsonSliceRef<JsonArray>::atSource(size_t pos) { return (*m_source)[pos]; }
    template<>
    inline void JsonSliceRef<JsonArray>::setSource(size_t pos, const JsonElement& val) { (*m_source)[pos] = val; }

    template<>
    [[nodiscard]] inline size_t JsonSliceCRef<JsonArray>::sourceSize() const { return m_source->size(); }
    template<>
    [[nodiscard]] inline const JsonElement& JsonSliceCRef<JsonArray>::atSource(size_t pos) const { return (*m_source)[pos]; }

    template<>
    [[nodiscard]] inline size_t JsonSliceRef<OrderedObject>::sourceSize() const { return m_source->size(); }
    template<>
    [[nodiscard]] inline JsonElement& JsonSliceRef<OrderedObject>::atSource(size_t pos) { return m_source->at(pos); }
    template<>
    inline void JsonSliceRef<OrderedObject>::setSource(size_t pos, const JsonElement& val) { m_source->at(pos) = val; }

    template<>
    [[nodiscard]] inline size_t JsonSliceCRef<OrderedObject>::sourceSize() const { return m_source->size(); }
    template<>
    [[nodiscard]] inline const JsonElement& JsonSliceCRef<OrderedObject>::atSource(size_t pos) const { return m_source->at(pos); }

    // ===================== 解析选项 =====================
    enum class ParseOption : unsigned int {
        None = 0,
        KeepObjectOrder = 1U << 0, // 开启产出 OrderedObject；默认 UnorderedObject
        Lenient        = 1U << 1, // 非严格模式：容忍 // /* */ # 注释 与 尾逗号
    };

    constexpr ParseOption operator|(ParseOption a, ParseOption b) noexcept {
        return static_cast<ParseOption>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }
    constexpr bool hasFlag(ParseOption opt, ParseOption flag) noexcept {
        return (static_cast<unsigned>(opt) & static_cast<unsigned>(flag)) != 0U;
    }

    // ===================== 对象显式互转 =====================
    UnorderedObject ToUnordered(const OrderedObject& src);
    OrderedObject ToOrdered(const UnorderedObject& src);

    // ===================== 解析 / 序列化 =====================
    // 解析错误：携带出错字节偏移、1 起始行列号与期望符号。
    // what() 保持 "JSON parse error at offset N: expected X" 格式以兼容旧调用方。
    class ParseError : public std::runtime_error {
    public:
        size_t      offset;    // 出错字节偏移（0 起始）
        size_t      line;      // 1 起始行号
        size_t      column;    // 1 起始列号（按字节）
        std::string expected;  // 期望的符号/类别描述

        ParseError(size_t off, size_t ln, size_t col, std::string exp, std::string msg)
            : std::runtime_error(std::move(msg)),
              offset(off), line(ln), column(col), expected(std::move(exp)) {}
    };

    // 解析 RFC 8259 JSON。失败时抛出 ParseError（what 含出错位置与行列）。
    JsonElement Parse(const std::string& text, ParseOption opt = ParseOption::None);

    // 流式 / 文件解析：整读输入后按同一解析器处理（当前为整读后解析，
    // 真正逐 token 的增量拉取留作后续增量解析演进）。
    JsonElement Parse(std::istream& is, ParseOption opt = ParseOption::None);
    JsonElement ParseFile(const char* path, ParseOption opt = ParseOption::None);

    // 非抛出版本；失败返回 false，可通过 err 获取原因（含行列信息）。
    bool TryParse(const std::string& text, JsonElement& out, std::string* err = nullptr);

    // 收集式解析：目前递归下降解析器遇第一个错误即停，issues 至多一条；
    // 接口预留多错误收集（随错误恢复策略落地）。
    bool TryParseIssues(const std::string& text, JsonElement& out, std::vector<ParseError>& issues);

    // 序列化为 JSON 文本。indent < 0 输出紧凑格式；indent >= 0 输出带缩进的美化格式。
    std::string Serialize(const JsonElement& el, int indent = -1);

    // ===================== 序列化增强：SerializeTo / 文件输出 =====================

    // 序列化到已有 buffer（复用容量，适合循环调用）
    void SerializeTo(const JsonElement& el, std::string& out, int indent = -1);
    void SerializeTo(const JsonElement& el, std::string& out, const PrettyOptions& opt);

    // 序列化到 FILE* / 文件路径；失败返回 false
    bool SerializeTo(FILE* f, const JsonElement& el, int indent = -1);
    bool SerializeTo(FILE* f, const JsonElement& el, const PrettyOptions& opt);
    bool SerializeToFile(const JsonElement& el, const char* path, int indent = -1);
    bool SerializeToFile(const JsonElement& el, const char* path, const PrettyOptions& opt);

} // namespace JSON
