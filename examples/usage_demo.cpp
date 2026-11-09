// JsonLib 完整用法示例（编译运行：g++ -std=c++17 usage_demo.cpp JsonCore.cpp Number.cpp JsonYaml.cpp -I..）
#include "../JsonCore.hpp"
#include "../JsonYaml.hpp"
#include <iostream>
#include <sstream>

using namespace JSON;

static void print(const std::string& tag, const std::string& s) {
    std::cout << "[" << tag << "] " << s << "\n";
}

int main() {
    // ========== 1. 构造 JsonElement ==========
    JsonElement n;                 // null
    JsonElement b(true);           // bool
    JsonElement num(3.14);         // double
    JsonElement str(std::string("hi")); // string
    JsonElement arr(JsonArray{ JsonElement(1.0), JsonElement(2.0) });
    JsonElement obj(UnorderedObject{});

    // 赋值切换类型
    n = 42.0;
    n = std::string("now a string");
    n = nullptr;                   // 回到 null

    // kind / 判断
    std::cout << "kind check: " << Parse("true").isBool()
        << " " << Parse("[1]").isArray() << "\n";

    // ========== 2. 解析 Parse / TryParse ==========
    // 默认：对象产出 UnorderedObject
    auto doc = Parse("{\"name\":\"Tom\",\"age\":30,\"tags\":[\"c++\",\"json\"]}");
    print("unord", Serialize(doc));

    // 保序：产出 OrderedObject
    auto odoc = Parse("{\"z\":1,\"a\":2,\"m\":3}", ParseOption::KeepObjectOrder);
    print("ordered", Serialize(odoc));

    // 非抛出版本
    JsonElement out;
    std::string err;
    bool ok = TryParse("[1,2", out, &err);
    std::cout << "TryParse bad: " << ok << " err=" << err << "\n";

    // 抛出版本（异常带位置）
    try { Parse("{a:1}"); }
    catch (const std::exception& e) { std::cout << "Parse threw: " << e.what() << "\n"; }

    // ========== 3. 读取（UnorderedObject）==========
    auto* uo = doc.asUnorderedObject();
    if (uo) {
        (*uo)["age"] = 31;                      // 修改
        print("age", std::to_string((*uo)["age"].getNumber()));
        print("contains", uo->contains("name") ? "yes" : "no");
        // 迭代
        for (auto& kv : *uo) std::cout << "  key=" << kv.first << "\n";
    }

    // ========== 4. 读取（OrderedObject：保序 + 按下标访问）==========
    auto* oo = odoc.asOrderedObject();
    if (oo) {
        std::cout << "at(0)=" << oo->at(0).getNumber()
            << " at(1)=" << oo->at(1).getNumber() << "\n"; // 按下标（插入顺序）
        oo->erase("a");                         // 删除（墓碑 + 惰性 compact）
        std::cout << "after erase size=" << oo->size() << "\n";
        (*oo)["a"] = 99.0;                      // 墓碑复用重新插入
        print("reinsert", Serialize(*oo));
    }

    // ========== 5. 构建 / 修改容器 ==========
    JsonArray arr2;
    arr2.reserve(4);
    arr2.push_back(JsonElement(1.0));
    arr2.emplace_back(JsonElement(2.0));
    arr2[1] = 20.0;                            // 下标写
    arr2.push_back(std::string("x"));
    arr2.push_back(JsonElement(true));

    UnorderedObject uo2;
    uo2["k1"] = 1.0;
    uo2["k2"] = std::string("v");
    uo2.underlyingMap().reserve(64);           // 预分配

    OrderedObject oo2;
    for (int i = 0; i < 3; ++i) oo2["key" + std::to_string(i)] = double(i);

    // ========== 6. 切片 Slice ==========
    JsonElement big = Parse("[0,1,2,3,4,5,6,7,8,9]");
    auto* ba = big.asArray();

    // 区间切片 [1,4)
    auto mid = (*ba)[Slice(1, 4)].toContainer();
    print("slice[1:4]", Serialize(JsonElement(JsonArray(mid))));

    // 步长切片 0,2,4,6,8
    auto even = (*ba)[Slice(0, 10, 2)].toContainer();
    std::cout << "even count=" << even.size() << "\n";

    // 负索引（末尾 2 个）：Python 语义下 Slice(-2, 0) == a[-2:0] == 空，
    // 故“末尾 N 个”用字符串形式 Slice("[-N:]") 或显式列表 Slice{-N, ...}
    auto last2 = (*ba)[Slice("[-2:]")].toContainer();
    std::cout << "last2 count=" << last2.size() << "\n";

    // 字符串形式切片
    auto fromStr = (*ba)[Slice("[2:5]")].toContainer();
    std::cout << "str-slice count=" << fromStr.size() << "\n";

    // 显式下标列表
    auto pick = (*ba)[Slice{ 0, 3, 5 }].toContainer();
    std::cout << "pick count=" << pick.size() << "\n";

    // 写切片：把 [1,4) 位置替换
    (*ba)[Slice(1, 4)] = std::vector<JsonElement>{ JsonElement(9.0), JsonElement(9.0), JsonElement(9.0) };
    print("after write", Serialize(big));

    // OrderedObject 也支持切片
    auto ooSlice = oo2[Slice(1, 3)].toContainer();
    std::cout << "oo slice count=" << ooSlice.size() << "\n";

    // ========== 7. 序列化 Serialize ==========
    print("compact", Serialize(odoc));
    print("pretty", Serialize(odoc, 2));

    // ========== 8. 对象互转 ==========
    auto fromOrdered = ToUnordered(*odoc.asOrderedObject());
    auto backOrdered = ToOrdered(fromOrdered);
    std::cout << "toUnordered->toOrdered size=" << backOrdered.size() << "\n";

    // ========== 9. 典型业务用法 ==========
    // 从 API 响应解析 + 取值
    auto resp = Parse("{\"code\":0,\"data\":{\"items\":[{\"id\":1,\"name\":\"a\"},{\"id\":2,\"name\":\"b\"}]}}");
    auto* data = resp.asUnorderedObject();
    auto* items = (*data)["data"].asUnorderedObject();
    auto& itemsArr = *(*items)["items"].asArray();
    for (auto& it : itemsArr) {
        auto* o = it.asUnorderedObject();
        std::cout << "item " << (*o)["id"].getNumber()
            << " = " << (*o)["name"].getString() << "\n";
    }

    auto dobj = Parse(R"({"name":"WanMario", "age":15, "onWork":true, "hobbies":["Program","Sport"], "post":{"Invalid": true, "3-lvlobj":{"des":"Hi"}}})");

    // ========== 10. YAML 导入 / 导出 ==========
    auto ydoc = ParseYaml(
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n"
        "  enabled: true\n"
        "  tags:\n"
        "    - web\n"
        "    - api\n"
        "  big: 123456789012345678901234567890\n");
    std::cout << "yaml host = " << ydoc["server"]["host"].getString() << "\n";
    std::cout << "yaml tags[1] = " << (*ydoc["server"]["tags"].asArray())[1].getString() << "\n";
    std::cout << "yaml big = " << ydoc["server"]["big"].getBigIntString() << "\n";
    print("yaml_out", ToYaml(ydoc, 2));

    // YAML → JSON 互通
    print("yaml_as_json", Serialize(ydoc));
    auto backFromYaml = ParseYaml(ToYaml(ydoc));   // 往返
    std::cout << "yaml roundtrip host = " << backFromYaml["server"]["host"].getString() << "\n";

    std::istringstream iss("k: 1\nlist:\n  - x\n");
    auto fromStream = ParseYaml(iss);
    std::cout << "yaml from istream k = " << fromStream["k"].getInt() << "\n";

    return 0;
}
