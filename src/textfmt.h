// ============================================================
//  文本编解码 / 转义
//  与 GUI 解耦，便于单元测试。
// ============================================================
#pragma once

#include <string>

namespace at {

enum class TextEncoding {
    Utf8,
    Utf16LE,
    Utf16BE,
    Ansi,
};

// 自动识别 BOM / UTF-8 / 系统 ANSI(GBK)，行为与 v1.0.0 一致。
// detected 可为空；解码成功时写入识别出的编码。
bool DecodeBytes(const std::string& raw, std::wstring& out,
                 TextEncoding* detected = nullptr);

// 读取文本文件并解码。上限 64 MiB，与旧实现一致。
bool ReadTextFile(const std::wstring& path, std::wstring& out,
                  TextEncoding* detected = nullptr);

// 命令行 -c 参数的反斜杠转义（换行/回车/制表/反斜杠）
std::wstring Unescape(const std::wstring& s);

// 工具函数
std::string  WideToUtf8(const std::wstring& s);
std::wstring Utf8ToWide(const std::string& s);

// UTF-16 码点数量（代理对记 1 个）
size_t CountCodePoints(const std::wstring& s);

} // namespace at
