# JsonLib — 轻量 C++17 JSON 库

单文件头 + 单实现文件的跨平台 JSON 库，提供 RFC 8259 完整解析、序列化、值语义容器与 **Python 风格切片**。无第三方运行时依赖，头文件无需预编译。

- 语言标准：**C++17**
- 平台：Windows / Linux / macOS（已验证 32 位与 64 位 MinGW）
- 依赖：仅标准库 + `../Util/Slice.hpp`

---

## 目录

1. [特性](#1-特性)
2. [目录结构](#2-目录结构)
3. [集成与构建](#3-集成与构建)
4. [快速开始](#4-快速开始)
5. [API 参考](#5-api-参考)
6. [Python 风格切片（重点）](#6-python-风格切片重点)
7. [性能](#7-性能)
8. [单元测试](#8-单元测试)
9. [实现要点与注意事项](#9-实现要点与注意事项)
10. [未来拓展方向](#10-未来拓展方向)

---

## 1. 特性

- **两种对象模型**：
  - `UnorderedObject`（默认）：基于 `std::unordered_map`，符合 RFC 8259，访问 O(1)，键序不保证。
  - `OrderedObject`：保插入顺序，支持按下标访问与切片，删除采用**墓碑 + 惰性 compact**，插入/删除摊还 O(1)；提供 `find` 返回迭代器、`rbegin/rend` 反向迭代与 `erase(first,last)` 范围删除。
- **Python 风格切片**：`Slice(start, end, step)` 与 `Slice("[start:end:step]")` 统一按 Python 语义解析（负边界、省略、负步长）；`Slice{...}` 为显式下标集合。
- **完整解析**：转义、`\uXXXX` 代理对转 UTF-8、数字校验、错误信息带**出错字节偏移、1 起始行列号与期望符号**（`ParseError`）。
- **非严格模式** `ParseOption::Lenient`：容忍 `//`、`/* */`、`#` 注释与尾逗号；支持 `istream` / 文件路径流式解析（`Parse(istream)` / `ParseFile`）。
- **整数保真 `Number`**：整数按 `int64 → uint64 → 字符串大整数` 逐级保精度，浮点提升为 `long double`；精确读取 `getInt64/getUInt64/getLongDouble/getBigIntString`，大整数只存储/比较/序列化。
- **最短往返序列化**：整数 `to_chars`、浮点 `to_chars` 最短表示 / `%.21Lg` 保精度；紧凑 / 美化两种模式，`PrettyOptions` 可配置缩进字符串、键排序、空容器策略；`SerializeTo` 支持 buffer 复用与文件输出。
- **便利 API**：链式 `operator[]`（写导向自动建键）、`.value(key, def)`、`.getInt()`、`.getStringOr(def)`、`.dump()`。
- **易用性重载**：整型字面量按数值、字符串字面量按 string 存储，避免 `bool`/`double` 歧义。
- **非抛出版本** `TryParse` 与收集式 `TryParseIssues`，适合对健壮性有要求的调用方。
- **容器互操作**（`JsonInterop.hpp`）：`ToJson`/`FromJson` 支持 `std::vector`/`map`/`unordered_map`/`optional`/`pair`，`JSON_DEFINE_STRUCT` 宏一键反射 struct。
- **YAML 导入 / 导出**（`JsonYaml.hpp`）：`ParseYaml` / `ToYaml`，支持块状映射/序列（缩进驱动）、流式内联 `[]`/`{}`、注释、标量类型推断（含大整数保真）。
- **对象互转** `ToUnordered` / `ToOrdered`。

---

## 2. 目录结构

```
DataStructure/Json/
├── JsonCore.hpp          # 全部类型与 API 声明（值语义、容器、切片代理、ParseError、PrettyOptions）
├── JsonCore.cpp          # Parse / Serialize / OrderedObject / 便利 API 等实现
├── Number.hpp            # Number 数值类型声明（int64/uint64/long double/字符串大整数）
├── Number.cpp            # Number 转换与跨类型比较实现
├── JsonInterop.hpp       # 容器/结构体互操作（ToJson/FromJson/JSON_DEFINE_STRUCT）
├── JsonYaml.hpp          # YAML 导入/导出声明（ParseYaml/ToYaml/YamlParseError）
├── JsonYaml.cpp          # YAML 解析与序列化实现
├── CMakeLists.txt        # 静态库 JsonCore + 单元测试 JsonCoreTests
├── tests/JsonCoreTests.cpp   # 316 条断言（含 Number 保真、ParseError 行列、容忍解析、互操作、YAML、性能回归门）
├── examples/usage_demo.cpp   # 覆盖全部 API 的可运行示例
└── README.md             # 本文档
DataStructure/Util/
└── Slice.hpp             # Slice 切片类型（被 JsonCore.hpp 依赖）
```

> ⚠️ `JsonCore.hpp` 通过 `#include "../Util/Slice.hpp"` 引用切片头。该相对包含会**按 `JsonCore.hpp` 自身所在目录自动解析**，因此你只需让 `-I` 能定位到 `JsonCore.hpp`（即 `Json/` 目录）即可，无需额外处理 `Slice.hpp`。

---

## 3. 集成与构建

### 3.1 直接编译（最简单）

```bash
# -I 指向 Json 目录（JsonCore.hpp 所在处）；其内部 ../Util/Slice.hpp 会自动按相对路径解析
g++ -std=c++17 -O2 my_app.cpp JsonCore.cpp -ID:/path/to/DataStructure/Json -o my_app
```

### 3.2 CMake

```cmake
add_subdirectory(path/to/DataStructure/Json)   # 提供静态库 JsonCore
target_link_libraries(my_app PRIVATE JsonCore)
```

或从项目内直接构建并跑测试：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### 3.3 只引入头

把 `JsonCore.hpp`、`JsonCore.cpp` 与 `Number.hpp`、`Number.cpp` 拷入你的工程，并确保 `Slice.hpp` 可通过 `../Util/Slice.hpp` 解析（或直接放到对应相对路径）。需要互操作时加 `JsonInterop.hpp`，需要 YAML 时加 `JsonYaml.hpp` + `JsonYaml.cpp`。库全部 API 在 `namespace JSON` 中。

---

## 4. 快速开始

```cpp
#include "JsonCore.hpp"
#include <iostream>
using namespace JSON;

int main() {
    // 解析（默认对象为 UnorderedObject）
    auto doc = Parse(R"({"name":"Tom","age":30,"tags":["c++","json"]})");

    // 读取
    auto* obj = doc.asUnorderedObject();
    std::cout << (*obj)["name"].getString() << "\n";   // Tom
    std::cout << (*obj)["age"].getNumber()  << "\n";   // 30

    // 修改 + 序列化
    (*obj)["age"] = 31;
    std::cout << Serialize(doc) << "\n";                // 紧凑
    std::cout << Serialize(doc, 2) << "\n";             // 美化

    // 保序解析 + 切片
    auto odoc = Parse("{\"z\":1,\"a\":2,\"m\":3}", ParseOption::KeepObjectOrder);
    auto* oo = odoc.asOrderedObject();
    std::cout << oo->at(0).getNumber() << "\n";         // 1（插入顺序）

    // 数组切片（Python 语义）
    auto big = Parse("[0,1,2,3,4,5,6,7,8,9]");
    auto* arr = big.asArray();
    auto mid  = (*arr)[Slice(1, 4)].toContainer();      // {1,2,3}
    auto rev  = (*arr)[Slice("[::-1]")].toContainer();  // {9,8,...,0}
    return 0;
}
```

完整可运行示例见 `examples/usage_demo.cpp`。

---

## 5. API 参考

### 5.1 `JsonElement` — 值语义变体

内部为 `std::variant<nullptr_t, bool, Number, std::string, JsonArray, UnorderedObject, OrderedObject>`，其中 `Number`（`Number.hpp`）为整数保真数值类型：`variant<int64_t, uint64_t, long double, string 大整数>`。

**构造 / 赋值**（含易用性重载）：

```cpp
JsonElement n;                    // null
JsonElement b(true);              // bool
JsonElement d(3.14);              // double → 提升为 long double 存储
JsonElement i(42);                // int  → int64_t（精确保真）
JsonElement ll(10000000000LL);    // long long → int64_t
JsonElement u(18446744073709551615ULL); // unsigned long long → uint64_t
JsonElement s(std::string("hi")); // string
JsonElement cs("hi");             // const char* → string（不会误判为 bool）
JsonElement a(JsonArray{1.0,2.0});
JsonElement uo(UnorderedObject{});
JsonElement oo(OrderedObject{});

n = nullptr;  n = true;  n = 42;  n = "str";  n = 3.5;   // 赋值切换类型
```

**类型判断**：

```cpp
el.kind();            // JsonKind::Null/Bool/Number/String/Array/UnorderedObject/OrderedObject
el.CheckKind(JsonKind::Number);
el.isNull(); el.isBool(); el.isNumber(); el.isString();
el.isArray(); el.isUnorderedObject(); el.isOrderedObject();
```

**访问**（容器返回指针，先判空再使用）：

```cpp
bool        b  = el.getBool();
double      x  = el.getNumber();        // 数值 → double（大整数会截断，仅近似）
const std::string& s = el.getString();

JsonArray*         a  = el.asArray();           // 类型不符返回 nullptr
UnorderedObject*   u  = el.asUnorderedObject();
OrderedObject*     o  = el.asOrderedObject();
// 另有 const 版本 asXxx() const
```

**数值精确读取**（`Number` 保真，整数逐级 int64 → uint64 → 大整数）：

```cpp
const Number& num = el.numberValue();      // 直接取 Number（非数值抛）
std::int64_t  i   = el.getInt64();         // 整数精确转 int64；溢出/非整数值抛
std::uint64_t u   = el.getUInt64();        // 整数精确转 uint64；负数/溢出/非整数值抛
int           n   = el.getInt();           // 便捷：按 int 读取（需整数值）
long double   ld  = el.getLongDouble();    // 数值按 long double 返回
const std::string& big = el.getBigIntString(); // 大整数原字符串（非大整数抛）
```

> 解析整数字面量时逐级保精度：`9223372036854775807` → `int64`；`9223372036854775808`…`18446744073709551615` → `uint64`；再大（或负向超出）→ 字符串大整数（规范化、无前导零）。大整数只做存储/比较/序列化，不做算术；与 `int64`/`uint64` 比较精确、与 `long double` 比较近似。`getNumber()` 会截断，精确读取请用上述 API。

**便利 API**：

```cpp
el.value("key", JsonElement(0));     // 对象按 key 取值；缺失/非对象返回默认值，不抛
el.getStringOr("fallback");          // string 安全读取；非 string 返回默认值
el.dump();                           // 序列化别名：紧凑
el.dump(2);                          // 序列化别名：2 空格缩进美化
```

**链式下标访问**（`operator[]`，推荐用于嵌套读写）：

```cpp
// 非 const：对象按 key、数组按下标；null 自动升级为对应容器并创建子元素
UnorderedObject obj;
obj["post"]["Invalid"] = std::string("yes");    // post 缺失 → 自动建对象
obj["post"]["Invalid"].getString();             // "yes"
Serialize(JsonElement(obj));                    // {"post":{"Invalid":"yes"}}

auto doc = Parse("{\"post\":{\"Invalid\":{\"id\":7}}}");
doc["post"]["Invalid"]["id"].getNumber();       // 7（读嵌套）

auto arr = Parse("[[1,2],{\"k\":\"v\"}]");
arr[0][1].getNumber();                          // 2（数组链式）
arr[1]["k"].getString();                        // "v"

// const 版本：只读，key 不存在或类型不符抛异常，不修改
const JsonElement& ce = doc;
ce["post"]["Invalid"]["id"].getNumber();        // 7
```

> 注意：非 const 链式下标在元素缺失或为 `null` 时会**原地创建**（写导向，类似 Python dict 的自动建键）；`const` 版本只读、类型不符或 key 不存在会抛 `std::out_of_range`/`std::runtime_error`。

### 5.2 解析

```cpp
// 抛出版本：失败抛 ParseError（继承 std::runtime_error），
// 携带字节偏移、1 起始行列号与期望符号；what() 前缀保持
// "JSON parse error at offset N: expected X" 以兼容旧调用方
JsonElement Parse(const std::string& text, ParseOption opt = ParseOption::None);

// 非抛出版本：成功返回 true；失败返回 false，err 填原因（含行列信息）
bool TryParse(const std::string& text, JsonElement& out, std::string* err = nullptr);

enum class ParseOption : unsigned int {
    None = 0,
    KeepObjectOrder = 1U << 0,   // 对象产出 OrderedObject（保序）；默认 UnorderedObject
};
```

```cpp
auto a = Parse("[1,2,3]");                                 // 数组
auto o = Parse("{\"b\":2,\"a\":1}");                       // UnorderedObject（键序不保证）
auto oo = Parse("{\"b\":2,\"a\":1}", ParseOption::KeepObjectOrder); // 保序

JsonElement out; std::string err;
if (!TryParse("[1,", out, &err)) std::cerr << err;         // JSON parse error at offset 3: ...
```

**非严格模式（注释 / 尾逗号）与流式 / 文件解析**：

```cpp
// Lenient：容忍 // /* */ # 注释 与 尾逗号
auto doc = Parse(R"({ // 注释
    "a": 1, /* 块注释 */ "b": 2,   // 尾逗号
})", ParseOption::Lenient);

// 可与保序组合
Parse(txt, ParseOption::KeepObjectOrder | ParseOption::Lenient);

// 流式 / 文件（整读后解析）
std::istringstream iss(...);
auto d1 = Parse(iss);
auto d2 = ParseFile("config.json");

// 收集式（当前遇首错即停，接口预留多错误收集）
std::vector<ParseError> issues;
if (!TryParseIssues(txt, out, &issues)) { /* issues 至多一条 */ }
```

`ParseError` 字段：

```cpp
try {
    Parse("{\n  \"a\": 1,\n  \"b\": tru\n}");
} catch (const ParseError& e) {
    e.offset;    // 出错字节偏移（0 起始）
    e.line;      // 行号（1 起始）
    e.column;    // 列号（1 起始，按字节）
    e.expected;  // 期望符号/类别描述，如 "'"、"'\\''"、"'true'"
    e.what();    // "JSON parse error at offset N: expected X"
}
```

### 5.3 序列化

```cpp
std::string Serialize(const JsonElement& el, int indent = -1);
// indent < 0  → 紧凑：{"a":1,"b":2}
// indent >= 0 → 美化（每级缩进 indent 空格）
```

```cpp
std::string s1 = Serialize(doc);          // 紧凑
std::string s2 = Serialize(doc, 2);       // 美化，可再次 Parse 还原
```

**自定义美化配置 `PrettyOptions` 与 `SerializeTo`**：

```cpp
JSON::PrettyOptions opt;
opt.indentStr = "\t";              // 缩进字符串（默认 2 空格）
opt.sortKeys  = true;              // 按键排序输出（Ordered/Unordered 均生效）
opt.emptyContainerCompact = false; // 空容器多行展开 { \n }
opt.indent    = 2;                 // <0 紧凑

std::string buf;
SerializeTo(doc, buf);              // 写入已有 buffer（复用容量，循环调用友好）
SerializeTo(doc, buf, opt);         // 带配置
SerializeToFile(doc, "out.json");   // 写文件（失败返回 false）
SerializeToFile(doc, "out.json", opt);
doc.dump(opt);                      // JsonElement 便捷别名
```

### 5.4 `JsonArray`

组合式 vector，完整转发常用容器接口：

```cpp
JsonArray arr;
arr.reserve(8);
arr.push_back(JsonElement(1.0));
arr.emplace_back(JsonElement(2.0));
arr[0] = 10.0;
arr.at(1).getNumber();
arr.size(); arr.empty(); arr.front(); arr.back(); arr.pop_back();

// 构造
JsonArray a1;                                    // 空
JsonArray a2(4);                                 // 4 个 null
JsonArray a3(2, JsonElement(0.0));               // 2 个 0.0
JsonArray a4{1.0, 2.0, 3.0};                     // 初始化列表
std::vector<JsonElement> v; JsonArray a5(std::move(v)); // 由 vector 回包
```

`begin()/end()/cbegin()/cend()` 均可用，支持 range-for。

### 5.5 `UnorderedObject`（无序对象）

```cpp
UnorderedObject o;
o["k1"] = 1.0;                       // 不存在则插入
o["k2"] = std::string("v");
o.contains("k1");                    // true
auto it = o.find("k1");              // iterator，可用 it->first / it->second
o.erase("k1");  o.clear();

o.underlyingMap().reserve(64);       // 直接操作底层 unordered_map

for (auto& kv : o) { kv.first; kv.second; }
```

### 5.6 `OrderedObject`（有序对象）

保插入顺序，支持按下标访问、迭代、删除、切片。

```cpp
OrderedObject o;
o["z"] = 1.0;  o["a"] = 2.0;  o["m"] = 3.0;   // 键序：z,a,m

o.at(0).getNumber();        // 1.0（按下标，插入顺序）
o.contains("a");            // true
o.erase("a");               // 墓碑删除（惰性 compact）
o.size();                   // 有效元素数

// 迭代得到 (key, value) 对，保持插入顺序、跳过已删
for (auto& kv : o) std::cout << kv.first << "=" << kv.second.getNumber() << "\n";

// 重新插入已删键（墓碑复用）
o["a"] = 99.0;

// 增强：find 返回迭代器、反向迭代、范围删除
auto it = o.find("z");                     // 未命中返回 end()；it->second
for (auto rit = o.rbegin(); rit != o.rend(); ++rit)   // 反向（跳过墓碑）
    std::cout << rit->first << "\n";
o.erase(o.find("a"), o.find("m"));         // 范围删除 [first, last)
```

### 5.9 容器 / 结构体互操作（`JsonInterop.hpp`）

独立的扩展头，`#include "JsonInterop.hpp"` 后即可使用：

```cpp
// 内置类型与 STL 容器
JsonElement j = ToJson(std::vector<int>{1, 2, 3});   // [1,2,3]
auto v = FromJson<std::vector<int>>(Parse("[4,5]"));
auto m = FromJson<std::map<std::string, int>>(Parse("{\"a\":1}"));

// 自定义类型：提供 ADL 的 to_json / from_json 即可被自动发现
struct Point { int x, y; };
void to_json(JsonElement& el, const Point& p) { el = JSON::UnorderedObject{}; el["x"] = p.x; el["y"] = p.y; }
void from_json(const JsonElement& el, Point& p) { p.x = el["x"].getInt(); p.y = el["y"].getInt(); }

// 或直接用反射宏（键名为字段名，缺失字段保持默认值）
struct Person { std::string name; int age; std::optional<std::string> nick; };
JSON_DEFINE_STRUCT(Person, name, age, nick)
Person p{"Tom", 30, std::nullopt};
auto jp = ToJson(p);                       // {"name":"Tom","age":30,"nick":null}
auto back = FromJson<Person>(jp);
```

### 5.7 切片代理

数组与有序对象都支持 `operator[](const Slice&)`，返回可写 `JsonSliceRef` / 只读 `JsonSliceCRef`：

```cpp
auto* arr = Parse("[0,1,2,3,4,5,6,7,8,9]").asArray();

// 读切片 → 拷贝出新 vector
std::vector<JsonElement> mid = (*arr)[Slice(1, 4)].toContainer();   // {1,2,3}

// 写切片（把 [1,4) 三个位置替换为给定值）
(*arr)[Slice(1, 4)] = std::vector<JsonElement>{9.0, 9.0, 9.0};

// 切片内按下标访问
auto& e = (*arr)[Slice(0, 6)][2];            // 取切片第 2 个元素

// 链式复合切片
auto comp = (*arr)[Slice(0, 6)][Slice("[1:3]")].toContainer();  // 先取[0,6)，再取其[1:3)

// 只读引用
const JsonArray* carr = /* ... */;
auto cref = (*carr)[Slice(1, 4)];            // JsonSliceCRef
std::vector<JsonElement> c = cref.toContainer();

// OrderedObject 同样支持
auto os = oo[Slice(1, 3)].toContainer();
```

### 5.8 对象互转

```cpp
UnorderedObject u = ToUnordered(orderedObj);   // OrderedObject → UnorderedObject
OrderedObject   o = ToOrdered(unorderedObj);   // UnorderedObject → OrderedObject（按原 unordered 迭代序）
```

### 5.10 YAML 导入 / 导出（`JsonYaml.hpp`）

```cpp
#include "JsonYaml.hpp"   // 链接时加入 JsonYaml.cpp

// 解析 YAML → JsonElement（失败抛 YamlParseError，携带行列）
auto doc = ParseYaml(
    "server:\n"
    "  host: localhost\n"
    "  port: 8080\n"
    "  tags:\n"
    "    - web\n"
    "    - api\n");
doc["server"]["host"].getString();   // "localhost"
doc["server"]["tags"].asArray();     // ["web", "api"]

// 其他入口
std::istringstream iss("k: 1\n");
JsonElement a = ParseYaml(iss);          // std::istream
JsonElement b = ParseYamlFile("cfg.yml");// 文件
JsonElement out; std::string err;
if (!TryParseYaml("a: [1, 2", out, &err)) std::cerr << err;  // 非抛出

// 序列化 JsonElement → YAML（indent 控制缩进步长）
std::string yaml = ToYaml(doc, 2);

// 往返保证：ToYaml 输出可再 ParseYaml 还原；字符串自动加引号防歧义
```

支持的 YAML 子集（文档化边界）：块状映射 / 块状序列（缩进驱动）、流式内联 `[...]` 与 `{...}`（裸 key、单/双引号、尾逗号）、注释 `#`、文档标记 `---`/`...`、标量类型推断（`null`/`bool`/整数→大整数/浮点/字符串）、空值、嵌套容器的值换行缩进。**不支持**：锚点/别名、标签、多行块标量 `|`/`>`、tab 缩进、折叠标量、强制类型标记（遇到按字符串或报错处理）。

---

## 6. Python 风格切片（重点）

> 语义已**统一为 Python 切片**，避免数字形式与字符串形式歧义。核心规则：切片在**知道容器长度时**按 Python 规则归一化，而非预先按字面值展开。

### 6.1 三种写法

| 写法 | 含义 | 示例 |
|---|---|---|
| `Slice(start, end, step=1)` | Python 区间（负边界按长度归一） | `Slice(1, -1)` = 去首尾；`Slice(-2, 0)` = **空** |
| `Slice("[start:end:step]")` | 完整 Python 表达式（支持省略 / 负步长） | `Slice("[::-1]")` = 反转 |
| `Slice{1,3,5}` / `Slice(vector)` | **显式下标集合**（负下标逐元素从尾部计数） | `Slice{-2,-1}` = 末尾 2 个 |

### 6.2 规则（与 Python 一致）

- 负索引按长度归一化：`a[-1]` = 末元素；`a[-2:]` = 末尾 2 个。
- 省略边界：`[1:]` 从 1 到末尾；`[:5]` 从 0 到 4；`[:]` 全部。
- 步长可为负（反转）：`[::-1]` 逆序；`[::-2]` 逆序隔 1。
- 越界自动截断：`a[0:1000]` 只取到末尾；`a[-100:5]` 从 0 开始。
- 边界归一化后若区间为空，结果为**空**（不抛错、不越界）：`a[-2:0]`、`a[5:2]` 均为空。
- `step == 0` 抛 `std::invalid_argument`；非法字符串（`"[a]"`）抛 `std::invalid_argument`。

### 6.3 对照表（`a = [0,1,2,3,4,5,6,7,8,9]`，len=10）

| 表达式 | 结果 |
|---|---|
| `Slice(1, 4)` / `Slice("[1:4]")` | `{1,2,3}` |
| `Slice("[1:]")` | `{1,2,3,4,5,6,7,8,9}` |
| `Slice("[:5]")` | `{0,1,2,3,4}` |
| `Slice("[::-1]")` | `{9,8,7,6,5,4,3,2,1,0}` |
| `Slice("[::-2]")` | `{9,7,5,3,1}` |
| `Slice(1, -1)` / `Slice("[1:-1]")` | `{1,2,3,4,5,6,7,8}`（去首尾） |
| `Slice("[-2:]")` | `{8,9}`（末尾 2 个） |
| `Slice("[-2:6]")` | `{}`（Python 一致：-2→8，8>6 空） |
| `Slice("[-8:-2]")` | `{2,3,4,5,6,7}` |
| `Slice("[1:-1:2]")` | `{1,3,5,7}` |
| `Slice("[3]")` | `{3}`（单下标） |
| `Slice("[-1]")` | `{9}`（末元素） |
| `Slice(0, 10, 2)` | `{0,2,4,6,8}` |
| `Slice(9, 0, -1)` | `{9,8,7,6,5,4,3,2,1}` |
| `Slice(-2, 0)` | `{}`（Python `a[-2:0]` = 空） |
| `Slice{-2, -1}` | `{8,9}`（显式下标，末尾 2 个） |
| `Slice{1, 3, 5}` | `{1,3,5}`（显式集合） |

> ⚠️ **注意**：`Slice(-2, 0)` 在 Python 语义下是**空切片**（不是"末尾 2 个"）。要"末尾 N 个"请用 `Slice("[-N:]")` 或显式列表 `Slice{-N, ...}`。

### 6.4 为什么之前会崩溃（已修复，供参考）

早期 `Slice` 有一个无约束的模板迭代器构造 `Slice(Iter, Iter)`。在 64 位平台（`index_t`=long ≠ int）上，`Slice(-2, 0)` 会以**精确匹配**命中该模板，把 `-2`、`0` 当"迭代器对"去构造 `std::vector`，`-2` 被当作**元素个数**（约 2⁶⁴ 个）而 `bad_alloc`/崩溃。

修复：给该模板加 SFINAE（`enable_if_t<!is_integral_v<Iter>>`），整型实参只匹配 `Slice(start,end,step)` 的三元构造。此后 `Slice(a,b)` 在任何平台都确定性地走 Python 语义，不再有歧义。

---

## 7. 性能

| 操作 | 规模 | 实测耗时（Release，64 位 MinGW） |
|---|---|---|
| `OrderedObject` 构建（`o["k"+i]=i`） | 200,000 键 | ≈ 110–180 ms（O(N)） |
| 数组解析 | 50,000 元素 | ≈ 5–7 ms |

设计要点：

- **`OrderedObject` 为 O(N) 构建**：有效下标缓存采用"脏标记 + 惰性重建"，只在 `at(idx)` 需要时才重建；插入/删除摊还 O(1)。若每次插入都重建缓存，200k 键会退化到分钟级（O(N²)）——单元测试含性能回归门拦截该退化。
- **`double` 最短往返**：序列化用 `to_chars`，输出可往返表示（`0.1` → `0.1`，非 `0.100000000000000005`）。
- **`UnorderedObject`** 基于 `std::unordered_map`，键访问 O(1)（摊还，常数大）。
- 32 位 x87 扩展精度注意：`-O2` 下 `Parse("3.14").getNumber() == 3.14` 因字面量按 80 位比较可能为 `false`；比较浮点请用容差（测试用 `near()`/`nearVal()`）。

---

## 8. 单元测试

```bash
# 直接 g++
g++ -std=c++17 -Wall -Wextra -O2 JsonCore.cpp Number.cpp JsonYaml.cpp tests/JsonCoreTests.cpp -I. -o JsonCoreTests && ./JsonCoreTests

# 或 CMake + ctest
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
```

- 覆盖：标量/空白/转义/容器/对象顺序/解析错误、`OrderedObject`（含反向迭代/范围删除）、切片（含 Python 切片）、序列化往返、`TryParse`/`ParseError` 行列、`Lenient` 容忍解析、流式/文件解析、对象互转、易用性重载、容器/结构体互操作、**YAML 导入导出（块状/流式/注释/大整数保真/往返）**、性能回归门。
- 当前断言数：**316**（含 19 条 Python 切片用例）。
- 建议扩展时**先加测试再加实现**，并保持 `-Wall -Wextra -Wpedantic` 零警告。

---

## 9. 实现要点与注意事项

- **循环依赖**：`JsonSliceRef` / `JsonSliceCRef` 模板体涉及 `JsonElement` 的成员是**模板成员**，只在实例化时才需要完整类型；特化体放在 `JsonElement` 定义之后。
- **`OrderedObject` 并行数组**：用 `m_keys / m_values / m_deleted / m_keyIndex` 四数组，避免直接持有含 `JsonElement` 的结构体造成定义顺序问题；墓碑超 25% 触发 `compact()`。
- **`asXxx()` 返回指针**：类型不符返回 `nullptr`，先判空再使用，否则解引用空指针是 UB。
- **`operator[]`（下标）不做越界检查**，`at()` 会抛 `std::out_of_range`。
- **`Serialize` 的 `indent`**：`-1`（默认）紧凑，`>=0` 美化；美化输出可再次 `Parse` 还原。
- **数值类型 `Number`**：整数按 `int64 → uint64 → 字符串大整数` 逐级保精度存储，浮点提升为 `long double`；大整数只做存储/比较/序列化（与 `int64`/`uint64` 比较精确、与 `long double` 近似）。`getNumber()` 会截断为 double，精确读取用 `getInt64()/getUInt64()/getLongDouble()/getBigIntString()`。

---

## 10. 未来拓展方向

以下为建议的增量演进路线，均可逐条实现并补充测试：

1. **大整数 / 整数保真**：✅ **已实现** — `Number` 类型（`variant<int64_t, uint64_t, long double, string 大整数>`），解析与构造逐级保精度，跨类型比较、精确读取 API 齐备（见 5.1）。
2. **序列化 API 增强**：✅ **已实现** — `SerializeTo`（buffer 复用/`FILE*`/文件路径）与 `PrettyOptions`（缩进字符串、键排序、空容器策略）齐备（见 5.3）。
3. **解析增强**：✅ **已实现** — `ParseOption::Lenient`（`//`/`/* */`/`#` 注释 + 尾逗号）、`Parse(istream)` / `ParseFile`、浮点 `long double` 存储（见 5.2）；真正的逐 token 增量拉取留作后续。
4. **便利 API**：**`JsonElement` 的 `operator[]`（按 key / 下标）统一链式访问、`.value(key, default)`、`.getInt()`、`.getStringOr(...)`、`dump()` 均已实现**（见 5.1 链式下标访问与便利 API）。
5. **错误分类**：**`ParseError` 携带行列号与期望符号已实现**（见 5.2）；`TryParseIssues` 接口预留多错误收集，错误恢复策略随增量解析落地。
6. **性能**：✅ **已实现** — 序列化 buffer 复用（`SerializeTo`）、数字 `from_chars`/`to_chars`、对象序列化默认路径零拷贝迭代；`std::string` SSO 天然提供小字符串优化。字符串池与 `simdjson` 式解析路径留作后续评估。
7. **JSON Pointer / Patch / Schema**：`/a/b/0` 路径寻址，RFC 6902 Patch，最小 Schema 校验。
8. **`OrderedObject` 增强**：✅ **已实现** — `find(key)` 返回迭代器、`rbegin`/`rend` 反向迭代、`erase(first,last)` 范围删除（见 5.6）。
9. **容器互操作**：✅ **已实现** — `JsonInterop.hpp`：`ToJson`/`FromJson`（`vector`/`map`/`unordered_map`/`optional`/`pair` + ADL 自定义类型）+ `JSON_DEFINE_STRUCT` 反射宏（见 5.9）。
10. **工具链**：加入 `-Werror` 严格门、Sanitizer（ASan/UBSan）回归、跨平台 CI、基准基准脚本。
11. **YAML 扩展**：✅ **已实现（务实子集）** — `JsonYaml.hpp/cpp`：`ParseYaml`/`ToYaml`（块状+流式、注释、大整数保真）（见 5.10）。后续可加锚点/别名、多行块标量 `|`/`>`、折叠标量、强制类型标记。

> 每个方向落地时，请保持"头文件声明 + cpp 实现 + 单元测试"的结构，并在本文档对应章节同步更新用法。
