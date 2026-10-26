// ============================================================
//  typing 引擎自动化测试
//  自建隐藏宿主窗口 + EDIT 控件，注入后用 WM_GETTEXT 回读比对。
//  覆盖：ASCII / 中文 / Emoji 代理对 / 换行 / Tab / CRLF / 编码
//  用法: typing_test.exe [--verbose]
//  退出码 0 = 全部通过
// ============================================================
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../src/textfmt.h"
#include "../src/typing.h"

static int g_pass = 0;
static int g_fail = 0;
static bool g_verbose = false;

static void Check(bool ok, const char* name, const std::wstring& detail = L"")
{
    if (ok) {
        ++g_pass;
        if (g_verbose) std::printf("[PASS] %s\n", name);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s", name);
        if (!detail.empty()) {
            char buf[1024] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, detail.c_str(), (int)detail.size(),
                                buf, sizeof(buf) - 1, nullptr, nullptr);
            std::printf("  detail=%s", buf);
        }
        std::printf("\n");
    }
}

static HWND g_host = nullptr;
static HWND g_edit = nullptr;

static LRESULT CALLBACK HostProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_edit = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN,
            0, 0, 600, 400, hWnd, (HMENU)1, nullptr, nullptr);
        return 0;
    case WM_SIZE:
        MoveWindow(g_edit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// 用 AttachThreadInput 绕过前台锁定（不放行会静默失败）
static bool ForceForeground(HWND hWnd)
{
    if (!hWnd) return false;
    if (GetForegroundWindow() == hWnd) {
        SetFocus(g_edit);
        return true;
    }

    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myThread = GetCurrentThreadId();

    bool attached = false;
    if (fgThread && fgThread != myThread)
        attached = AttachThreadInput(myThread, fgThread, TRUE) != FALSE;

    ShowWindow(hWnd, SW_SHOW);
    BringWindowToTop(hWnd);
    SetForegroundWindow(hWnd);
    SetActiveWindow(hWnd);
    SetFocus(g_edit);

    if (attached)
        AttachThreadInput(myThread, fgThread, FALSE);

    return GetForegroundWindow() == hWnd;
}

static bool CreateHost()
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HostProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AutoTyperUnitTestHost";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    g_host = CreateWindowExW(WS_EX_TOOLWINDOW, L"AutoTyperUnitTestHost",
                             L"AutoTyperUnitTestHost", WS_OVERLAPPEDWINDOW,
                             0, 0, 640, 480, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);

    if (!g_host) return false;

    ShowWindow(g_host, SW_SHOW);
    UpdateWindow(g_host);
    return ForceForeground(g_host);
}

static void DestroyHost()
{
    if (g_host) { DestroyWindow(g_host); g_host = nullptr; g_edit = nullptr; }
}

static std::wstring ReadBack()
{
    int len = GetWindowTextLengthW(g_edit);
    std::wstring buf((size_t)len + 1, L'\0');
    int got = GetWindowTextW(g_edit, &buf[0], len + 1);
    buf.resize(got > 0 ? (size_t)got : 0);
    return buf;
}

static void ClearEdit()
{
    SetWindowTextW(g_edit, L"");
    // 让 EDIT 的撤销缓冲与选区归零，避免与上一轮串味
    SetFocus(g_edit);
}

static std::wstring Normalize(const std::wstring& s)
{
    std::wstring r;
    r.reserve(s.size());
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        r.push_back(c);
    }
    return r;
}

// 启动一个消息泵，让目标窗口处理完注入事件
static void PumpFor(int ms)
{
    DWORD start = GetTickCount();
    MSG msg;
    while (GetTickCount() - start < (DWORD)ms) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(5);
    }
}

struct Case {
    const char* name;
    std::wstring text;
    bool humanize;
};

static void RunInjectCase(const Case& c)
{
    // 每例重新抢前台：中途任何弹窗/焦点变化都要纠正，否则断言无意义
    if (!ForceForeground(g_host)) {
        Check(false, c.name, L"无法取得前台焦点，本用例未执行");
        return;
    }
    ClearEdit();
    PumpFor(80);

    at::TypingOptions opts;
    opts.intervalMs   = 0;          // 测试求快
    opts.checkUipi    = false;      // 目标是自身，无需预检
    opts.forceEnglish = false;
    opts.humanize.enabled = c.humanize;
    opts.humanize.meanMs = 5;
    opts.humanize.sigmaMs = 0;
    opts.humanize.minMs = 0;
    opts.humanize.maxMs = 10;

    at::TypingReport rep = at::TypeText(c.text, opts, nullptr);
    PumpFor(c.humanize ? 400 : 200);

    std::wstring got = ReadBack();
    bool ok = (Normalize(got) == Normalize(c.text)) &&
              rep.status == at::TypingStatus::Ok &&
              rep.blockedEvents == 0;

    std::wstring detail;
    if (!ok) {
        detail = L"expected=[" + Normalize(c.text) + L"] actual=[" + Normalize(got) + L"]";
        wchar_t st[64];
        swprintf_s(st, L" status=%d injected=%d/%d",
                   (int)rep.status, rep.injectedPoints, rep.totalCodePoints);
        detail += st;
    }
    Check(ok, c.name, detail);
}

// ---------------- 纯逻辑测试（不依赖 GUI） ----------------
static void TestBuildUnits()
{
    {
        auto u = at::BuildUnits(L"a\r\nb\nc\rd\te");
        // a, RETURN, b, RETURN, c, RETURN, d, TAB, e
        bool ok = u.size() == 9;
        if (ok) {
            ok = u[0].kind == at::TypingUnit::Unicode && u[0].ch == L'a';
            ok = ok && u[1].kind == at::TypingUnit::VirtualKey && u[1].vk == VK_RETURN;
            ok = ok && u[2].ch == L'b';
            ok = ok && u[3].vk == VK_RETURN;
            ok = ok && u[4].ch == L'c';
            ok = ok && u[5].vk == VK_RETURN;
            ok = ok && u[6].ch == L'd';
            ok = ok && u[7].vk == VK_TAB;
            ok = ok && u[8].ch == L'e';
        }
        Check(ok, "BuildUnits: CRLF/CR/LF/Tab 归一");
    }
    {
        // 😀 = U+1F600 = D83D DE00
        auto u = at::BuildUnits(L"😀");
        bool ok = u.size() == 2 &&
                  u[0].highSurrogate && u[0].ch == (wchar_t)0xD83D &&
                  u[1].lowSurrogate  && u[1].ch == (wchar_t)0xDE00;
        Check(ok, "BuildUnits: Emoji 代理对标记");
    }
    {
        auto u = at::BuildUnits(L"");
        Check(u.empty(), "BuildUnits: 空串");
    }
}

static void TestCountCodePoints()
{
    Check(at::CountCodePoints(L"abc") == 3, "CountCodePoints: ASCII");
    Check(at::CountCodePoints(L"中文") == 2, "CountCodePoints: 中文");
    Check(at::CountCodePoints(L"😀") == 1, "CountCodePoints: Emoji 记 1");
    Check(at::CountCodePoints(L"a😀中") == 3, "CountCodePoints: 混合");
}

static void TestAutoBatch()
{
    at::TypingOptions o;
    o.intervalMs = 0;
    Check(at::AutoBatchChars(o) > 1, "AutoBatch: interval=0 启用批量");

    o.intervalMs = 3;
    Check(at::AutoBatchChars(o) > 1, "AutoBatch: interval<=3 小批量");

    o.intervalMs = 8;
    Check(at::AutoBatchChars(o) == 1, "AutoBatch: 默认 8ms 保持逐字兼容");

    o.intervalMs = 0;
    o.humanize.enabled = true;
    Check(at::AutoBatchChars(o) == 1, "AutoBatch: 拟人化强制逐字");

    o.humanize.enabled = false;
    o.batchChars = 32;
    Check(at::AutoBatchChars(o) == 32, "AutoBatch: 显式 batch 生效");

    o.batchChars = 9999;
    Check(at::AutoBatchChars(o) == 256, "AutoBatch: 上限 256");
}

// ---------------- 编解码测试 ----------------
static void TestDecode()
{
    {
        std::wstring out;
        at::TextEncoding enc{};
        const char utf8[] = "Hello \xE4\xB8\x96\xE7\x95\x8C";   // Hello 世界
        bool ok = at::DecodeBytes(std::string(utf8, sizeof(utf8) - 1), out, &enc);
        Check(ok && out == L"Hello 世界" && enc == at::TextEncoding::Utf8,
              "Decode: UTF-8");
    }
    {
        std::wstring out;
        std::string raw = "\xEF\xBB\xBF" "abc";
        bool ok = at::DecodeBytes(raw, out);
        Check(ok && out == L"abc", "Decode: UTF-8 BOM 剥离");
    }
    {
        std::wstring out;
        at::TextEncoding enc{};
        std::string raw;
        raw.push_back('\xFF'); raw.push_back('\xFE');
        raw.push_back('A'); raw.push_back('\0');
        raw.push_back('B'); raw.push_back('\0');
        bool ok = at::DecodeBytes(raw, out, &enc);
        Check(ok && out == L"AB" && enc == at::TextEncoding::Utf16LE,
              "Decode: UTF-16 LE BOM");
    }
    {
        // 与原实现一致：BOM 后不足一个 wchar 的尾部字节被丢弃
        std::wstring out;
        std::string raw;
        raw.push_back('\xFF'); raw.push_back('\xFE');
        raw.push_back('A'); raw.push_back('\0');
        raw.push_back('B'); raw.push_back('\0');
        raw.push_back('X');
        bool ok = at::DecodeBytes(raw, out);
        Check(ok && out == L"AB", "Decode: UTF-16 LE 奇数尾字节丢弃");
    }
    {
        std::wstring out;
        std::string raw;
        raw.push_back('\xFE'); raw.push_back('\xFF');
        raw.push_back('\x00'); raw.push_back('A');
        raw.push_back('\x4E'); raw.push_back('\x2D');   // 中 U+4E2D BE
        bool ok = at::DecodeBytes(raw, out);
        Check(ok && out == L"A\u4E2D", "Decode: UTF-16 BE BOM");
    }
    {
        std::wstring out;
        at::TextEncoding enc = at::TextEncoding::Utf8;
        bool ok = at::DecodeBytes(std::string(), out, &enc);
        Check(ok && out.empty(), "Decode: 空输入");
    }
    {
        // GBK "中文" = D6 D0 CE C4；系统 ACP 非 936 时此断言跳过
        if (GetACP() == 936) {
            std::wstring out;
            at::TextEncoding enc{};
            std::string gbk;
            gbk.push_back((char)0xD6); gbk.push_back((char)0xD0);
            gbk.push_back((char)0xCE); gbk.push_back((char)0xC4);
            bool ok = at::DecodeBytes(gbk, out, &enc);
            Check(ok && out == L"中文" && enc == at::TextEncoding::Ansi,
                  "Decode: GBK(ACP 936)");
        } else {
            std::printf("[SKIP] Decode: GBK 需要 ACP=936，当前 %u\n", GetACP());
        }
    }
}

static void TestUnescape()
{
    Check(at::Unescape(L"a\\nb") == L"a\nb", "Unescape: \\n");
    Check(at::Unescape(L"a\\tb") == L"a\tb", "Unescape: \\t");
    Check(at::Unescape(L"a\\\\b") == L"a\\b", "Unescape: \\\\");
    Check(at::Unescape(L"a\\xb") == L"a\\xb", "Unescape: 未知转义保留");
}

// ---------------- 主流程 ----------------
int wmain(int argc, wchar_t** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--verbose") == 0) g_verbose = true;
    }

    std::printf("=== AutoTyper typing tests ===\n");

    TestCountCodePoints();
    TestBuildUnits();
    TestAutoBatch();
    TestDecode();
    TestUnescape();

    if (!CreateHost()) {
        std::printf("[ERROR] 无法创建测试宿主窗口，跳过注入用例。\n");
        std::printf("RESULT: pass=%d fail=%d\n", g_pass, g_fail);
        return 2;
    }

    // 确认前台确实是我们的宿主，否则注入会打到别处，测试无意义
    PumpFor(150);
    HWND fg = GetForegroundWindow();
    if (fg != g_host) {
        std::printf("[WARN] 前台窗口不是测试宿主（%p vs %p），注入用例可能不稳定。\n",
                    (void*)fg, (void*)g_host);
        SetForegroundWindow(g_host);
        PumpFor(150);
    }

    std::vector<Case> cases = {
        { "Inject: ASCII",            L"Hello World 123", false },
        { "Inject: 中文",             L"你好，世界！这是中文测试。", false },
        { "Inject: Emoji",           L"😀🎉👍 end", false },
        { "Inject: Emoji+中文混合",   L"a中😀b", false },
        { "Inject: CRLF 换行",        L"line1\r\nline2\r\nline3", false },
        { "Inject: 单独 LF",          L"x\ny", false },
        { "Inject: Tab",             L"col1\tcol2\tcol3", false },
        { "Inject: 混合特殊字符",      L"中文\ttab\r\nemoji😀\r\nend", false },
        { "Inject: 长文本(批量)",      std::wstring(600, L'x'), false },
        { "Inject: 长中文(批量)",      std::wstring(200, L'中'), false },
        { "Inject: 拟人化模式",        L"Hi 中文😀\r\nend", true },
    };

    for (auto& c : cases) RunInjectCase(c);

    DestroyHost();

    std::printf("RESULT: pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
