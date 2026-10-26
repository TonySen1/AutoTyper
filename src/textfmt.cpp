#include "textfmt.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace at {

bool DecodeBytes(const std::string& raw, std::wstring& out,
                 TextEncoding* detected)
{
    const unsigned char* p = (const unsigned char*)raw.data();
    size_t n = raw.size();

    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {          // UTF-16 LE BOM
        size_t cnt = (n - 2) / sizeof(wchar_t);
        out.assign((const wchar_t*)(p + 2), cnt);
        if (detected) *detected = TextEncoding::Utf16LE;
        return true;
    }
    if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {          // UTF-16 BE BOM
        size_t cnt = (n - 2) / 2;
        out.clear();
        out.reserve(cnt);
        for (size_t i = 0; i < cnt; ++i) {
            unsigned short v = (unsigned short)((p[2 + i * 2] << 8) | p[3 + i * 2]);
            out.push_back((wchar_t)v);
        }
        if (detected) *detected = TextEncoding::Utf16BE;
        return true;
    }

    size_t off = 0;
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) off = 3;

    if (n == off) {
        out.clear();
        if (detected) *detected = TextEncoding::Utf8;
        return true;
    }

    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   raw.data() + off, (int)(n - off), nullptr, 0);
    if (need > 0) {
        out.resize((size_t)need);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            raw.data() + off, (int)(n - off), &out[0], need);
        if (detected) *detected = TextEncoding::Utf8;
        return true;
    }

    need = MultiByteToWideChar(CP_ACP, 0, raw.data() + off, (int)(n - off), nullptr, 0);
    if (need <= 0) return false;
    out.resize((size_t)need);
    MultiByteToWideChar(CP_ACP, 0, raw.data() + off, (int)(n - off), &out[0], need);
    if (detected) *detected = TextEncoding::Ansi;
    return true;
}

bool ReadTextFile(const std::wstring& path, std::wstring& out,
                  TextEncoding* detected)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (64LL << 20)) {
        CloseHandle(h);
        return false;
    }

    std::string buf((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    BOOL ok = buf.empty() ? TRUE
                          : ReadFile(h, &buf[0], (DWORD)buf.size(), &got, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    buf.resize(got);
    return DecodeBytes(buf, out, detected);
}

std::wstring Unescape(const std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[++i];
            if (n == L'n') out.push_back(L'\n');
            else if (n == L'r') out.push_back(L'\r');
            else if (n == L't') out.push_back(L'\t');
            else if (n == L'\\') out.push_back(L'\\');
            else { out.push_back(L'\\'); out.push_back(n); }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty()) return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                                   nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                        &out[0], need, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   s.data(), (int)s.size(), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        s.data(), (int)s.size(), &out[0], need);
    return out;
}

size_t CountCodePoints(const std::wstring& s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size()) {
            wchar_t d = s[i + 1];
            if (d >= 0xDC00 && d <= 0xDFFF) { ++i; }
        }
        ++n;
    }
    return n;
}

} // namespace at
