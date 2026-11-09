// JsonCore 单元测试（无外部依赖的轻量测试框架）
#include "JsonCore.hpp"
#include "JsonInterop.hpp"
#include "JsonYaml.hpp"
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <sstream>

using namespace JSON;

static int g_pass = 0, g_fail = 0;

#define CHECK(cond) \
    do { if (cond) { ++g_pass; } else { ++g_fail; \
        std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_THROWS(expr) \
    do { bool thrown = false; \
        try { (void)(expr); } catch (const std::exception&) { thrown = true; } \
        if (thrown) { ++g_pass; } else { ++g_fail; \
            std::printf("  FAIL %s:%d  expected throw: %s\n", __FILE__, __LINE__, #expr); } } while (0)

// 浮点近似比较：本环境为 32 位 MinGW（x87 扩展精度），
// 形如 `x == 3.14` 的写法在 -O2 下会把字面量按 80 位扩展精度参与比较，
// 与 Parse 返回的 64 位 double 不相等。因此对小数一律按容差比较，
// 避免把工具链的 x87 行为误判为解析错误。
static bool near(double a, double b, double tol = 1e-12) {
    double diff = a > b ? a - b : b - a;
    double scale = (a > b ? a : b);
    if (scale < 1.0) scale = 1.0;
    return diff <= tol * scale;
}
static bool nearVal(const JsonElement& e, double b, double tol = 1e-12) {
    return e.isNumber() && near(e.getNumber(), b, tol);
}

// ---------- 基础解析 ----------
static void test_parse_scalars() {
    CHECK(Parse("null").isNull());
    CHECK(Parse("true").getBool() == true);
    CHECK(Parse("false").getBool() == false);

    CHECK(Parse("0").getNumber() == 0.0);
    CHECK(Parse("-1").getNumber() == -1.0);
    CHECK(nearVal(Parse("3.14"), 3.14));
    CHECK(Parse("-0.5").getNumber() == -0.5);
    CHECK(Parse("1e5").getNumber() == 100000.0);
    CHECK(nearVal(Parse("2.5e-3"), 0.0025));
    CHECK(nearVal(Parse("1.0E+2"), 100.0));

    CHECK(Parse("\"hello\"").getString() == "hello");
    CHECK(Parse("\"\"").getString().empty());
}

static void test_parse_whitespace() {
    CHECK(Parse("  [ 1 , 2 ]  ").isArray());
    CHECK(Parse(" \t\n\r { } ").isUnorderedObject());
}

// ---------- 字符串转义 ----------
static void test_parse_escapes() {
    CHECK(Parse("\"a\\\"b\"").getString() == "a\"b");
    CHECK(Parse("\"a\\\\b\"").getString() == "a\\b");
    CHECK(Parse("\"a\\/b\"").getString() == "a/b");
    CHECK(Parse("\"\\n\\t\\r\\b\\f\"").getString() == "\n\t\r\b\f");
    // \uXXXX
    CHECK(Parse("\"\\u0041\"").getString() == "A");
    // 中文 / 代理对（emoji 😀 = U+1F600）
    CHECK(Parse("\"\\u4e2d\\u6587\"").getString() == std::string("\xE4\xB8\xAD\xE6\x96\x87", 6));
    CHECK(Parse("\"\\ud83d\\ude00\"").getString() == std::string("\xF0\x9F\x98\x80", 4));
}

// ---------- 数组 / 对象 ----------
static void test_parse_container() {
    auto arr = Parse("[1,2,3]");
    CHECK(arr.isArray());
    CHECK(arr.asArray()->size() == 3);
    CHECK((*arr.asArray())[0].getNumber() == 1.0);

    auto nested = Parse("{\"a\":[1,{\"b\":true}],\"c\":null}");
    CHECK(nested.isUnorderedObject());
    auto* obj = nested.asUnorderedObject();
    CHECK(obj->contains("a") && obj->contains("c"));
    CHECK((*obj)["c"].isNull());

    CHECK(Parse("[]").isArray());
    CHECK(Parse("{}").isUnorderedObject());
    CHECK(Parse("[[[[]]]]").isArray());
}

static void test_parse_object_order() {
    auto ordered = Parse("{\"z\":1,\"a\":2,\"m\":3}", ParseOption::KeepObjectOrder);
    CHECK(ordered.isOrderedObject());
    auto* o = ordered.asOrderedObject();
    CHECK(o->size() == 3);
    std::vector<std::string> keys;
    for (auto&& kv : *o) keys.push_back(kv.first);
    CHECK((keys.size() == 3 && keys[0] == "z" && keys[1] == "a" && keys[2] == "m"));

    auto unord = Parse("{\"z\":1,\"a\":2}");
    CHECK(unord.isUnorderedObject());
}

// ---------- 解析错误 ----------
static void test_parse_errors() {
    CHECK_THROWS(Parse(""));
    CHECK_THROWS(Parse("   "));
    CHECK_THROWS(Parse("{"));
    CHECK_THROWS(Parse("[1,2"));
    CHECK_THROWS(Parse("{\"a\":1,}"));
    CHECK_THROWS(Parse("[1,,2]"));
    CHECK_THROWS(Parse("{a:1}"));
    CHECK_THROWS(Parse("{\"a\" 1}"));
    CHECK_THROWS(Parse("\"abc"));
    CHECK_THROWS(Parse("\"\\x\""));
    CHECK_THROWS(Parse("\"\\u12\""));
    CHECK_THROWS(Parse("\"\\ud83d\""));
    CHECK_THROWS(Parse("01"));
    CHECK_THROWS(Parse("1."));
    CHECK_THROWS(Parse(".5"));
    CHECK_THROWS(Parse("1e"));
    CHECK_THROWS(Parse("tru"));
    CHECK_THROWS(Parse("nulll"));
    CHECK_THROWS(Parse("1 2"));           // 顶层多余数据
    CHECK_THROWS(Parse("[1]x"));
    CHECK_THROWS(Parse("\"a\"\t\"b\""));
}

// ---------- OrderedObject 操作 ----------
static void test_ordered_object() {
    OrderedObject o;
    o["b"] = 1.0;
    o["a"] = 2.0;
    o["c"] = 3.0;
    CHECK(o.size() == 3);
    CHECK(o.at(0).getNumber() == 1.0);
    CHECK(o.at(1).getNumber() == 2.0);
    CHECK(o.at(2).getNumber() == 3.0);

    o.erase("b");
    CHECK(o.size() == 2);
    CHECK(o.at(0).getNumber() == 2.0);   // 删 b 后 "a" 成为有效第 0 项
    CHECK(!o.contains("b"));
    CHECK(o.contains("a") && o.contains("c"));

    // 墓碑复用
    o["b"] = 9.0;                          // 重新插入被删的 key
    CHECK(o.size() == 3);
    CHECK(o.contains("b"));

    // 迭代顺序（插入顺序）
    std::vector<std::string> keys;
    for (auto&& kv : o) keys.push_back(kv.first);
    CHECK((keys.size() == 3 && keys[0] == "a" && keys[1] == "c" && keys[2] == "b"));

    // 越界访问
    CHECK_THROWS(o.at(3));

    // clear
    o.clear();
    CHECK(o.empty());

    // 覆盖已存在的 key（不改变顺序）
    o["x"] = 1.0;
    o["y"] = 2.0;
    o["x"] = 100.0;
    CHECK(o.size() == 2);
    CHECK(o.at(0).getNumber() == 100.0);
}

// ---------- 切片 ----------
static void test_slices() {
    JsonElement e = Parse("[0,1,2,3,4,5]");
    auto* arr = e.asArray();

    // 读取切片
    auto mid = (*arr)[Slice(1, 4)].toContainer();
    CHECK(mid.size() == 3);
    CHECK(mid[0].getNumber() == 1.0 && mid[2].getNumber() == 3.0);

    // Python 语义：Slice(-2, 0) == a[-2:0]（-2→4, end=0，4<0 为假 → 空切片）
    auto neg = (*arr)[Slice(-2, 0)].toContainer();
    CHECK(neg.size() == 0);
    // 末尾 2 个用字符串形式
    auto last2 = (*arr)[Slice("[-2:]")].toContainer();
    CHECK(last2.size() == 2 && nearVal(last2[0], 4.0) && nearVal(last2[1], 5.0));

    // 写入切片
    (*arr)[Slice(1, 4)] = std::vector<JsonElement>{ JsonElement(9.0), JsonElement(8.0), JsonElement(7.0) };
    CHECK((*arr)[0].getNumber() == 0.0);
    CHECK((*arr)[1].getNumber() == 9.0);
    CHECK((*arr)[3].getNumber() == 7.0);

    // OrderedObject 切片
    auto obj = Parse("{\"a\":1,\"b\":2,\"c\":3,\"d\":4}", ParseOption::KeepObjectOrder);
    auto* oo = obj.asOrderedObject();
    auto objs = (*oo)[Slice(1, 3)].toContainer();
    CHECK(objs.size() == 2);
    CHECK(objs[0].getNumber() == 2.0 && objs[1].getNumber() == 3.0);
}

// ---------- 序列化 / 往返 ----------
static void test_serialize_roundtrip() {
    auto doc = Parse("{\"name\":\"K\\u00f6nig\",\"n\":123,\"pi\":3.14159,"
                     "\"ok\":true,\"nil\":null,\"list\":[1,\"two\",false],\"sub\":{\"a\":[]}}");
    std::string s1 = Serialize(doc);
    CHECK(!s1.empty());
    auto back = Parse(s1);
    CHECK(back.isUnorderedObject());
    // 无序对象重解析后键序不保证一致，故按键集合与值比对
    auto* bo = back.asUnorderedObject();
    CHECK(bo->size() == 7);
    CHECK((*bo)["name"].getString() == "K\xC3\xB6nig");
    CHECK(nearVal((*bo)["pi"], 3.14159));
    CHECK((*bo)["ok"].getBool() == true && (*bo)["nil"].isNull());
    CHECK((*bo)["list"].isArray() && (*bo)["list"].asArray()->size() == 3);
    CHECK((*bo)["sub"].isUnorderedObject());

    // 有序往返
    auto odoc = Parse("{\"z\":1,\"a\":2,\"m\":3}", ParseOption::KeepObjectOrder);
    auto oback = Parse(Serialize(odoc), ParseOption::KeepObjectOrder);
    std::vector<std::string> k1, k2;
    for (auto&& kv : *odoc.asOrderedObject()) k1.push_back(kv.first);
    for (auto&& kv : *oback.asOrderedObject()) k2.push_back(kv.first);
    CHECK((k1 == k2 && k1.size() == 3));

    // 数字最短往返
    CHECK(nearVal(Parse(Serialize(Parse("0.1"))), 0.1));
    CHECK(Serialize(Parse("1.5")) == "1.5");
    CHECK(Serialize(Parse("100")) == "100");

    // 美化格式可再解析
    auto pretty = Serialize(doc, 2);
    CHECK(pretty.find('\n') != std::string::npos);
    CHECK(Parse(pretty).isUnorderedObject());
}

// ---------- TryParse ----------
static void test_try_parse() {
    JsonElement out;
    std::string err;
    CHECK(TryParse("[1,2]", out, &err));
    CHECK(out.isArray());
    CHECK(!TryParse("[1,", out, &err));
    CHECK(!err.empty());
}

// ---------- 对象互转 ----------
static void test_conversion() {
    auto o = Parse("{\"a\":1,\"b\":2}", ParseOption::KeepObjectOrder);
    auto u = ToUnordered(*o.asOrderedObject());
    CHECK(u.contains("a") && u.contains("b"));
    auto back = ToOrdered(u);
    CHECK(back.size() == 2);
}

// ---------- 性能：有序对象构建必须是 O(N) ----------
static void test_performance() {
    const size_t N = 200000;
    auto t0 = std::chrono::steady_clock::now();
    OrderedObject o;
    for (size_t i = 0; i < N; ++i)
        o["k" + std::to_string(i)] = double(i);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    CHECK(o.size() == N);
    // 若缓存每次重建（O(N^2)），200k 元素将是分钟级；O(N) 应在毫秒级。
    // 留足余量，只拦截明显的 O(N^2) 回归。
    CHECK(ms < 3000.0);
    std::printf("  [perf] OrderedObject build N=%zu: %.1f ms\n", N, ms);

    // 随机删除 + 重插（墓碑与 compact 路径）
    for (size_t i = 0; i < N; i += 2) o.erase("k" + std::to_string(i));
    CHECK(o.size() == N / 2);
    for (size_t i = 0; i < N; i += 2) o["k" + std::to_string(i)] = double(i) * 2;
    CHECK(o.size() == N);

    // 解析大文档
    std::string big = "[";
    for (size_t i = 0; i < 50000; ++i) { if (i) big += ","; big += std::to_string(i); }
    big += "]";
    auto t2 = std::chrono::steady_clock::now();
    auto bigArr = Parse(big);
    auto t3 = std::chrono::steady_clock::now();
    CHECK(bigArr.asArray()->size() == 50000);
    double pms = std::chrono::duration<double, std::milli>(t3 - t2).count();
    std::printf("  [perf] parse array N=50000: %.1f ms\n", pms);
}

// ---------- 易用性：整型 / 字符串字面量不应产生歧义 ----------
static void test_ergonomics() {
    // 整型字面量 / 变量按数值存储（精确重载，避免与 bool/double 歧义）
    JsonElement a;
    a = 31;
    CHECK(a.isNumber() && a.getNumber() == 31.0);
    JsonElement b(100);
    CHECK(b.isNumber() && b.getNumber() == 100.0);
    int iv = 7;
    JsonElement c(iv);
    CHECK(c.isNumber() && c.getNumber() == 7.0);
    long long ll = 10000000000LL;
    JsonElement d(ll);
    CHECK(d.isNumber() && d.getNumber() == 10000000000.0);

    // 字符串字面量按 string 存储（避免被误判为 bool）
    JsonElement s;
    s = "hello";
    CHECK(s.isString() && s.getString() == "hello");
    JsonElement s2("world");
    CHECK(s2.isString() && s2.getString() == "world");

    // bool 仍是 bool（不被整型重载抢走）
    JsonElement t = true;
    CHECK(t.isBool() && t.getBool() == true);

    // toContainer 结果可直接回包成 JsonArray
    auto arr = Parse("[1,2,3]");
    auto mid = (*arr.asArray())[Slice(1, 3)].toContainer();
    JsonArray wrapped(mid);
    CHECK(wrapped.size() == 2 && wrapped[0].getNumber() == 2.0);
}

// ---------- 字符串切片：完整 Python 语义（省略边界 / 反转 / 负边界） ----------
static std::vector<JsonElement> take(const Slice& sl) {
    JsonElement big = Parse("[0,1,2,3,4,5,6,7,8,9]");
    return (*big.asArray())[sl].toContainer();
}
static bool numsEq(const std::vector<JsonElement>& v, const std::vector<double>& want) {
    if (v.size() != want.size()) return false;
    for (size_t i = 0; i < v.size(); ++i)
        if (!(v[i].isNumber() && near(v[i].getNumber(), want[i]))) return false;
    return true;
}

static void test_python_slices() {
    // 数字形式：完整 Python 语义（负边界按长度归一化）
    CHECK(numsEq(take(Slice(-2, 0)), {}));                            // Python a[-2:0] = 空
    CHECK(numsEq(take(Slice(1, 5)), {1, 2, 3, 4}));
    CHECK(numsEq(take(Slice(1, -1)), {1, 2, 3, 4, 5, 6, 7, 8}));      // 数字形式去首尾 a[1:-1]
    CHECK(numsEq(take(Slice(0, 10, 2)), {0, 2, 4, 6, 8}));
    CHECK(numsEq(take(Slice(9, 0, -1)), {9, 8, 7, 6, 5, 4, 3, 2, 1})); // 数字形式负步长反转
    // 显式索引列表：仍是确定下标集合（负下标逐元素解析）
    CHECK(numsEq(take(Slice{1, 3, 5}), {1, 3, 5}));
    CHECK(numsEq(take(Slice{-2, -1}), {8, 9}));                       // 末尾 2 个用显式列表

    // 字符串形式：完整 Python 语义
    CHECK(numsEq(take(Slice("[1:4]")), {1, 2, 3}));                       // 区间
    CHECK(numsEq(take(Slice("[1:]")), {1, 2, 3, 4, 5, 6, 7, 8, 9}));      // 省略 end
    CHECK(numsEq(take(Slice("[:5]")), {0, 1, 2, 3, 4}));                  // 省略 start
    CHECK(numsEq(take(Slice("[::-1]")), {9, 8, 7, 6, 5, 4, 3, 2, 1, 0}));  // 反转
    CHECK(numsEq(take(Slice("[::-2]")), {9, 7, 5, 3, 1}));                 // 反向步长
    CHECK(numsEq(take(Slice("[1:-1]")), {1, 2, 3, 4, 5, 6, 7, 8}));        // 去首尾
    CHECK(numsEq(take(Slice("[-2:]")), {8, 9}));                           // 末尾2个
    CHECK(numsEq(take(Slice("[-2:6]")), {}));                              // Python a[-2:6] = 空
    CHECK(numsEq(take(Slice("[-8:-2]")), {2, 3, 4, 5, 6, 7}));
    CHECK(numsEq(take(Slice("[1:-1:2]")), {1, 3, 5, 7}));
    CHECK(numsEq(take(Slice("[3]")), {3}));                                // 单下标
    CHECK(numsEq(take(Slice("[-1]")), {9}));                               // 末元素
    CHECK(numsEq(take(Slice("[0:10:2]")), {0, 2, 4, 6, 8}));

    // 链式复合切片（数字 + 字符串混用）
    JsonElement big = Parse("[0,1,2,3,4,5,6,7,8,9]");
    auto compound = (*big.asArray())[Slice(0, 6)][Slice("[1:3]")].toContainer();
    CHECK(numsEq(compound, {1, 2}));

    // 非法字符串切片仍抛错
    CHECK_THROWS(take(Slice("[a]")));
    CHECK_THROWS(take(Slice("[1:4:0]")));                      // step=0
}

// ---------- 链式下标访问 obj["a"]["b"] / arr[0]["x"] ----------
static void test_chained_access() {
    // 写：缺失/null 自动创建
    UnorderedObject o;
    o["post"]["Invalid"] = std::string("x");
    CHECK(o["post"].isUnorderedObject());
    CHECK(o["post"]["Invalid"].getString() == "x");
    CHECK(Serialize(JsonElement(o)) == "{\"post\":{\"Invalid\":\"x\"}}");

    // 读已解析文档（非 const 链式）
    auto doc = Parse("{\"a\":{\"b\":[1,2,3]}}");
    CHECK(doc["a"]["b"][1].getNumber() == 2.0);
    doc["a"]["b"][1] = 99.0;
    CHECK(doc["a"]["b"][1].getNumber() == 99.0);

    // 数组链式
    auto arr = Parse("[0,[10,20],{\"k\":1}]");
    CHECK(arr[1][0].getNumber() == 10.0);
    CHECK(arr[2]["k"].getNumber() == 1.0);
    arr[1][1] = 21.0;                                   // 写嵌套
    CHECK(arr[1][1].getNumber() == 21.0);

    // const 链式只读
    const JsonElement ce = Parse("{\"a\":{\"b\":7}}");
    CHECK(ce["a"]["b"].getNumber() == 7.0);
    const JsonElement carr = Parse("[1,2]");
    CHECK(carr[0].getNumber() == 1.0);

    // 类型不符 / key 缺失抛错（const 不修改）
    CHECK_THROWS(Parse("3.14")["x"]);
    CHECK_THROWS(static_cast<const JsonElement&>(Parse("true"))["x"]);
    CHECK_THROWS(static_cast<const JsonElement&>(Parse("{\"a\":1}"))["nope"]);
    CHECK_THROWS(Parse("[1]")["x"]);
}

// ---------- Number 整数保真 ----------
static void test_number() {
    using std::int64_t;
    using std::uint64_t;
    const int64_t I64MAX = (std::numeric_limits<int64_t>::max)();
    const int64_t I64MIN = (std::numeric_limits<int64_t>::min)();
    const uint64_t U64MAX = (std::numeric_limits<uint64_t>::max)();

    // 解析整数字面量：int64 → uint64 → 大整数，逐级保精度
    auto e1 = Parse("9223372036854775807");              // INT64_MAX
    CHECK(e1.numberValue().isInt());
    CHECK(e1.getInt64() == I64MAX);
    CHECK(e1.getUInt64() == static_cast<uint64_t>(I64MAX));

    auto e2 = Parse("-9223372036854775808");             // INT64_MIN
    CHECK(e2.numberValue().isInt());
    CHECK(e2.getInt64() == I64MIN);

    auto e3 = Parse("18446744073709551615");             // UINT64_MAX
    CHECK(e3.numberValue().isUInt());
    CHECK(e3.getUInt64() == U64MAX);
    CHECK_THROWS(e3.getInt64());

    auto e4 = Parse("9223372036854775808");              // > INT64_MAX → uint64
    CHECK(e4.numberValue().isUInt());
    CHECK(e4.getUInt64() == 9223372036854775808ULL);

    auto e5 = Parse("18446744073709551616");             // > UINT64_MAX → 大整数
    CHECK(e5.numberValue().isBigInt());
    CHECK(e5.getBigIntString() == "18446744073709551616");
    CHECK_THROWS(e5.getInt64());
    CHECK_THROWS(e5.getUInt64());

    auto e6 = Parse("-18446744073709551616");            // 负大整数
    CHECK(e6.numberValue().isBigInt());
    CHECK(e6.getBigIntString() == "-18446744073709551616");

    // 大整数与整型比较（精确）与排序
    const Number big("18446744073709551616");
    const Number small(std::int64_t(5));
    CHECK(big == Number(std::string("18446744073709551616")));
    CHECK(big != small && small < big && !(big < small));
    CHECK(Number(std::string("-3")) < Number(std::string("-2")));
    CHECK(Number(std::string("12345678901234567890")) ==
          Number(std::string("12345678901234567890")));

    // 浮点提升为 long double
    auto f = Parse("3.14");
    CHECK(f.numberValue().isFloat());
    CHECK(near(f.getNumber(), 3.14));
    CHECK(f.getLongDouble() > 3.14L - 1e-12L && f.getLongDouble() < 3.14L + 1e-12L);

    // 数值构造分派
    JsonElement ei(42);
    CHECK(ei.numberValue().isInt() && ei.getInt64() == 42);
    JsonElement eu(18446744073709551615ULL);
    CHECK(eu.numberValue().isUInt() && eu.getUInt64() == U64MAX);
    JsonElement ed(1.5);
    CHECK(ed.numberValue().isFloat() && ed.getNumber() == 1.5);
    JsonElement el(-7LL);
    CHECK(el.getInt64() == -7);

    // 序列化保真（整数按原值，大整数按规范化字符串）
    CHECK(Serialize(Parse("9223372036854775807")) == "9223372036854775807");
    CHECK(Serialize(Parse("18446744073709551615")) == "18446744073709551615");
    CHECK(Serialize(Parse("18446744073709551616")) == "18446744073709551616");
    CHECK(Serialize(Parse("-18446744073709551616")) == "-18446744073709551616");
    CHECK(Serialize(Parse("1.5")) == "1.5");
    CHECK(Serialize(Parse("1e5")) == "1e+05");
    CHECK(nearVal(Parse(Serialize(Parse("0.1"))), 0.1));
    CHECK(Parse(Serialize(Parse("1e5"))).getNumber() == 100000.0);

    // 精确读取的溢出/类型错误
    CHECK_THROWS(Parse("9223372036854775808").getInt64());
    CHECK_THROWS(Parse("-9223372036854775809").getInt64());
    CHECK_THROWS(Parse("-1").getUInt64());
    CHECK_THROWS(Parse("3.14").getInt64());              // 浮点按整数读取需整值
    CHECK_THROWS(Parse("3.14").getBigIntString());
    CHECK_THROWS(Parse("\"s\"").getInt64());

    // 大整数截断读取（近似）
    CHECK(Parse("18446744073709551616").getNumber() > 1.8e19);
}

// ---------- ParseError 行列号与期望符号 ----------
static void test_parse_error_location() {
    // 多行文本：第 3 行 "tru" 处截断的 true
    const std::string txt = "{\n  \"a\": 1,\n  \"b\": tru\n}";
    bool caught = false;
    size_t off = 0, line = 0, col = 0;
    std::string expected, msg;
    try { Parse(txt); } catch (const ParseError& e) {
        caught = true;
        off = e.offset; line = e.line; col = e.column;
        expected = e.expected; msg = e.what();
    }
    CHECK(caught);
    CHECK(off == 19 && line == 3 && col == 8);
    CHECK(expected == "true");
    // what() 保持旧前缀兼容
    CHECK(msg.rfind("JSON parse error at offset 19: ", 0) == 0);

    // 期望符号
    try { Parse("{\"a\" 1}"); } catch (const ParseError& e) { CHECK(e.expected == "':'"); }
    try { Parse("[1 2]"); } catch (const ParseError& e) { CHECK(e.expected == "','"); }

    // TryParse 错误串含行列前缀
    JsonElement out;
    std::string err;
    CHECK(!TryParse("[1,", out, &err));
    CHECK(err.rfind("JSON parse error at offset", 0) == 0);
    CHECK(ParseError(3U, 1U, 4U, "']'", "boom").offset == 3U);
}

// ---------- 便利 API ----------
static void test_convenience_api() {
    auto doc = Parse("{\"a\": 5, \"s\": \"hi\", \"arr\": [1,2]}");

    // value(key, def)：命中 / 缺失 / 自身非对象
    CHECK(doc.value("a", JsonElement(0)).getInt() == 5);
    CHECK(doc.value("nope", JsonElement(-1)).getInt() == -1);
    CHECK(doc.value("arr", JsonElement(0)).isArray());
    CHECK(Parse("3.14").value("x", JsonElement("d")).getString() == "d");

    // getStringOr
    CHECK(doc["s"].getStringOr("fallback") == "hi");
    CHECK(doc["a"].getStringOr("fallback") == "fallback");
    CHECK(Parse("true").getStringOr("d") == "d");

    // getInt（便捷截取，需整数值）
    CHECK(doc["a"].getInt() == 5);
    CHECK(Parse("3.0").getInt() == 3);
    CHECK_THROWS(Parse("3.14").getInt());
    CHECK_THROWS(Parse("1e30").getInt());
    CHECK_THROWS(doc["s"].getInt());

    // 精确读取
    CHECK(Parse("42").getInt64() == 42);
    CHECK(Parse("-7").getInt64() == -7);
    CHECK(Parse("42").getLongDouble() == 42.0L);

    // dump == Serialize
    CHECK(doc.dump() == Serialize(doc));
    CHECK(doc.dump(2) == Serialize(doc, 2));
}

// ---------- 容器 / 结构体互操作 ----------
struct Person {
    std::string name;
    int age = 0;
    std::vector<int> scores;
    std::optional<std::string> nick;
};
JSON_DEFINE_STRUCT(Person, name, age, scores, nick)

static void test_interop() {
    // 基础类型
    CHECK(ToJson(42).getInt() == 42);
    CHECK(ToJson(std::string("hi")).getString() == "hi");
    CHECK(ToJson(3.5).getNumber() == 3.5);
    CHECK(ToJson(true).getBool() == true);
    CHECK(FromJson<int>(Parse("7")) == 7);
    CHECK(FromJson<double>(Parse("2.5")) == 2.5);
    CHECK(FromJson<std::string>(Parse("\"s\"")) == "s");

    // vector
    std::vector<int> v{1, 2, 3};
    CHECK(Serialize(ToJson(v)) == "[1,2,3]");
    auto back = FromJson<std::vector<int>>(Parse("[4,5]"));
    CHECK(back.size() == 2 && back[0] == 4 && back[1] == 5);

    // map / unordered_map
    std::map<std::string, int> m{{"a", 1}, {"b", 2}};
    auto jm = ToJson(m);
    CHECK(jm.isUnorderedObject());
    CHECK((FromJson<std::map<std::string, int>>(jm)["a"] == 1));
    std::unordered_map<std::string, int> um{{"x", 9}};
    CHECK((FromJson<std::unordered_map<std::string, int>>(ToJson(um))["x"] == 9));

    // optional
    CHECK(ToJson(std::optional<int>(5)).getInt() == 5);
    CHECK(ToJson(std::optional<int>()).isNull());
    CHECK(!FromJson<std::optional<int>>(Parse("null")).has_value());
    CHECK(FromJson<std::optional<int>>(Parse("3")).value() == 3);

    // pair
    CHECK(Serialize(ToJson(std::make_pair(1, "x"))) == "[1,\"x\"]");
    auto p = FromJson<std::pair<int, std::string>>(Parse("[7,\"k\"]"));
    CHECK(p.first == 7 && p.second == "k");

    // struct 反射
    Person p1{"Tom", 30, {1, 2, 3}, std::string("t")};
    auto jp = ToJson(p1);
    CHECK(jp["name"].getString() == "Tom");
    CHECK(jp["age"].getInt() == 30);
    CHECK(Serialize(jp["scores"]) == "[1,2,3]");
    CHECK(jp["nick"].getString() == "t");

    auto p2 = FromJson<Person>(Parse(
        "{\"name\":\"Jerry\",\"age\":25,\"scores\":[9],\"nick\":null}"));
    CHECK(p2.name == "Jerry" && p2.age == 25);
    CHECK(p2.scores.size() == 1 && p2.scores[0] == 9);
    CHECK(!p2.nick.has_value());

    // 缺失字段保持默认值
    auto p3 = FromJson<Person>(Parse("{\"name\":\"A\"}"));
    CHECK(p3.name == "A" && p3.age == 0 && p3.scores.empty());
}

// ---------- 解析增强：流式 / 容忍模式 ----------
static void test_parse_lenient_stream() {
    // 注释容忍：// /* */ #
    auto doc = Parse(R"({
        // 行注释
        "a": 1, /* 块注释 */ "b": 2,
        # 井号注释
        "c": 3
    })", ParseOption::Lenient);
    CHECK(doc.isUnorderedObject());
    CHECK(doc["a"].getInt() == 1 && doc["b"].getInt() == 2 && doc["c"].getInt() == 3);

    // 尾逗号容忍
    CHECK(Parse("[1, 2,]", ParseOption::Lenient).isArray());
    CHECK(Parse("{\"a\":1,}", ParseOption::Lenient).isUnorderedObject());
    CHECK(Parse("{\"a\":1,}", ParseOption::KeepObjectOrder | ParseOption::Lenient).isOrderedObject());

    // 严格模式仍拒绝
    CHECK_THROWS(Parse("[1, 2,]"));
    CHECK_THROWS(Parse("{\"a\":1,}"));

    // 流式解析
    std::istringstream iss("{\"k\": [1, 2, 3]}");
    auto sdoc = Parse(iss);
    CHECK(sdoc["k"].isArray() && sdoc["k"].asArray()->size() == 3);

    // ParseFile：写临时文件再读
    const char* tmp = "jsonlib_tmp_test.json";
    CHECK(SerializeToFile(Parse("{\"x\": 9}"), tmp));
    auto fdoc = ParseFile(tmp);
    CHECK(fdoc["x"].getInt() == 9);
    std::remove(tmp);

    // TryParseIssues（收集式，当前至多一条）
    JsonElement out;
    std::vector<ParseError> issues;
    CHECK(!TryParseIssues("[1, 2", out, issues));
    CHECK(issues.size() == 1 && issues[0].line == 1);
}

// ---------- OrderedObject 增强 ----------
static void test_ordered_object_extra() {
    OrderedObject o;
    o["a"] = 1; o["b"] = 2; o["c"] = 3; o["d"] = 4;

    // find 返回迭代器
    auto it = o.find("b");
    CHECK(it != o.end() && it->second.getInt() == 2);
    CHECK(o.find("nope") == o.end());
    const OrderedObject& co = o;
    CHECK(co.find("c") != co.end() && co.find("c")->second.getInt() == 3);

    // 反向迭代
    std::vector<std::string> revKeys;
    for (auto rit = o.rbegin(); rit != o.rend(); ++rit) revKeys.push_back(rit->first);
    CHECK(revKeys.size() == 4 && revKeys[0] == "d" && revKeys[3] == "a");

    // 范围删除 [b, d)：删除 b、c
    o.erase(o.find("b"), o.find("d"));
    CHECK(o.size() == 2);
    CHECK(!o.contains("b") && !o.contains("c"));
    CHECK(o.contains("a") && o.contains("d"));
    // 墓碑跳过后的反向迭代仍正确
    std::vector<std::string> rev2;
    for (auto rit = o.rbegin(); rit != o.rend(); ++rit) rev2.push_back(rit->first);
    CHECK(rev2.size() == 2 && rev2[0] == "d" && rev2[1] == "a");
    // 正向迭代跳过墓碑
    std::vector<std::string> fwd;
    for (auto it2 = o.begin(); it2 != o.end(); ++it2) fwd.push_back(it2->first);
    CHECK(fwd.size() == 2 && fwd[0] == "a" && fwd[1] == "d");
}

static void test_yaml() {
    // 块映射 + 标量类型推断
    {
        auto d = ParseYaml("name: Tom\nage: 30\nactive: true\nscore: 3.5\nempty:\nnote: null\n");
        CHECK(d.isUnorderedObject());
        CHECK(d["name"].isString() && d["name"].getString() == "Tom");
        CHECK(d["age"].isNumber() && d["age"].getInt() == 30);
        CHECK(d["active"].isBool() && d["active"].getBool() == true);
        CHECK(d["score"].isNumber() && nearVal(d["score"], 3.5));
        CHECK(d["note"].isNull());
        CHECK(d["empty"].isNull());   // key:（无嵌套行）→ null
    }

    // 块序列
    {
        auto d = ParseYaml("- a\n- b\n- c\n");
        CHECK(d.isArray());
        CHECK(d.asArray()->size() == 3);
        CHECK((*d.asArray())[0].getString() == "a");
        CHECK((*d.asArray())[2].getString() == "c");
    }

    // 嵌套：映射值换行缩进 + 数组
    {
        auto d = ParseYaml(
            "server:\n"
            "  host: localhost\n"
            "  port: 8080\n"
            "  tags:\n"
            "    - web\n"
            "    - api\n");
        CHECK(d.isUnorderedObject());
        CHECK(d["server"]["host"].getString() == "localhost");
        CHECK(d["server"]["port"].getInt() == 8080);
        const JsonArray* tags = d["server"]["tags"].asArray();
        CHECK(tags && tags->size() == 2 && (*tags)[0].getString() == "web");
    }

    // 序列项行内映射 + 同缩进扩展（YAML 常见 servers 结构）
    {
        auto d = ParseYaml(
            "servers:\n"
            "  - name: web\n"
            "    port: 80\n"
            "  - name: db\n"
            "    port: 5432\n");
        const JsonArray* s = d["servers"].asArray();
        CHECK(s && s->size() == 2);
        CHECK((*s)[0]["name"].getString() == "web");
        CHECK((*s)[0]["port"].getInt() == 80);
        CHECK((*s)[1]["name"].getString() == "db");
        CHECK((*s)[1]["port"].getInt() == 5432);
    }

    // 流式内联
    {
        auto d = ParseYaml("nums: [1, 2, 3]\ncfg: {a: 1, b: [x, y], c: hello}\n");
        const JsonArray* nums = d["nums"].asArray();
        CHECK(nums && nums->size() == 3 && (*nums)[2].getInt() == 3);
        CHECK(d["cfg"]["a"].getInt() == 1);
        CHECK(d["cfg"]["b"].asArray() && (*d["cfg"]["b"].asArray())[1].getString() == "y");
        CHECK(d["cfg"]["c"].getString() == "hello");
        auto flow2 = ParseYaml("plain: [1, 2,]\n");   // 流式尾逗号容忍
        CHECK(flow2["plain"].asArray() && flow2["plain"].asArray()->size() == 2);
    }

    // 注释与文档标记
    {
        auto d = ParseYaml("# top comment\n---\na: 1 # inline\nb: 2\n...\n");
        CHECK(d["a"].getInt() == 1 && d["b"].getInt() == 2);
    }

    // 大整数保真（复用 Number）
    {
        auto d = ParseYaml("big: 123456789012345678901234567890\nneg: -123456789012345678901234567890\n");
        CHECK(d["big"].isNumber() && d["big"].getBigIntString() == "123456789012345678901234567890");
        CHECK(d["neg"].isNumber() && d["neg"].getBigIntString() == "-123456789012345678901234567890");
        auto i64 = ParseYaml("n: 9223372036854775807\n");   // int64 max 保真
        CHECK(i64["n"].getInt64() == 9223372036854775807LL);
    }

    // 引号字符串
    {
        auto d = ParseYaml("s1: 'it''s fine'\ns2: \"a\\nb\"\ns3: 123\ns4: true\n");
        CHECK(d["s1"].getString() == "it's fine");
        CHECK(d["s2"].getString() == "a\nb");
        CHECK(d["s3"].isNumber());        // 裸 123 → 数字
        CHECK(d["s4"].isBool());          // 裸 true → bool
    }

    // ToYaml 往返
    {
        JsonElement doc;
        doc["name"] = "Tom";
        doc["age"] = 30;
        doc["active"] = true;
        doc["pi"] = 3.14;
        doc["big"] = Number(std::string("123456789012345678901234567890")); // 字符串大整数
        JsonArray tags;
        tags.push_back(JsonElement("web"));
        tags.push_back(JsonElement("api"));
        doc["tags"] = tags;
        UnorderedObject nested;
        nested["x"] = 1;
        doc["nested"] = nested;
        std::string y = ToYaml(doc);
        auto back = ParseYaml(y);
        CHECK(back["name"].getString() == "Tom");
        CHECK(back["age"].getInt() == 30);
        CHECK(back["active"].getBool() == true);
        CHECK(nearVal(back["pi"], 3.14));
        CHECK(back["big"].isNumber() && back["big"].getBigIntString() == "123456789012345678901234567890");
        CHECK(back["tags"].asArray() && back["tags"].asArray()->size() == 2);
        CHECK(back["nested"]["x"].getInt() == 1);
    }

    // 字符串自动加引号（歧义保护）
    {
        JsonElement s;
        s["v"] = std::string("true");       // 内容像 bool → 必须引号，往返保持字符串
        auto back = ParseYaml(ToYaml(s));
        CHECK(back["v"].isString() && back["v"].getString() == "true");
        s["v"] = std::string("3.14");
        auto back2 = ParseYaml(ToYaml(s));
        CHECK(back2["v"].isString() && back2["v"].getString() == "3.14");
        s["v"] = std::string("");
        auto back3 = ParseYaml(ToYaml(s));
        CHECK(back3["v"].isString() && back3["v"].getString() == "");
        s["v"] = std::string("hello world");
        auto back4 = ParseYaml(ToYaml(s));
        CHECK(back4["v"].isString() && back4["v"].getString() == "hello world");
    }

    // TryParseYaml 与错误位置
    {
        JsonElement out;
        std::string err;
        CHECK(!TryParseYaml("a: [1, 2", out, &err));
        CHECK(!err.empty());
        try {
            ParseYaml("a: 1\n\tb: 2\n");   // tab 缩进 → 报错
            CHECK(false);
        } catch (const YamlParseError& e) {
            CHECK(e.line == 2);
            CHECK(g_pass > 0);  // 占位防误判
        }
        try {
            ParseYaml("a: \"unclosed\n");
            CHECK(false);
        } catch (const YamlParseError&) {
            CHECK(true);
        }
    }

    // istream / 文件
    {
        std::istringstream iss("k: 1\nlist:\n  - x\n");
        auto d = ParseYaml(iss);
        CHECK(d["k"].getInt() == 1 && d["list"].asArray()->size() == 1);
        const char* tmp = "jsonlib_yaml_test.yml";
        std::FILE* f = std::fopen(tmp, "wb");
        CHECK(f != nullptr);
        if (f) { std::fputs("from_file: 42\n", f); std::fclose(f); }
        auto df = ParseYamlFile(tmp);
        CHECK(df["from_file"].getInt() == 42);
        std::remove(tmp);
    }
}

int main() {
    test_parse_scalars();
    test_parse_whitespace();
    test_parse_escapes();
    test_parse_container();
    test_parse_object_order();
    test_parse_errors();
    test_ordered_object();
    test_slices();
    test_python_slices();
    test_serialize_roundtrip();
    test_try_parse();
    test_conversion();
    test_ergonomics();
    test_chained_access();
    test_number();
    test_parse_error_location();
    test_convenience_api();
    test_parse_lenient_stream();
    test_ordered_object_extra();
    test_interop();
    test_yaml();
    test_performance();

    std::printf("\n==== JsonCore tests: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
