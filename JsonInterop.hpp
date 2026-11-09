#pragma once

// ===================== JsonInterop：容器 / 结构体互操作 =====================
// 独立的扩展头：include "JsonInterop.hpp"（内部已含 JsonCore.hpp）。
//
// 1. ToJson / FromJson 模板：内置类型 + std::vector / std::map /
//    std::unordered_map / std::optional / std::pair 自动适配；
// 2. 自定义类型：在类型所在命名空间提供 ADL 函数
//      void to_json(JsonElement& el, const T& v);
//      void from_json(const JsonElement& el, T& v);
//    即可被 ToJson/FromJson 自动发现；
// 3. 结构体反射宏 JSON_DEFINE_STRUCT(Name, a, b, c)：
//    自动生成 to_json/from_json（键名为字段名，缺失字段保持默认值）。
//    注意：字段名之间用逗号分隔，字段类型若含逗号（如模板类型）需用 typedef 包一层。

#include "JsonCore.hpp"

#include <map>
#include <optional>
#include <type_traits>

namespace JSON {

    // ---------- 声明区（ToJson/FromJson 模板依赖这些重载可见） ----------
    inline void to_json(JsonElement& el, const JsonElement& v);
    inline void to_json(JsonElement& el, std::nullptr_t);
    inline void to_json(JsonElement& el, bool v);
    inline void to_json(JsonElement& el, const char* v);
    inline void to_json(JsonElement& el, const std::string& v);
    inline void to_json(JsonElement& el, double v);
    inline void to_json(JsonElement& el, long double v);
    inline void to_json(JsonElement& el, float v);
    template <typename T,
              std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    void to_json(JsonElement& el, T v);
    template <typename T> void to_json(JsonElement& el, const std::vector<T>& v);
    template <typename K, typename V> void to_json(JsonElement& el, const std::map<K, V>& m);
    template <typename K, typename V> void to_json(JsonElement& el, const std::unordered_map<K, V>& m);
    template <typename T> void to_json(JsonElement& el, const std::optional<T>& v);
    template <typename A, typename B> void to_json(JsonElement& el, const std::pair<A, B>& p);

    inline void from_json(const JsonElement& el, JsonElement& v);
    inline void from_json(const JsonElement& el, bool& v);
    inline void from_json(const JsonElement& el, std::string& v);
    inline void from_json(const JsonElement& el, double& v);
    inline void from_json(const JsonElement& el, long double& v);
    inline void from_json(const JsonElement& el, float& v);
    inline void from_json(const JsonElement& el, int& v);
    inline void from_json(const JsonElement& el, long long& v);
    inline void from_json(const JsonElement& el, unsigned& v);
    inline void from_json(const JsonElement& el, unsigned long long& v);
    template <typename T,
              std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    void from_json(const JsonElement& el, T& v);
    template <typename T> void from_json(const JsonElement& el, std::vector<T>& v);
    template <typename K, typename V> void from_json(const JsonElement& el, std::map<K, V>& m);
    template <typename K, typename V> void from_json(const JsonElement& el, std::unordered_map<K, V>& m);
    template <typename T> void from_json(const JsonElement& el, std::optional<T>& v);
    template <typename A, typename B> void from_json(const JsonElement& el, std::pair<A, B>& p);

    // ---------- ADL 中枢 ----------
    // ToJson(v)：写 el；FromJson<T>(el)：读回 T
    // using 声明使 JSON 命名空间的全部重载可见；自定义类型经 ADL 补充。
    template <typename T>
    JsonElement ToJson(const T& v) {
        JsonElement el;
        using JSON::to_json;
        to_json(el, v);
        return el;
    }

    template <typename T>
    T FromJson(const JsonElement& el) {
        T v{};
        using JSON::from_json;
        from_json(el, v);
        return v;
    }

    // ---------- 定义区：基础类型 ----------
    inline void to_json(JsonElement& el, const JsonElement& v) { el = v; }
    inline void to_json(JsonElement& el, std::nullptr_t) { el = nullptr; }
    inline void to_json(JsonElement& el, bool v) { el = v; }
    inline void to_json(JsonElement& el, const char* v) { el = v; }
    inline void to_json(JsonElement& el, const std::string& v) { el = v; }
    inline void to_json(JsonElement& el, double v) { el = v; }
    inline void to_json(JsonElement& el, long double v) { el = v; }
    inline void to_json(JsonElement& el, float v) { el = v; }

    // 泛型整型兜底（short/char/size_t 等；具体整型重载优先，无歧义）
    template <typename T,
              std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int>>
    void to_json(JsonElement& el, T v) {
        el = v;
    }

    inline void from_json(const JsonElement& el, JsonElement& v) { v = el; }
    inline void from_json(const JsonElement& el, bool& v) { v = el.getBool(); }
    inline void from_json(const JsonElement& el, std::string& v) { v = el.getString(); }
    inline void from_json(const JsonElement& el, double& v) { v = el.getNumber(); }
    inline void from_json(const JsonElement& el, long double& v) { v = el.getLongDouble(); }
    inline void from_json(const JsonElement& el, float& v) { v = static_cast<float>(el.getNumber()); }
    inline void from_json(const JsonElement& el, int& v) { v = el.getInt(); }
    inline void from_json(const JsonElement& el, long long& v) { v = el.getInt64(); }
    inline void from_json(const JsonElement& el, unsigned& v) { v = static_cast<unsigned>(el.getUInt64()); }
    inline void from_json(const JsonElement& el, unsigned long long& v) { v = el.getUInt64(); }

    template <typename T,
              std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int>>
    void from_json(const JsonElement& el, T& v) {
        if constexpr (std::is_signed_v<T>) v = static_cast<T>(el.getInt64());
        else v = static_cast<T>(el.getUInt64());
    }

    // ---------- 定义区：容器 ----------
    template <typename T>
    void to_json(JsonElement& el, const std::vector<T>& v) {
        JsonArray arr;
        arr.reserve(v.size());
        for (const auto& x : v) arr.push_back(ToJson(x));
        el = std::move(arr);
    }
    template <typename T>
    void from_json(const JsonElement& el, std::vector<T>& v) {
        const auto* a = el.asArray();
        if (!a) throw std::runtime_error("from_json: expected array");
        v.clear();
        v.reserve(a->size());
        for (const auto& x : *a) v.push_back(FromJson<T>(x));
    }

    template <typename K, typename V>
    void to_json(JsonElement& el, const std::map<K, V>& m) {
        UnorderedObject obj;
        for (const auto& kv : m) obj[ToJson(kv.first).getString()] = ToJson(kv.second);
        el = std::move(obj);
    }
    template <typename K, typename V>
    void from_json(const JsonElement& el, std::map<K, V>& m) {
        const auto* o = el.asUnorderedObject();
        if (!o) throw std::runtime_error("from_json: expected object");
        m.clear();
        for (const auto& kv : *o) {
            K key = FromJson<K>(JsonElement(kv.first));
            m[std::move(key)] = FromJson<V>(kv.second);
        }
    }

    template <typename K, typename V>
    void to_json(JsonElement& el, const std::unordered_map<K, V>& m) {
        UnorderedObject obj;
        for (const auto& kv : m) obj[ToJson(kv.first).getString()] = ToJson(kv.second);
        el = std::move(obj);
    }
    template <typename K, typename V>
    void from_json(const JsonElement& el, std::unordered_map<K, V>& m) {
        const auto* o = el.asUnorderedObject();
        if (!o) throw std::runtime_error("from_json: expected object");
        m.clear();
        for (const auto& kv : *o) {
            K key = FromJson<K>(JsonElement(kv.first));
            m[std::move(key)] = FromJson<V>(kv.second);
        }
    }

    template <typename T>
    void to_json(JsonElement& el, const std::optional<T>& v) {
        if (v.has_value()) el = ToJson(*v);
        else el = nullptr;
    }
    template <typename T>
    void from_json(const JsonElement& el, std::optional<T>& v) {
        if (el.isNull()) v = std::nullopt;
        else v = FromJson<T>(el);
    }

    template <typename A, typename B>
    void to_json(JsonElement& el, const std::pair<A, B>& p) {
        JsonArray arr;
        arr.reserve(2);
        arr.push_back(ToJson(p.first));
        arr.push_back(ToJson(p.second));
        el = std::move(arr);
    }
    template <typename A, typename B>
    void from_json(const JsonElement& el, std::pair<A, B>& p) {
        const auto* a = el.asArray();
        if (!a || a->size() < 2) throw std::runtime_error("from_json: expected pair array");
        p.first = FromJson<A>((*a)[0]);
        p.second = FromJson<B>((*a)[1]);
    }

    // ---------- 结构体反射宏（最多 32 个字段） ----------
    namespace detail {
        template <typename T>
        void struct_field_from(const UnorderedObject& o, const std::string& key, T& out) {
            const auto it = o.find(key);
            if (it != o.end()) out = FromJson<T>(it->second);
        }
    } // namespace detail

    // 字段动作宏：写入 / 读取
    #define JSON_INTEROP_FIELD_W(f) (*_obj)[#f] = JSON::ToJson(v.f);
    #define JSON_INTEROP_FIELD_R(f) JSON::detail::struct_field_from(*_o, #f, v.f);

    // 递归展开（支持 1..32 个字段）
    #define JSON_INTEROP_EXPAND(x) x
    #define JSON_INTEROP_CAT(a, b) a##b
    #define JSON_INTEROP_FE_1(A, f) A(f)
    #define JSON_INTEROP_FE_2(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_1(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_3(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_2(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_4(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_3(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_5(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_4(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_6(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_5(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_7(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_6(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_8(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_7(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_9(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_8(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_10(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_9(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_11(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_10(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_12(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_11(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_13(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_12(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_14(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_13(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_15(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_14(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_16(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_15(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_17(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_16(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_18(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_17(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_19(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_18(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_20(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_19(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_21(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_20(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_22(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_21(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_23(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_22(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_24(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_23(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_25(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_24(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_26(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_25(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_27(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_26(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_28(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_27(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_29(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_28(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_30(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_29(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_31(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_30(A, __VA_ARGS__))
    #define JSON_INTEROP_FE_32(A, f, ...) A(f) JSON_INTEROP_EXPAND(JSON_INTEROP_FE_31(A, __VA_ARGS__))

    #define JSON_INTEROP_NARG_(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, \
                               u, v, w, x, y, z, aa, ab, ac, ad, ae, af, N, ...) N
    #define JSON_INTEROP_NARG(...) \
        JSON_INTEROP_NARG_(__VA_ARGS__, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, \
                           20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
    #define JSON_INTEROP_FE_C(N, A, ...) JSON_INTEROP_CAT(JSON_INTEROP_FE_, N)(A, __VA_ARGS__)
    #define JSON_INTEROP_FOR_EACH(A, ...) \
        JSON_INTEROP_FE_C(JSON_INTEROP_NARG(__VA_ARGS__), A, __VA_ARGS__)

    // 结构体反射：JSON_DEFINE_STRUCT(Name, field1, field2, ...)
    // 在 struct 定义后调用（与 struct 同一命名空间），生成 to_json/from_json。
    #define JSON_DEFINE_STRUCT(Name, ...) \
        inline void to_json(JSON::JsonElement& el, const Name& v) { \
            el = JSON::UnorderedObject{}; \
            auto* _obj = el.asUnorderedObject(); \
            JSON_INTEROP_FOR_EACH(JSON_INTEROP_FIELD_W, __VA_ARGS__) \
        } \
        inline void from_json(const JSON::JsonElement& el, Name& v) { \
            const auto* _o = el.asUnorderedObject(); \
            if (!_o) throw std::runtime_error("from_json: expected object"); \
            JSON_INTEROP_FOR_EACH(JSON_INTEROP_FIELD_R, __VA_ARGS__) \
        }

} // namespace JSON
