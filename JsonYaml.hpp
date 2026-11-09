// ============================================================================
// JsonYaml.hpp — JsonLib 的 YAML 导入 / 导出（务实子集）
//
// 在 JSON 之外提供一组与 std::string / 流 / 文件互通的 YAML 转换函数，
// 全部建立在 JsonCore 的公开 API 之上（不侵入 JsonElement 内部）。
//
//   JsonElement ParseYaml(text)              // YAML 文本 → JsonElement
//   JsonElement ParseYaml(istream)           // 从输入流读取（整读后解析）
//   JsonElement ParseYamlFile(path)          // 从文件读取
//   bool        TryParseYaml(text, out, err) // 非抛出版本
//   std::string ToYaml(el, indent = 2)       // JsonElement → YAML 文本
//
// 解析失败抛出 YamlParseError（携带行列与期望说明）。
//
// == 支持的 YAML 子集（文档化边界，未来可在此扩展）==
//  - 块状映射：key: value（缩进驱动层级）
//  - 块状序列：- item（item 可为标量 / 行内映射 / 嵌套块 / 流式）
//  - 流式内联：[...] 与 {a: 1, b: [x]}（裸 key、单/双引号、嵌套、尾逗号）
//  - 注释：# 整行 与 行内 " #"；文档标记 --- / ...
//  - 标量类型推断：null(~ null)、bool(true/false/yes/no/on/off)、整数
//    （int64 → uint64 → 字符串大整数，复用 Number 保精度）、浮点、字符串
//  - 空值：key:（后无嵌套行 → null）；- （后无嵌套行 → null）
//  - 嵌套容器的值换行缩进
//  - 不支持：锚点(&)/别名(*)、标签(!!str)、多行块标量(| >)、tab 缩进、
//    折叠标量、强制类型标记。这些遇到时按字符串或报错处理。
//
// 注意：块序列项的行内映射 `- name: web` 及其后同缩进 `port: 80` 按
// YAML 语义组成同一映射（键列 = 序列项缩进 + 2）。映射/序列默认产出
// UnorderedObject / JsonArray（与 Parse 默认一致）。
// ============================================================================
#pragma once

#include <string>
#include <istream>
#include <stdexcept>

#include "JsonCore.hpp"

namespace JSON {

    // YAML 解析错误：与 ParseError 对齐的字段
    class YamlParseError : public std::runtime_error {
    public:
        size_t      offset;    // 出错字节偏移（0 起始，行内估算）
        size_t      line;      // 1 起始行号
        size_t      column;    // 1 起始列号（按字节）
        std::string expected;  // 期望说明

        YamlParseError(size_t off, size_t ln, size_t col, std::string exp, std::string msg)
            : std::runtime_error(std::move(msg)),
              offset(off), line(ln), column(col), expected(std::move(exp)) {}
    };

    // 解析 YAML 文本 → JsonElement。失败抛 YamlParseError。
    JsonElement ParseYaml(const std::string& text);

    // 从输入流整读后解析（同 JSON 流式入口语义）
    JsonElement ParseYaml(std::istream& is);

    // 从文件读取并解析；失败抛 YamlParseError（读文件失败抛 std::runtime_error）
    JsonElement ParseYamlFile(const char* path);

    // 非抛出版本；失败返回 false，err（若提供）填入原因。
    bool TryParseYaml(const std::string& text, JsonElement& out, std::string* err = nullptr);

    // 序列化 JsonElement → YAML 文本。indent < 0 时使用 0（最小缩进）。
    // 输出保持 "可再 ParseYaml 还原" 的往返特性。
    std::string ToYaml(const JsonElement& el, int indent = 2);

} // namespace JSON
