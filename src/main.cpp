// ============================================================
//  AutoTyper - 单文件 Win32 自动键入工具
//  用 SendInput(KEYEVENTF_UNICODE) 逐字发送，不依赖剪贴板，
//  适用于 VMware / VirtualBox / Hyper-V / RDP / 远控等禁用粘贴的场景。
// ============================================================
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cwctype>

#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <cstdio>

#include "textfmt.h"
#include "typing.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

// ---------------- 控件 ID ----------------
#define IDC_HINT          1001
#define IDC_EDIT          1002
#define IDC_SPEED_LBL     1003
#define IDC_SPEED         1004
#define IDC_CD_LBL        1005
#define IDC_CD            1006
#define IDC_CHK_ENGLISH   1007
#define IDC_CHK_RESTORE   1008
#define IDC_BTN_LOAD      1009
#define IDC_BTN_CLEAR     1010
#define IDC_BTN_START     1011
#define IDC_BTN_STOP      1012
#define IDC_STATUS        1013
#define IDC_BTN_SETTINGS  1014
#define IDC_REPO          1016
#define IDC_CHK_HUMANIZE  1017

// 设置窗口内控件
#define IDC_LBL_START     2001
#define IDC_LBL_STOP      2002
#define IDC_BOX_START     2003
#define IDC_BOX_STOP      2004
#define IDC_BTN_DEFAULT   2005
#define IDC_SET_TIP       2006

#define TIMER_COUNTDOWN   1
#define TIMER_SELFTEST    2
#define TIMER_SELFTEST2   3

#define WM_APP_DONE       (WM_APP + 1)

static const wchar_t* kMainClass  = L"AutoTyperMainWnd";
static const wchar_t* kCountClass = L"AutoTyperCountWnd";
static const wchar_t* kSetClass   = L"AutoTyperSettingsWnd";

// ---------------- 全局状态 ----------------
static HINSTANCE g_hInst = nullptr;

static HWND g_hMain = nullptr;
static HWND g_hHint = nullptr;
static HWND g_hEdit = nullptr;
static HWND g_hSpeedLbl = nullptr;
static HWND g_hSpeed = nullptr;
static HWND g_hCdLbl = nullptr;
static HWND g_hCd = nullptr;
static HWND g_hChkEnglish = nullptr;
static HWND g_hChkRestore = nullptr;
static HWND g_hBtnLoad = nullptr;
static HWND g_hBtnClear = nullptr;
static HWND g_hBtnStart = nullptr;
static HWND g_hBtnStop = nullptr;
static HWND g_hBtnSettings = nullptr;
static HWND g_hRepo = nullptr;
static bool g_verHot = false;
static const wchar_t* kRepoUrl = L"https://github.com/TonySen1/AutoTyper";
// 毛玻璃为默认效果，不再提供开关

static HWND g_hStatus = nullptr;
static HWND g_hCountdown = nullptr;

static HFONT g_font = nullptr;
static HFONT g_countFont = nullptr;
static HFONT g_countFontSmall = nullptr;

static int  g_dpi = 96;
static int  g_countdownLeft = 0;

static std::atomic<bool> g_stop{ false };
static std::atomic<bool> g_running{ false };
static std::thread g_worker;
static std::wstring g_pending;
static bool g_restoreAfter = false;

static bool g_selftest = false;
static std::wstring g_testExpected;

// ---------------- 快捷键配置 ----------------
struct Hotkey {
    UINT mods = 0;
    UINT vk = 0;
};

static Hotkey g_hkStart{ 0, VK_F9 };    // 默认单键 F9
static Hotkey g_hkStop { 0, VK_F10 };   // 默认单键 F10
static bool   g_hkStartOK = false;
static bool   g_hkStopOK  = false;

// 设置窗口
static HWND g_hSettings = nullptr;
static HWND g_hBoxStart = nullptr;
static HWND g_hBoxStop = nullptr;
static Hotkey g_capStart{};
static Hotkey g_capStop{};
static int    g_captureTarget = 0;   // 0=无 1=开始 2=停止

static int S(int v) { return MulDiv(v, g_dpi, 96); }

// 前置：圆角绘制（定义在自绘按钮段）
static void StrokeRoundRect(HDC dc, const RECT& rc, int radius, COLORREF color, int width = 1);
static void ApplyDarkScrollbar(HWND h);
static void FillRoundRect(HDC dc, const RECT& rc, int radius, COLORREF color);


// 前置声明（定义在后面，供上方函数调用）
static std::wstring HotkeyToString(const Hotkey& hk);
static bool ParseHotkey(const std::wstring& text, Hotkey& out);


// ============================================================
//  文本编解码：实现在 textfmt.cpp，行为与 v1.0.0 一致
// ============================================================
using at::ReadTextFile;

// ============================================================
//  键鼠注入：实现在 typing.cpp
// ============================================================
static at::TypingOptions g_typeOpts;

static at::TypingReport TypingWorker(const std::wstring& text,
                                     const at::TypingOptions& opts)
{
    return at::TypeText(text, opts, &g_stop);
}

// ============================================================
//  UI 辅助
// ============================================================
static void SetStatus(const wchar_t* msg)
{
    if (g_hStatus) SetWindowTextW(g_hStatus, msg);
}

static std::wstring ReadEdit(HWND h)
{
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return L"";
    std::wstring buf((size_t)len + 1, L'\0');
    int got = GetWindowTextW(h, &buf[0], len + 1);
    if (got <= 0) return L"";
    buf.resize((size_t)got);
    return buf;
}

static int ReadInt(HWND h, int defVal, int lo, int hi)
{
    std::wstring s = ReadEdit(h);
    if (s.empty()) return defVal;
    wchar_t* end = nullptr;
    long v = wcstol(s.c_str(), &end, 10);
    if (end == s.c_str()) return defVal;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (int)v;
}

static int GetSpeedMs()      { return ReadInt(g_hSpeed, 8, 0, 1000); }
static int GetCountdownSec() { return ReadInt(g_hCd, 3, 0, 30); }

static bool IsChecked(HWND h)
{
    if (!h) return false;
    return (GetPropW(h, L"AutoTyperChecked") != nullptr);
}

static void SetChecked(HWND h, bool on)
{
    if (!h) return;
    SetPropW(h, L"AutoTyperChecked", on ? (HANDLE)1 : nullptr);
    InvalidateRect(h, nullptr, TRUE);
}

static void ToggleChecked(HWND h)
{
    if (!h) return;
    SetChecked(h, !IsChecked(h));
}

static void ApplyFont(HWND h)
{
    if (h && g_font) SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
}

static void UpdateHotkeyLabels()
{
    if (g_hBtnStart) {
        std::wstring s = L"开始输入";
        if (g_hkStart.vk != 0) s += L"  (" + HotkeyToString(g_hkStart) + L")";
        SetWindowTextW(g_hBtnStart, s.c_str());
    }
    if (g_hBtnStop) {
        std::wstring s = L"停止";
        if (g_hkStop.vk != 0) s += L"  (" + HotkeyToString(g_hkStop) + L")";
        SetWindowTextW(g_hBtnStop, s.c_str());
    }
}

// ============================================================
//  主题（浅色 / 深色）
// ============================================================
struct Palette {
    COLORREF bg;          // 窗口背景
    COLORREF panel;       // 分组面板
    COLORREF text;        // 主文本
    COLORREF textDim;     // 次要文本
    COLORREF editBg;      // 输入框背景
    COLORREF editText;    // 输入框文字
    COLORREF btnBg;       // 按钮背景
    COLORREF btnText;     // 按钮文字
    COLORREF btnHot;      // 按钮悬停
    COLORREF btnDown;     // 按钮按下
    COLORREF accent;      // 强调色（开始按钮）
    COLORREF accentHot;
    COLORREF accentText;
    COLORREF danger;      // 停止按钮
    COLORREF dangerHot;   // 停止按钮悬停
    COLORREF border;      // 边框
    COLORREF countBg;     // 倒计时浮窗背景
    COLORREF countText;
};

static const Palette kDark = {
    RGB(43,  45,  49),    // bg          统一中性深灰
    RGB(52,  55,  60),    // panel
    RGB(232, 234, 237),   // text
    RGB(150, 154, 160),   // textDim
    RGB(36,  38,  42),    // editBg
    RGB(226, 229, 233),   // editText
    RGB(62,  66,  72),    // btnBg
    RGB(232, 234, 237),   // btnText
    RGB(76,  80,  87),    // btnHot
    RGB(90,  95, 102),    // btnDown
    RGB(48,  164,  84),   // accent
    RGB(62,  182,  99),   // accentHot
    RGB(255, 255, 255),   // accentText
    RGB(196,  62,  58),   // danger
    RGB(220,  78,  74),   // dangerHot
    RGB(70,  74,  81),    // border
    RGB(30,  32,  36),    // countBg
    RGB(240, 242, 245),   // countText
};

static const Palette kLight = {
    RGB(246, 247, 249),   // bg          浅灰白，避免纯白刺眼
    RGB(238, 240, 244),   // panel
    RGB( 32,  35,  40),   // text
    RGB(110, 116, 125),   // textDim
    RGB(255, 255, 255),   // editBg
    RGB( 28,  31,  36),   // editText
    RGB(252, 252, 253),   // btnBg
    RGB( 40,  44,  50),   // btnText
    RGB(240, 242, 246),   // btnHot
    RGB(228, 231, 237),   // btnDown
    RGB( 34, 145,  74),   // accent
    RGB( 48, 164,  92),   // accentHot
    RGB(255, 255, 255),   // accentText
    RGB(198,  62,  58),   // danger
    RGB(216,  80,  76),   // dangerHot
    RGB(206, 210, 218),   // border
    RGB(252, 252, 253),   // countBg
    RGB( 32,  35,  40),   // countText
};

// 当前使用浅色主题；保留 kDark 以便后续加回切换
static const bool g_dark = false;
static Palette    g_pal = kLight;

// 自绘资源
static HBRUSH g_brBg = nullptr;
static HBRUSH g_brPanel = nullptr;
static HBRUSH g_brEdit = nullptr;
static HBRUSH g_brBtn = nullptr;
static HBRUSH g_brBtnHot = nullptr;
static HBRUSH g_brBtnDown = nullptr;
static HBRUSH g_brAccent = nullptr;
static HBRUSH g_brAccentHot = nullptr;
static HBRUSH g_brBorder = nullptr;

static void RebuildBrushes()
{
    if (g_brBg)        DeleteObject(g_brBg);
    if (g_brPanel)     DeleteObject(g_brPanel);
    if (g_brEdit)      DeleteObject(g_brEdit);
    if (g_brBtn)       DeleteObject(g_brBtn);
    if (g_brBtnHot)    DeleteObject(g_brBtnHot);
    if (g_brBtnDown)   DeleteObject(g_brBtnDown);
    if (g_brAccent)    DeleteObject(g_brAccent);
    if (g_brAccentHot) DeleteObject(g_brAccentHot);
    if (g_brBorder)    DeleteObject(g_brBorder);

    g_brBg        = CreateSolidBrush(g_pal.bg);
    g_brPanel     = CreateSolidBrush(g_pal.panel);
    g_brEdit      = CreateSolidBrush(g_pal.editBg);
    g_brBtn       = CreateSolidBrush(g_pal.btnBg);
    g_brBtnHot    = CreateSolidBrush(g_pal.btnHot);
    g_brBtnDown   = CreateSolidBrush(g_pal.btnDown);
    g_brAccent    = CreateSolidBrush(g_pal.accent);
    g_brAccentHot = CreateSolidBrush(g_pal.accentHot);
    g_brBorder    = CreateSolidBrush(g_pal.border);
}

// ============================================================
//  亚克力（毛玻璃）背景
//  Win10 1803+ 通过未公开的 SetWindowCompositionAttribute 开启，
//  更早的系统自动降级为纯色背景。
// ============================================================
enum AccentState {
    ACCENT_DISABLED            = 0,
    ACCENT_ENABLE_GRADIENT     = 1,
    ACCENT_ENABLE_TRANSPARENT  = 2,
    ACCENT_ENABLE_BLURBEHIND   = 3,   // Win10 1803+ 基础模糊
    ACCENT_ENABLE_ACRYLICBLUR  = 4,   // Win10 1803+ 亚克力
    ACCENT_ENABLE_HOSTBACKDROP = 5,   // Win11 云母
};

struct AccentPolicy {
    int   state;
    int   flags;
    DWORD gradientColor;   // ABGR
    int   animationId;
};

struct WinCompositionAttrData {
    int   attribute;
    PVOID data;
    SIZE_T size;
};

static bool g_acrylicSupported = false;
static bool g_acrylicEnabled = false;
// 因主题选择而主动关闭（区别于"系统不支持"）
static bool g_acrylicThemeOff = false;

typedef BOOL (WINAPI *PFN_SetWindowCompositionAttribute)(HWND, WinCompositionAttrData*);

static PFN_SetWindowCompositionAttribute GetSetWCA()
{
    static PFN_SetWindowCompositionAttribute fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32)
            fn = (PFN_SetWindowCompositionAttribute)
                 GetProcAddress(user32, "SetWindowCompositionAttribute");
    }
    return fn;
}

// 把 COLORREF(RGB) 转成亚克力需要的 ABGR，并施加透明度
static DWORD AcrylicColor(COLORREF c, BYTE alpha)
{
    return ((DWORD)alpha << 24) |
           ((DWORD)GetBValue(c) << 16) |
           ((DWORD)GetGValue(c) << 8)  |
           ((DWORD)GetRValue(c));
}

static void EnableAcrylic(HWND hWnd, bool on)
{
    if (!hWnd) return;

    g_acrylicThemeOff = false;

    // 白色主题不启用 DWM 客户区合成。
    // 实测：浅色 gradientColor 下 ACCENT_ENABLE_ACRYLICBLUR 会把整块客户区
    // 合成成纯白，子控件虽然 Visible 却被盖住。纯色背景可预测得多。
    if (!g_dark) {
        g_acrylicThemeOff = true;
        g_acrylicSupported = false;
        g_acrylicEnabled = false;
        return;
    }

    PFN_SetWindowCompositionAttribute wca = GetSetWCA();
    if (!wca) { g_acrylicSupported = false; return; }
    g_acrylicSupported = true;

    AccentPolicy ap{};
    if (on) {
        ap.state = ACCENT_ENABLE_ACRYLICBLUR;
        ap.flags = 2;   // 允许绘制到客户区
        // 深色用较暗的底色，alpha 越低越透
        BYTE alpha = 0xC0;
        ap.gradientColor = AcrylicColor(g_pal.bg, alpha);
    } else {
        ap.state = ACCENT_DISABLED;
    }

    WinCompositionAttrData data{};
    data.attribute = 19;   // WCA_ACCENT_POLICY
    data.data = &ap;
    data.size = sizeof(ap);
    wca(hWnd, &data);

    g_acrylicEnabled = on;
}

// 用 AlphaBlend 在客户区铺一层半透明底色（GDI 的 FillRect 忽略 alpha）
static void FillTranslucent(HDC dc, const RECT& rc, COLORREF color, BYTE alpha)
{
    HDC mem = CreateCompatibleDC(dc);
    if (!mem) return;

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) { DeleteDC(mem); return; }

    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    RECT local = { 0, 0, w, h };
    HBRUSH br = CreateSolidBrush(color);
    FillRect(mem, &local, br);
    DeleteObject(br);

    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = alpha;
    bf.AlphaFormat = 0;

    AlphaBlend(dc, rc.left, rc.top, w, h, mem, 0, 0, w, h, bf);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// 窗口背景：开了亚克力就铺半透明，否则纯色
// GDI 没有逐像素 alpha，客户区做"真透明"会把旧像素混合进来 -> 残影。
// 这里改用不透明底色 + 顶部微亮渐变模拟玻璃；标题栏模糊仍由 DWM 负责。
static void PaintWindowBackground(HWND hWnd, HDC dc)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;

    // ① 不透明铺满，这是消除残影的关键
    FillRect(dc, &rc, g_brBg ? g_brBg : (HBRUSH)(COLOR_BTNFACE + 1));

    // ② 顶部 1/3 叠一层极淡的渐变
    //    深色主题提亮做受光感；浅色主题不依赖亚克力也要有层次，故同样绘制
    if (!g_acrylicEnabled && g_dark) return;
    RECT top = rc;
    top.bottom = rc.top + (rc.bottom - rc.top) / 3;
    if (top.bottom > top.top + 1) {
        const int delta = g_dark ? 12 : -10;
        auto shift = [delta](BYTE v) -> COLOR16 {
            int n = (int)v + delta;
            if (n < 0) n = 0;
            if (n > 255) n = 255;
            return (COLOR16)((BYTE)n << 8);
        };

        TRIVERTEX v[2];
        v[0].x = top.left;  v[0].y = top.top;
        v[0].Red   = shift(GetRValue(g_pal.bg));
        v[0].Green = shift(GetGValue(g_pal.bg));
        v[0].Blue  = shift(GetBValue(g_pal.bg));
        v[0].Alpha = 0;

        v[1].x = top.right; v[1].y = top.bottom;
        v[1].Red   = (COLOR16)GetRValue(g_pal.bg) << 8;
        v[1].Green = (COLOR16)GetGValue(g_pal.bg) << 8;
        v[1].Blue  = (COLOR16)GetBValue(g_pal.bg) << 8;
        v[1].Alpha = 0;

        GRADIENT_RECT gr = { 0, 1 };
        GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
    }
}

static void ApplyThemePalette()
{
    g_pal = g_dark ? kDark : kLight;
    RebuildBrushes();
}

// 标题栏深色（Win10 1809+ / Win11），失败时静默忽略
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static void ApplyDarkTitleBar(HWND hWnd, bool dark)
{
    if (!hWnd) return;
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;

    typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    PFN_DwmSetWindowAttribute fn =
        (PFN_DwmSetWindowAttribute)GetProcAddress(dwm, "DwmSetWindowAttribute");

    if (fn) {
        BOOL v = dark ? TRUE : FALSE;
        // 20 = Win10 1809+；19 = 更早的预览版。两个都试。
        if (FAILED(fn(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v))))
            fn(hWnd, 19, &v, sizeof(v));
    }
    FreeLibrary(dwm);
}

static HICON g_icoApp   = nullptr;   // 应用图标（固定 1 号资源）
static HICON g_icoAppSm = nullptr;   // 应用小图标

static void LoadAppIcons()
{
    g_icoApp = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                 0, 0, LR_DEFAULTSIZE);
    g_icoAppSm = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
}

// 应用图标与主题无关，固定使用 1 号资源
static void ApplyWindowIcons(HWND hWnd, bool)
{
    if (g_icoApp)   SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)g_icoApp);
    if (g_icoAppSm) SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icoAppSm);
}

// ============================================================
//  快捷键：格式化 / 判定
// ============================================================
static const wchar_t* VkName(UINT vk, wchar_t* buf, size_t cch)
{
    if (vk == 0) { wcscpy_s(buf, cch, L"无"); return buf; }

    if (vk >= VK_F1 && vk <= VK_F24) {
        swprintf_s(buf, cch, L"F%u", vk - VK_F1 + 1);
        return buf;
    }
    if (vk >= 'A' && vk <= 'Z') { swprintf_s(buf, cch, L"%c", (wchar_t)vk); return buf; }
    if (vk >= '0' && vk <= '9') { swprintf_s(buf, cch, L"%c", (wchar_t)vk); return buf; }

    switch (vk) {
    case VK_SPACE:      wcscpy_s(buf, cch, L"Space"); break;
    case VK_RETURN:     wcscpy_s(buf, cch, L"Enter"); break;
    case VK_TAB:        wcscpy_s(buf, cch, L"Tab"); break;
    case VK_ESCAPE:     wcscpy_s(buf, cch, L"Esc"); break;
    case VK_BACK:       wcscpy_s(buf, cch, L"Backspace"); break;
    case VK_INSERT:     wcscpy_s(buf, cch, L"Insert"); break;
    case VK_DELETE:     wcscpy_s(buf, cch, L"Delete"); break;
    case VK_HOME:       wcscpy_s(buf, cch, L"Home"); break;
    case VK_END:        wcscpy_s(buf, cch, L"End"); break;
    case VK_PRIOR:      wcscpy_s(buf, cch, L"PgUp"); break;
    case VK_NEXT:       wcscpy_s(buf, cch, L"PgDn"); break;
    case VK_LEFT:       wcscpy_s(buf, cch, L"Left"); break;
    case VK_RIGHT:      wcscpy_s(buf, cch, L"Right"); break;
    case VK_UP:         wcscpy_s(buf, cch, L"Up"); break;
    case VK_DOWN:       wcscpy_s(buf, cch, L"Down"); break;
    case VK_PAUSE:      wcscpy_s(buf, cch, L"Pause"); break;
    case VK_SNAPSHOT:   wcscpy_s(buf, cch, L"PrtSc"); break;
    case VK_SCROLL:     wcscpy_s(buf, cch, L"ScrollLock"); break;
    case VK_CAPITAL:    wcscpy_s(buf, cch, L"CapsLock"); break;
    case VK_NUMLOCK:    wcscpy_s(buf, cch, L"NumLock"); break;
    case VK_OEM_1:      wcscpy_s(buf, cch, L";"); break;
    case VK_OEM_PLUS:   wcscpy_s(buf, cch, L"="); break;
    case VK_OEM_COMMA:  wcscpy_s(buf, cch, L","); break;
    case VK_OEM_MINUS:  wcscpy_s(buf, cch, L"-"); break;
    case VK_OEM_PERIOD: wcscpy_s(buf, cch, L"."); break;
    case VK_OEM_2:      wcscpy_s(buf, cch, L"/"); break;
    case VK_OEM_3:      wcscpy_s(buf, cch, L"`"); break;
    case VK_OEM_4:      wcscpy_s(buf, cch, L"["); break;
    case VK_OEM_5:      wcscpy_s(buf, cch, L"\\"); break;
    case VK_OEM_6:      wcscpy_s(buf, cch, L"]"); break;
    case VK_OEM_7:      wcscpy_s(buf, cch, L"'"); break;
    default:            swprintf_s(buf, cch, L"VK%02X", vk); break;
    }
    return buf;
}

static std::wstring HotkeyToString(const Hotkey& hk)
{
    if (hk.vk == 0) return L"无";

    std::wstring s;
    if (hk.mods & MOD_CONTROL) s += L"Ctrl+";
    if (hk.mods & MOD_SHIFT)   s += L"Shift+";
    if (hk.mods & MOD_ALT)     s += L"Alt+";
    if (hk.mods & MOD_WIN)     s += L"Win+";

    wchar_t name[32];
    s += VkName(hk.vk, name, 32);
    return s;
}

static bool IsModifierVk(UINT vk)
{
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_SHIFT   || vk == VK_LSHIFT   || vk == VK_RSHIFT   ||
           vk == VK_MENU    || vk == VK_LMENU    || vk == VK_RMENU    ||
           vk == VK_LWIN    || vk == VK_RWIN;
}

// 无修饰键时只放行功能键/媒体键；字母数字等会造成全局抢占，禁止单独使用
static bool IsStandaloneAllowed(UINT vk)
{
    if (vk >= VK_F1 && vk <= VK_F24) return true;

    switch (vk) {
    case VK_PAUSE:
    case VK_SNAPSHOT:
    case VK_SCROLL:
    case VK_BROWSER_BACK:
    case VK_BROWSER_FORWARD:
    case VK_BROWSER_REFRESH:
    case VK_BROWSER_STOP:
    case VK_BROWSER_SEARCH:
    case VK_BROWSER_FAVORITES:
    case VK_BROWSER_HOME:
    case VK_MEDIA_NEXT_TRACK:
    case VK_MEDIA_PREV_TRACK:
    case VK_MEDIA_STOP:
    case VK_MEDIA_PLAY_PAUSE:
    case VK_VOLUME_MUTE:
    case VK_VOLUME_DOWN:
    case VK_VOLUME_UP:
        return true;
    default:
        return false;
    }
}

// ============================================================
//  配置文件（ini，exe 同目录优先，不可写时退回 %APPDATA%）
// ============================================================
static std::wstring ConfigPath()
{
    wchar_t base[MAX_PATH] = L"";
    DWORD n = GetModuleFileNameW(nullptr, base, MAX_PATH);
    std::wstring p(base, n ? n : 0);

    size_t slash = p.find_last_of(L'\\');
    std::wstring dir = (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
    std::wstring portable = dir + L"\\AutoTyper.ini";

    if (GetFileAttributesW(portable.c_str()) != INVALID_FILE_ATTRIBUTES) return portable;

    // 探测可写性：在目录里建临时探针文件，绝不留下空 ini
    DWORD attrs = GetFileAttributesW(dir.c_str());
    bool dirWritable = (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_READONLY);

    if (dirWritable) {
        wchar_t tmp[MAX_PATH];
        swprintf_s(tmp, L"%s\\~autotyper_%lu.tmp", dir.c_str(), GetCurrentProcessId());
        HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return portable;
        }
    }

    wchar_t appdata[MAX_PATH] = L"";
    DWORD m = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (m > 0 && m < MAX_PATH) {
        std::wstring d = std::wstring(appdata) + L"\\AutoTyper";
        CreateDirectoryW(d.c_str(), nullptr);
        return d + L"\\AutoTyper.ini";
    }
    return portable;
}

static void LoadConfig()
{
    std::wstring path = ConfigPath();
    wchar_t buf[128];

    GetPrivateProfileStringW(L"hotkey", L"start", L"F9", buf, 128, path.c_str());
    Hotkey hk;
    if (ParseHotkey(buf, hk)) g_hkStart = hk;

    GetPrivateProfileStringW(L"hotkey", L"stop", L"F10", buf, 128, path.c_str());
    if (ParseHotkey(buf, hk)) g_hkStop = hk;

    // [typing] 段：全部可选，缺省即 v1.0.0 行为
    auto rdInt = [&](const wchar_t* key, int def, int lo, int hi) -> int {
        GetPrivateProfileStringW(L"typing", key, L"", buf, 128, path.c_str());
        if (!buf[0]) return def;
        int v = (int)wcstol(buf, nullptr, 10);
        return v < lo ? lo : (v > hi ? hi : v);
    };
    auto rdBool = [&](const wchar_t* key, bool def) -> bool {
        GetPrivateProfileStringW(L"typing", key, L"", buf, 128, path.c_str());
        if (!buf[0]) return def;
        return _wcsicmp(buf, L"1") == 0 || _wcsicmp(buf, L"true") == 0 ||
               _wcsicmp(buf, L"yes") == 0 || _wcsicmp(buf, L"on") == 0;
    };

    g_typeOpts.intervalMs        = rdInt(L"interval", 8, 0, 1000);
    g_typeOpts.batchChars        = rdInt(L"batch", 0, 0, 256);
    g_typeOpts.forceEnglish      = rdBool(L"english", false);
    g_typeOpts.restoreLayout     = rdBool(L"restore_layout", false);
    g_typeOpts.checkUipi         = rdBool(L"check_uipi", true);

    auto& hz = g_typeOpts.humanize;
    hz.enabled               = rdBool(L"humanize", false);
    hz.meanMs                = (double)rdInt(L"humanize_mean_ms", 45, 1, 2000);
    hz.sigmaMs               = (double)rdInt(L"humanize_sigma_ms", 18, 0, 1000);
    hz.minMs                 = rdInt(L"humanize_min_ms", 8, 0, 5000);
    hz.maxMs                 = rdInt(L"humanize_max_ms", 400, 1, 10000);
    hz.pauseAfterPunct       = rdBool(L"humanize_pause_punct", true);
    hz.pauseAfterNewline     = rdBool(L"humanize_pause_newline", true);
    hz.pauseAfterLongWord    = rdBool(L"humanize_pause_word", true);
    hz.fatigue               = rdBool(L"humanize_fatigue", true);
    hz.typoRate              = (double)rdInt(L"humanize_typo_percent", 0, 0, 5) / 100.0;
    if (hz.maxMs < hz.minMs) hz.maxMs = hz.minMs;
}

static void SaveConfig()
{
    std::wstring path = ConfigPath();
    std::wstring start = HotkeyToString(g_hkStart);
    std::wstring stop  = HotkeyToString(g_hkStop);
    WritePrivateProfileStringW(L"hotkey", L"start", start.c_str(), path.c_str());
    WritePrivateProfileStringW(L"hotkey", L"stop",  stop.c_str(),  path.c_str());
}

// 用户显式改过高级选项时才落盘 [typing]，避免默认安装凭空多出配置段
static void SaveTypingConfig(const at::TypingOptions& o)
{
    std::wstring path = ConfigPath();
    wchar_t buf[64];

    swprintf_s(buf, L"%d", o.batchChars);
    WritePrivateProfileStringW(L"typing", L"batch", buf, path.c_str());

    WritePrivateProfileStringW(L"typing", L"humanize",
                              o.humanize.enabled ? L"1" : L"0", path.c_str());

    swprintf_s(buf, L"%d", (int)(o.humanize.meanMs + 0.5));
    WritePrivateProfileStringW(L"typing", L"humanize_mean_ms", buf, path.c_str());

    swprintf_s(buf, L"%d", (int)(o.humanize.sigmaMs + 0.5));
    WritePrivateProfileStringW(L"typing", L"humanize_sigma_ms", buf, path.c_str());

    swprintf_s(buf, L"%d", (int)(o.humanize.typoRate * 100.0 + 0.5));
    WritePrivateProfileStringW(L"typing", L"humanize_typo_percent", buf, path.c_str());

    WritePrivateProfileStringW(L"typing", L"humanize_fatigue",
                              o.humanize.fatigue ? L"1" : L"0", path.c_str());
}

static bool RegisterHotkeys(HWND hWnd)
{
    if (g_hkStartOK) { UnregisterHotKey(hWnd, 1); g_hkStartOK = false; }
    if (g_hkStopOK)  { UnregisterHotKey(hWnd, 2); g_hkStopOK  = false; }

    if (g_hkStart.vk != 0)
        g_hkStartOK = RegisterHotKey(hWnd, 1, g_hkStart.mods, g_hkStart.vk) != 0;
    if (g_hkStop.vk != 0)
        g_hkStopOK = RegisterHotKey(hWnd, 2, g_hkStop.mods, g_hkStop.vk) != 0;

    return g_hkStartOK || g_hkStopOK;
}


// 从 ini 的 "Ctrl+Shift+F9" 形式解析
static bool ParseHotkey(const std::wstring& text, Hotkey& out)
{
    out.mods = 0;
    out.vk = 0;

    std::wstring s = text;
    if (s.empty()) return false;
    if (_wcsicmp(s.c_str(), L"none") == 0 || s == L"\u65e0") return true;   // 禁用

    size_t pos = 0;
    while (pos < s.size()) {
        size_t plus = s.find(L'+', pos);
        std::wstring tok = s.substr(pos, (plus == std::wstring::npos) ? std::wstring::npos : plus - pos);

        while (!tok.empty() && (tok.front() == L' ' || tok.front() == L'\t')) tok.erase(tok.begin());
        while (!tok.empty() && (tok.back() == L' ' || tok.back() == L'\t')) tok.pop_back();

        if (tok.empty()) { if (plus == std::wstring::npos) break; pos = plus + 1; continue; }

        std::wstring low;
        low.reserve(tok.size());
        for (wchar_t c : tok) low.push_back((wchar_t)towlower(c));

        if (low == L"ctrl" || low == L"control")   out.mods |= MOD_CONTROL;
        else if (low == L"shift")                  out.mods |= MOD_SHIFT;
        else if (low == L"alt")                    out.mods |= MOD_ALT;
        else if (low == L"win" || low == L"super") out.mods |= MOD_WIN;
        else {
            UINT vk = 0;
            if (tok.size() == 1) {
                wchar_t c = (wchar_t)towupper(tok[0]);
                if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) vk = (UINT)c;
            }
            if (!vk && (tok[0] == L'F' || tok[0] == L'f')) {
                int n = _wtoi(tok.c_str() + 1);
                if (n >= 1 && n <= 24) vk = VK_F1 + (UINT)(n - 1);
            }
            if (!vk) {
                struct { const wchar_t* name; UINT vk; } kNamed[] = {
                    { L"space", VK_SPACE }, { L"enter", VK_RETURN }, { L"return", VK_RETURN },
                    { L"tab", VK_TAB }, { L"esc", VK_ESCAPE }, { L"escape", VK_ESCAPE },
                    { L"backspace", VK_BACK }, { L"insert", VK_INSERT }, { L"ins", VK_INSERT },
                    { L"delete", VK_DELETE }, { L"del", VK_DELETE },
                    { L"home", VK_HOME }, { L"end", VK_END },
                    { L"pgup", VK_PRIOR }, { L"pageup", VK_PRIOR },
                    { L"pgdn", VK_NEXT }, { L"pagedown", VK_NEXT },
                    { L"left", VK_LEFT }, { L"right", VK_RIGHT },
                    { L"up", VK_UP }, { L"down", VK_DOWN },
                    { L"pause", VK_PAUSE }, { L"prtsc", VK_SNAPSHOT },
                    { L"printscreen", VK_SNAPSHOT },
                };
                for (auto& e : kNamed) {
                    if (low == e.name) { vk = e.vk; break; }
                }
            }
            if (!vk) return false;
            out.vk = vk;
        }

        if (plus == std::wstring::npos) break;
        pos = plus + 1;
    }

    if (out.vk == 0) return false;
    if (IsModifierVk(out.vk)) return false;
    if (out.mods == 0 && !IsStandaloneAllowed(out.vk)) return false;
    return true;
}

// ============================================================
//  编辑框边框
// ============================================================
static LRESULT CALLBACK EditBorderProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                       UINT_PTR, DWORD_PTR)
{
    if (msg == WM_ERASEBKGND) {
        // 必须真的填充，返回 1 而不绘制会留下旧像素残影
        HDC dc = (HDC)wp;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(dc, &rc, g_brEdit ? g_brEdit : (HBRUSH)(COLOR_WINDOW + 1));
        return 1;
    }

    if (msg == WM_NCPAINT)
        return 0;   // 不画直角边框

    if (msg == WM_PAINT) {
        LRESULT r = DefSubclassProc(hWnd, msg, wp, lp);
        HDC dc = GetDC(hWnd);
        if (dc) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            InflateRect(&rc, -1, -1);

            // 小输入框（间隔 / 倒计时）用直角，主编辑框保持圆角
            bool square = (hWnd == g_hSpeed || hWnd == g_hCd);
            if (square) {
                HPEN pen = CreatePen(PS_SOLID, 1, g_pal.border);
                HGDIOBJ op = SelectObject(dc, pen);
                HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
                SelectObject(dc, ob);
                SelectObject(dc, op);
                DeleteObject(pen);
            } else {
                StrokeRoundRect(dc, rc, S(6), g_pal.border);
            }
            ReleaseDC(hWnd, dc);
        }
        return r;
    }


    return DefSubclassProc(hWnd, msg, wp, lp);
}

static void SubclassEdit(HWND h)
{
    if (!h) return;
    SetWindowSubclass(h, EditBorderProc, 200, 0);
    ApplyDarkScrollbar(h);
}


// ============================================================
//  右下角版本标签（点击跳转仓库）
// ============================================================
static LRESULT CALLBACK VersionLabelProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR, DWORD_PTR)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(dc, &rc, g_brBg ? g_brBg : (HBRUSH)(COLOR_BTNFACE + 1));
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HBRUSH bg = CreateSolidBrush(g_pal.bg);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        wchar_t text[64] = L"";
        GetWindowTextW(hWnd, text, 64);

        SetBkMode(dc, TRANSPARENT);
        // 悬停时提亮，暗示可点击
        SetTextColor(dc, g_verHot ? g_pal.text : g_pal.textDim);

        HFONT font = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
        HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;

        DrawTextW(dc, text, -1, &rc,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        if (old) SelectObject(dc, old);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hWnd;
        TrackMouseEvent(&tme);
        if (!g_verHot) { g_verHot = true; InvalidateRect(hWnd, nullptr, TRUE); }
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return 0;
    }

    case WM_MOUSELEAVE:
        g_verHot = false;
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        break;

    default:
        break;
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

static void SubclassVersionLabel(HWND h)
{
    if (h) SetWindowSubclass(h, VersionLabelProc, 300, 0);
}

// ============================================================
//  快捷键捕获框
// ============================================================
static LRESULT CALLBACK CaptureBoxProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                       UINT_PTR, DWORD_PTR refData)
{
    int which = (int)refData;
    Hotkey* dst = (which == 1) ? &g_capStart : &g_capStop;

    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        UINT vk = (UINT)wp;
        if (IsModifierVk(vk)) return 0;

        UINT mods = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
        if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= MOD_SHIFT;
        if (GetKeyState(VK_MENU)    & 0x8000) mods |= MOD_ALT;
        if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;

        if (vk == VK_ESCAPE) {
            SetWindowTextW(hWnd, L"\u6309\u4e0b\u65b0\u7684\u7ec4\u5408\u952e\u2026");
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        if (vk == VK_BACK || vk == VK_DELETE) {
            dst->mods = 0;
            dst->vk = 0;
            SetWindowTextW(hWnd, L"\u65e0\uff08\u5df2\u7981\u7528\uff09");
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }

        if (mods == 0 && !IsStandaloneAllowed(vk)) {
            SetWindowTextW(hWnd, L"\u5355\u952e\u4ec5\u652f\u6301 F1\u2013F24 \u7b49\u529f\u80fd\u952e");
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }

        dst->mods = mods;
        dst->vk = vk;
        g_captureTarget = 0;   // 已捕获，退出等待态

        std::wstring combo = HotkeyToString(*dst);
        SetWindowTextW(hWnd, combo.c_str());
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    }

    case WM_CHAR:
    case WM_SYSCHAR:
        return 0;

    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;

    case WM_LBUTTONDOWN:
        SetFocus(hWnd);
        g_captureTarget = which;
        SetWindowTextW(hWnd, L"\u6309\u4e0b\u65b0\u7684\u7ec4\u5408\u952e\u2026");
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_SETFOCUS:
        g_captureTarget = which;
        SetPropW(hWnd, L"AutoTyperFocused", (HANDLE)1);
        SetWindowTextW(hWnd, L"\u6309\u4e0b\u65b0\u7684\u7ec4\u5408\u952e\u2026");
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;

    case WM_KILLFOCUS: {
        RemovePropW(hWnd, L"AutoTyperFocused");
        // 只有「本来正在捕获我」时才退出捕获态；
        // 若焦点转到另一个框，那边的 WM_SETFOCUS 已把目标切过去了，
        // 这里再清会把新目标擦掉。
        if (g_captureTarget == which) g_captureTarget = 0;

        // 失焦显示「正在编辑」的值（*dst），而不是已生效的 g_hkStart/g_hkStop；
        // 后者要点确定才更新，显示它会让用户以为改动丢了。
        SetWindowTextW(hWnd, HotkeyToString(*dst).c_str());
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    }

    default:
        break;
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}


// ============================================================
//  圆角绘制
// ============================================================
static void RoundRectPath(HDC dc, const RECT& rc, int radius)
{
    int r = radius * 2;
    if (r > rc.right - rc.left) r = rc.right - rc.left;
    if (r > rc.bottom - rc.top)  r = rc.bottom - rc.top;

    // 用 RoundRect 配合画笔/画刷即可；GDI 的 RoundRect 圆角半径是椭圆宽高
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
}

static void FillRoundRect(HDC dc, const RECT& rc, int radius, COLORREF color)
{
    HBRUSH br = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRectPath(dc, rc, radius);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
    DeleteObject(br);
}

static void StrokeRoundRect(HDC dc, const RECT& rc, int radius, COLORREF color, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRectPath(dc, rc, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}


// 单行 EDIT 的文本默认贴着上边，这里用 EM_SETRECT 把文本区压到垂直居中
static void CenterEditTextVertically(HWND h)
{
    if (!h) return;

    RECT rc;
    GetClientRect(h, &rc);

    HDC dc = GetDC(h);
    HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    HGDIOBJ old = f ? SelectObject(dc, f) : nullptr;

    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);

    if (old) SelectObject(dc, old);
    ReleaseDC(h, dc);

    // 关键：先清零左右边距，ES_CENTER 的基准才等于控件中心
    SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);

    int textH = tm.tmHeight;
    int boxH  = rc.bottom - rc.top;
    int padY  = (boxH - textH) / 2;
    if (padY < 1) padY = 1;

    RECT tr = { 0, padY, rc.right, padY + textH };
    SendMessageW(h, EM_SETRECT, 0, (LPARAM)&tr);
    SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
}


// ============================================================
//  滚动条深色化
//  EDIT 的滚动条由系统主题绘制，SetWindowTheme 是唯一稳定手段。
// ============================================================
static void ApplyDarkScrollbar(HWND h)
{
    if (!h) return;

    static HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
    if (!uxtheme) return;

    typedef HRESULT (WINAPI *PFN_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
    static PFN_SetWindowTheme fn =
        (PFN_SetWindowTheme)GetProcAddress(uxtheme, "SetWindowTheme");
    if (!fn) return;

    // 深色用 DarkMode_Explorer（Win10 1809+）；浅色恢复系统默认 Explorer 主题
    if (g_dark) fn(h, L"DarkMode_Explorer", nullptr);
    else        fn(h, L"Explorer", nullptr);
}

// ============================================================
//  自绘按钮 / 复选框
//  每个按钮用 refData 标记角色，决定配色：
//    0 = 普通  1 = 主按钮(开始)  2 = 危险(停止)
// ============================================================
enum BtnRole { ROLE_NORMAL = 0, ROLE_PRIMARY = 1, ROLE_DANGER = 2, ROLE_CHECKBOX = 3 };

static LRESULT CALLBACK ButtonSubProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                      UINT_PTR, DWORD_PTR refData);

static void SubclassButton(HWND h, BtnRole role)
{
    if (!h) return;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)role);
    SetWindowSubclass(h, ButtonSubProc, 100 + (int)role, (DWORD_PTR)role);
}

static inline bool ptr_to_bool(HANDLE v) { return v != nullptr; }

static void DrawOwnedButton(LPDRAWITEMSTRUCT dis, BtnRole role)
{
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    bool focused  = (dis->itemState & ODS_FOCUS) != 0;
    // ODS_HOTLIGHT 在没有键盘焦点跟踪时会一直为真，改用自己维护的悬停标记
    bool hot      = ptr_to_bool(GetPropW(dis->hwndItem, L"AutoTyperHot"));

    COLORREF bg, fg;
    if (role == ROLE_PRIMARY) {
        bg = pressed ? g_pal.accent : (hot ? g_pal.accentHot : g_pal.accent);
        fg = g_pal.accentText;
    } else if (role == ROLE_DANGER) {
        bg = (pressed || hot) ? g_pal.dangerHot : g_pal.danger;
        fg = g_pal.accentText;
    } else {
        bg = pressed ? g_pal.btnDown : (hot ? g_pal.btnHot : g_pal.btnBg);
        fg = g_pal.btnText;
    }
    if (disabled) {
        bg = g_pal.btnBg;
        fg = g_pal.textDim;
    }

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(dc, &rc, br);
    DeleteObject(br);

    // 边框
    HPEN pen = CreatePen(PS_SOLID, 1, role == ROLE_PRIMARY ? g_pal.accent : g_pal.border);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBr  = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    // 文字
    wchar_t text[256] = L"";
    GetWindowTextW(dis->hwndItem, text, 256);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);

    HFONT font = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;

    RECT tr = rc;
    if (pressed) OffsetRect(&tr, 0, 1);
    DrawTextW(dc, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 焦点框
    if (focused) {
        RECT fr = rc;
        InflateRect(&fr, -S(3), -S(3));
        DrawFocusRect(dc, &fr);
    }

    if (oldFont) SelectObject(dc, oldFont);
}

static void DrawOwnedCheckbox(LPDRAWITEMSTRUCT dis)
{
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    bool checked = IsChecked(dis->hwndItem);
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    // 背景交给父窗口，避免出现色块


    int box = S(15);
    int top = rc.top + (rc.bottom - rc.top - box) / 2;
    RECT boxRc = { rc.left, top, rc.left + box, top + box };

    // 方框
    HBRUSH boxBg = CreateSolidBrush(checked ? g_pal.accent : g_pal.editBg);
    FillRect(dc, &boxRc, boxBg);
    DeleteObject(boxBg);

    HPEN pen = CreatePen(PS_SOLID, 1, checked ? g_pal.accent : g_pal.textDim);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, boxRc.left, boxRc.top, boxRc.right, boxRc.bottom);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    // 勾
    if (checked) {
        HPEN check = CreatePen(PS_SOLID, (S(2) > 2 ? S(2) : 2), g_pal.accentText);
        HGDIOBJ op = SelectObject(dc, check);
        MoveToEx(dc, boxRc.left + box / 4,     boxRc.top + box / 2, nullptr);
        LineTo  (dc, boxRc.left + box * 2 / 5, boxRc.top + box * 3 / 4);
        LineTo  (dc, boxRc.left + box * 4 / 5, boxRc.top + box / 4);
        SelectObject(dc, op);
        DeleteObject(check);
    }

    // 标签
    wchar_t text[256] = L"";
    GetWindowTextW(dis->hwndItem, text, 256);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? g_pal.textDim : g_pal.text);

    HFONT font = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;

    RECT tr = rc;
    tr.left = boxRc.right + S(6);
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (oldFont) SelectObject(dc, oldFont);
}

static LRESULT CALLBACK ButtonSubProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                      UINT_PTR, DWORD_PTR refData)
{
    BtnRole role = (BtnRole)refData;

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;   // 全部由 WM_DRAWITEM 处理

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hWnd;
        TrackMouseEvent(&tme);
        if (!GetPropW(hWnd, L"AutoTyperHot")) {
            SetPropW(hWnd, L"AutoTyperHot", (HANDLE)1);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }

    case WM_MOUSELEAVE:
        RemovePropW(hWnd, L"AutoTyperHot");
        InvalidateRect(hWnd, nullptr, FALSE);
        break;

    default:
        break;
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// ============================================================
//  设置窗口
// ============================================================
static void ApplySettings(HWND hDlg)
{
    // 冲突检测
    bool startEnabled = g_capStart.vk != 0;
    bool stopEnabled  = g_capStop.vk  != 0;

    if (startEnabled && stopEnabled &&
        g_capStart.mods == g_capStop.mods && g_capStart.vk == g_capStop.vk) {
        MessageBoxW(hDlg, L"“开始”和“停止”不能是同一个组合键。", L"快捷键冲突", MB_ICONWARNING | MB_OK);
        return;
    }
    if (!startEnabled && !stopEnabled) {
        if (MessageBoxW(hDlg, L"两个热键都被禁用了，之后只能用按钮操作。确定继续？",
                        L"提示", MB_ICONQUESTION | MB_YESNO) != IDYES)
            return;
    }

    // 先试注册，失败则回滚
    Hotkey oldStart = g_hkStart, oldStop = g_hkStop;
    g_hkStart = g_capStart;
    g_hkStop  = g_capStop;

    if (g_hkStartOK) { UnregisterHotKey(g_hMain, 1); g_hkStartOK = false; }
    if (g_hkStopOK)  { UnregisterHotKey(g_hMain, 2); g_hkStopOK  = false; }

    LRESULT r1 = (g_hkStart.vk != 0)
        ? RegisterHotKey(g_hMain, 1, g_hkStart.mods, g_hkStart.vk) : 0;
    LRESULT r2 = (g_hkStop.vk != 0)
        ? RegisterHotKey(g_hMain, 2, g_hkStop.mods, g_hkStop.vk) : 0;

    bool okStart = (g_hkStart.vk == 0) || r1;
    bool okStop  = (g_hkStop.vk  == 0) || r2;

    if (!okStart || !okStop) {
        // 回滚
        if (okStart && g_hkStart.vk != 0) UnregisterHotKey(g_hMain, 1);
        if (okStop  && g_hkStop.vk  != 0) UnregisterHotKey(g_hMain, 2);
        g_hkStart = oldStart;
        g_hkStop  = oldStop;
        g_hkStartOK = false;
        g_hkStopOK  = false;
        RegisterHotkeys(g_hMain);

        std::wstring bad;
        if (!okStart) bad += L"“开始”：" + HotkeyToString(g_capStart) + L"\r\n";
        if (!okStop)  bad += L"“停止”：" + HotkeyToString(g_capStop) + L"\r\n";
        std::wstring msg = L"以下组合键被系统或其他程序占用，注册失败：\r\n\r\n" + bad +
                           L"\r\n请换一组再试。";
        MessageBoxW(hDlg, msg.c_str(), L"注册失败", MB_ICONERROR | MB_OK);
        return;
    }

    g_hkStartOK = okStart;
    g_hkStopOK  = okStop;

    // 拟人化开关（高级选项，默认关闭）
    HWND chk = GetDlgItem(hDlg, IDC_CHK_HUMANIZE);
    bool wantHumanize = chk && GetPropW(chk, L"AutoTyperChecked") != nullptr;
    if (wantHumanize != g_typeOpts.humanize.enabled) {
        g_typeOpts.humanize.enabled = wantHumanize;
        SaveTypingConfig(g_typeOpts);
    }

    SaveConfig();
    UpdateHotkeyLabels();
    SetStatus(L"设置已更新并保存。");
    DestroyWindow(hDlg);
    g_hSettings = nullptr;
}

static LRESULT CALLBACK SettingsProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hWnd);
        int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(hWnd, hdc);
        if (dpi <= 0) dpi = 96;
        auto D = [dpi](int v) { return MulDiv(v, dpi, 96); };

        HWND lblStart = CreateWindowExW(0, L"STATIC", L"开始输入",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            D(16), D(18), D(90), D(24), hWnd, (HMENU)IDC_LBL_START, g_hInst, nullptr);

        g_hBoxStart = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_READONLY | ES_CENTER | WS_TABSTOP,
            D(112), D(14), D(210), D(30), hWnd, (HMENU)IDC_BOX_START, g_hInst, nullptr);

        HWND lblStop = CreateWindowExW(0, L"STATIC", L"停止",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            D(16), D(60), D(90), D(24), hWnd, (HMENU)IDC_LBL_STOP, g_hInst, nullptr);

        g_hBoxStop = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_READONLY | ES_CENTER | WS_TABSTOP,
            D(112), D(56), D(210), D(30), hWnd, (HMENU)IDC_BOX_STOP, g_hInst, nullptr);

        HWND tip = CreateWindowExW(0, L"STATIC",
            L"点上方方框后直接按键即可，支持单键或组合键：\r\n"
            L"  单键：F8 / F9 / F10 … F1–F24、Pause、PrtSc、媒体键\r\n"
            L"  组合：Alt+F8、Ctrl+F10、Ctrl+Shift+F9、Win+Alt+K …\r\n"
            L"Backspace / Delete 清空 = 禁用；Esc 取消本次修改。\r\n"
            L"字母/数字/空格等字符键必须配修饰键，否则会挡住正常打字。",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            D(16), D(94), D(400), D(96), hWnd, (HMENU)IDC_SET_TIP, g_hInst, nullptr);

        HWND chkHumanize = CreateWindowExW(0, L"BUTTON", L"拟人化节奏（逐字、带自然停顿）",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_AUTOCHECKBOX | WS_TABSTOP,
            D(16), D(196), D(240), D(24), hWnd, (HMENU)IDC_CHK_HUMANIZE, g_hInst, nullptr);

        HWND btnDefault = CreateWindowExW(0, L"BUTTON", L"恢复默认",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            D(16), D(232), D(100), D(32), hWnd, (HMENU)IDC_BTN_DEFAULT, g_hInst, nullptr);

        HWND btnOk = CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON | WS_TABSTOP,
            D(196), D(232), D(100), D(32), hWnd, (HMENU)IDOK, g_hInst, nullptr);

        HWND btnCancel = CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            D(304), D(232), D(100), D(32), hWnd, (HMENU)IDCANCEL, g_hInst, nullptr);

        HFONT font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        HWND all[] = { lblStart, g_hBoxStart, lblStop, g_hBoxStop, tip,
                       chkHumanize, btnDefault, btnOk, btnCancel };
        for (HWND h : all) SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);

        SubclassEdit(g_hBoxStart);
        SubclassEdit(g_hBoxStop);

        SubclassButton(chkHumanize, ROLE_CHECKBOX);
        SubclassButton(btnDefault, ROLE_NORMAL);
        SubclassButton(btnOk,      ROLE_PRIMARY);
        SubclassButton(btnCancel,  ROLE_NORMAL);

        SetPropW(chkHumanize, L"AutoTyperChecked",
                 g_typeOpts.humanize.enabled ? (HANDLE)1 : nullptr);

        SetWindowLongPtrW(g_hBoxStart, GWLP_USERDATA, 1);
        SetWindowLongPtrW(g_hBoxStop,  GWLP_USERDATA, 2);
        SetWindowSubclass(g_hBoxStart, CaptureBoxProc, 1, 1);
        SetWindowSubclass(g_hBoxStop,  CaptureBoxProc, 2, 2);

        g_capStart = g_hkStart;
        g_capStop  = g_hkStop;
        SetWindowTextW(g_hBoxStart, HotkeyToString(g_capStart).c_str());
        SetWindowTextW(g_hBoxStop,  HotkeyToString(g_capStop).c_str());

        g_captureTarget = 0;

        ApplyDarkTitleBar(hWnd, g_dark);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_CHK_HUMANIZE && HIWORD(wp) == BN_CLICKED) {
            // 与主界面复选框同一套状态语义：属性即真值源
            HWND ctl = (HWND)lp;
            SetPropW(ctl, L"AutoTyperChecked",
                     GetPropW(ctl, L"AutoTyperChecked") ? nullptr : (HANDLE)1);
            InvalidateRect(ctl, nullptr, TRUE);
            return 0;
        }
        if (id == IDOK)        { ApplySettings(hWnd); return 0; }
        if (id == IDCANCEL)    { DestroyWindow(hWnd); g_hSettings = nullptr; return 0; }
        if (id == IDC_BTN_DEFAULT && HIWORD(wp) == BN_CLICKED) {
            g_capStart.mods = 0;
            g_capStart.vk = VK_F9;
            g_capStop.mods = 0;
            g_capStop.vk = VK_F10;
            SetWindowTextW(g_hBoxStart, HotkeyToString(g_capStart).c_str());
            SetWindowTextW(g_hBoxStop,  HotkeyToString(g_capStop).c_str());
            return 0;
        }
        return 0;
    }

    case WM_ERASEBKGND:
        PaintWindowBackground(hWnd, (HDC)wp);
        return 1;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_pal.text);
        return (LRESULT)(g_brBg ? g_brBg : (HBRUSH)(COLOR_BTNFACE + 1));
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, g_pal.editBg);
        SetTextColor(dc, g_pal.editText);
        return (LRESULT)(g_brEdit ? g_brEdit : (HBRUSH)(COLOR_WINDOW + 1));
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        if (!dis || dis->CtlType != ODT_BUTTON) break;
        LONG_PTR roleRaw = GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA);
        // 和主窗口一致：复选框必须走 DrawOwnedCheckbox，
        // 否则会被当普通按钮整块填充成白色横条
        if ((BtnRole)roleRaw == ROLE_CHECKBOX) DrawOwnedCheckbox(dis);
        else                                   DrawOwnedButton(dis, (BtnRole)roleRaw);
        return TRUE;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        g_hSettings = nullptr;
        return 0;

    case WM_DESTROY:
        g_hSettings = nullptr;
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void ShowSettings(HWND owner)
{
    if (g_hSettings) {
        SetForegroundWindow(g_hSettings);
        return;
    }

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // 背景由 WM_ERASEBKGND 自绘
    wc.lpszClassName = kSetClass;
    RegisterClassExW(&wc);

    int w = S(432), h = S(318);
    RECT mrc;
    GetWindowRect(owner, &mrc);
    int x = mrc.left + (mrc.right - mrc.left - w) / 2;
    int y = mrc.top + (mrc.bottom - mrc.top - h) / 2;

    g_hSettings = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kSetClass, L"快捷键设置",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, owner, nullptr, g_hInst, nullptr);
    if (!g_hSettings) return;

    ShowWindow(g_hSettings, SW_SHOW);
    UpdateWindow(g_hSettings);
    SetForegroundWindow(g_hSettings);
}

// ============================================================
//  倒计时浮窗
// ============================================================
static LRESULT CALLBACK CountProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HBRUSH bg = CreateSolidBrush(g_pal.countBg);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_pal.countText);

        wchar_t num[32];
        swprintf_s(num, L"%d", g_countdownLeft);

        HFONT old = (HFONT)SelectObject(dc, g_countFont);
        RECT rNum = rc;
        rNum.top += S(8);
        rNum.bottom = rc.bottom - S(26);
        DrawTextW(dc, num, -1, &rNum, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(dc, g_countFontSmall);
        RECT rTip = rc;
        rTip.top = rc.bottom - S(28);
        DrawTextW(dc, L"切换到目标窗口…", -1, &rTip, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(dc, old);
        EndPaint(hWnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void CreateCountdownWindow()
{
    if (g_hCountdown) return;
    g_hCountdown = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        kCountClass, L"倒计时", WS_POPUP,
        0, 0, S(240), S(124), nullptr, nullptr, g_hInst, nullptr);
    if (g_hCountdown)
        SetLayeredWindowAttributes(g_hCountdown, 0, 235, LWA_ALPHA);
}

static void ShowCountdownWindow()
{
    CreateCountdownWindow();
    if (!g_hCountdown) return;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int w = S(240), h = S(124);
    int x = (sw - w) / 2;
    int y = S(80);

    SetWindowPos(g_hCountdown, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hCountdown, nullptr, TRUE);
    UpdateWindow(g_hCountdown);
}

static void HideCountdownWindow()
{
    if (g_hCountdown) ShowWindow(g_hCountdown, SW_HIDE);
    g_countdownLeft = 0;
}

// ============================================================
//  发送流程
// ============================================================
static void JoinWorker()
{
    if (g_worker.joinable()) g_worker.join();
}

static void StartWorkerNow()
{
    if (g_pending.empty()) return;

    JoinWorker();

    g_stop = false;
    std::wstring text = std::move(g_pending);
    g_pending.clear();

    // 每次启动都从界面/配置重算，保证 GUI 改动立即生效
    at::TypingOptions opts = g_typeOpts;
    opts.intervalMs    = GetSpeedMs();
    opts.forceEnglish  = IsChecked(g_hChkEnglish);
    opts.restoreLayout = opts.forceEnglish;

    g_running = true;
    SetStatus(L"正在输入…（按 F10 或“停止”中止）");

    g_worker = std::thread([text = std::move(text), opts]() {
        at::TypingReport rep = TypingWorker(text, opts);
        PostMessageW(g_hMain, WM_APP_DONE, (WPARAM)rep.status,
                     (LPARAM)rep.blockedEvents);
    });
}

static void BeginTyping()
{
    if (g_running.load()) {
        SetStatus(L"正在输入中，请先停止。");
        return;
    }

    std::wstring text = ReadEdit(g_hEdit);
    if (text.empty()) {
        SetStatus(L"文本框为空，没有可输入的内容。");
        MessageBeep(MB_ICONWARNING);
        return;
    }

    g_pending = std::move(text);
    g_stop = false;
    g_restoreAfter = IsChecked(g_hChkRestore);

    g_countdownLeft = GetCountdownSec();

    if (g_countdownLeft <= 0) {
        HideCountdownWindow();
        ShowWindow(g_hMain, SW_MINIMIZE);
        StartWorkerNow();
        return;
    }

    SetStatus(L"倒计时中…请切换到目标窗口");
    ShowCountdownWindow();
    ShowWindow(g_hMain, SW_MINIMIZE);
    SetTimer(g_hMain, TIMER_COUNTDOWN, 1000, nullptr);
}

// 可选提权：只在注入失败后询问，绝不默认强制
static bool OfferElevate(HWND owner)
{
    // 已经是管理员就不再骚扰
    at::IntegrityInfo probe = at::ProbeForegroundIntegrity();
    if (probe.self >= SECURITY_MANDATORY_HIGH_RID) {
        SetStatus(L"已具备管理员权限，但仍被拦截（可能是安全桌面或受保护进程）。");
        return false;
    }

    int r = MessageBoxW(owner,
        L"键鼠注入被系统拦截。\n\n"
        L"当前以普通权限运行，而目标窗口权限更高。\n"
        L"是否以管理员身份重新启动 AutoTyper？",
        L"需要更高权限", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    if (r != IDYES) {
        SetStatus(L"发送失败：请右键“以管理员身份运行”本程序。");
        return false;
    }

    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);

    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        if (GetLastError() == ERROR_CANCELLED) {
            SetStatus(L"已取消提权。");
        } else {
            SetStatus(L"提权启动失败。");
        }
        return false;
    }

    g_stop = true;
    DestroyWindow(owner);
    return true;
}

// 用 AttachThreadInput 绕过前台锁：SetForegroundWindow 在非用户输入触发的
// 场景下会被系统静默拒绝，导致注入打到别的窗口。selftest 必须自己保证前台。
static bool ForceForegroundWindow(HWND hWnd)
{
    if (!hWnd) return false;
    if (GetForegroundWindow() == hWnd) return true;

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

    if (attached)
        AttachThreadInput(myThread, fgThread, FALSE);

    return GetForegroundWindow() == hWnd;
}

static void RequestStop()
{
    g_stop = true;

    if (g_countdownLeft > 0) {
        KillTimer(g_hMain, TIMER_COUNTDOWN);
        HideCountdownWindow();
        g_pending.clear();
        ShowWindow(g_hMain, SW_SHOWNOACTIVATE);
        SetWindowPos(g_hMain, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetStatus(L"已取消。");
        return;
    }

    if (g_running.load()) SetStatus(L"正在停止…");
}

// ============================================================
//  文件载入
// ============================================================
static void DoLoadFile()
{
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMain;
    ofn.lpstrFilter = L"文本文件 (*.txt;*.md;*.log)\0*.txt;*.md;*.log\0所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring text;
    if (!ReadTextFile(path, text)) {
        MessageBoxW(g_hMain, L"文件读取失败（或文件过大）。", L"载入失败", MB_ICONERROR);
        return;
    }

    // Edit 控件内部用 CRLF
    std::wstring norm;
    norm.reserve(text.size() + text.size() / 8);
    for (wchar_t ch : text) {
        if (ch == L'\r') continue;
        if (ch == L'\n') { norm.push_back(L'\r'); norm.push_back(L'\n'); }
        else norm.push_back(ch);
    }

    SetWindowTextW(g_hEdit, norm.c_str());

    wchar_t msg[128];
    swprintf_s(msg, L"已载入 %zu 个字符。", norm.size());
    SetStatus(msg);
}

// ============================================================
//  布局与创建
// ============================================================
static void Layout(HWND hWnd)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    int cx = rc.right;
    int cy = rc.bottom;
    int m = S(12);

    int y = m;
    MoveWindow(g_hHint, m, y, cx - 2 * m, S(36), TRUE);
    y += S(40);

    int bottomH = S(136);
    int editH = cy - y - bottomH;
    if (editH < S(80)) editH = S(80);
    MoveWindow(g_hEdit, m, y, cx - 2 * m, editH, TRUE);
    y += editH + S(8);

    // 第一行：间隔 / 倒计时 / 两个复选
    MoveWindow(g_hSpeedLbl, m, y + S(4), S(64), S(22), TRUE);
    MoveWindow(g_hSpeed, m + S(66), y, S(62), S(26), TRUE);
    MoveWindow(g_hCdLbl, m + S(136), y + S(4), S(70), S(22), TRUE);
    MoveWindow(g_hCd, m + S(208), y, S(58), S(26), TRUE);
    // 右侧两个复选框，从右往左排，窄窗口也不会挤在一起
    int restoreW = S(116);
    int englishW = S(124);

    int xRestore = cx - m - restoreW;
    int xEnglish = xRestore - S(10) - englishW;

    MoveWindow(g_hChkEnglish, xEnglish, y + S(2), englishW, S(24), TRUE);
    MoveWindow(g_hChkRestore, xRestore, y + S(2), restoreW, S(24), TRUE);

    y += S(36);

    // 第二行：按钮
    MoveWindow(g_hBtnLoad, m, y, S(104), S(32), TRUE);
    MoveWindow(g_hBtnClear, m + S(112), y, S(76), S(32), TRUE);
    MoveWindow(g_hBtnStart, m + S(196), y, S(210), S(32), TRUE);
    MoveWindow(g_hBtnStop, m + S(414), y, S(150), S(32), TRUE);
    MoveWindow(g_hBtnSettings, m + S(572), y, S(96), S(32), TRUE);

    CenterEditTextVertically(g_hSpeed);
    CenterEditTextVertically(g_hCd);

    y += S(38);
    // 右下角版本号：与右边缘留出更明显的间距
    int verW    = S(52);
    int verGap  = S(26);
    int verRight = cx - m - verGap;
    MoveWindow(g_hStatus, m, y, verRight - verW - m - S(12), S(24), TRUE);
    MoveWindow(g_hRepo, verRight - verW, y, verW, S(24), TRUE);
}

static void CreateControls(HWND hWnd)
{
    HDC hdc = GetDC(hWnd);
    g_dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hWnd, hdc);
    if (g_dpi <= 0) g_dpi = 96;

    g_font = CreateFontW(-MulDiv(10, g_dpi, 72), 0, 0, 0, FW_NORMAL,
                         FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    g_countFont = CreateFontW(-MulDiv(30, g_dpi, 72), 0, 0, 0, FW_BOLD,
                              FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    g_countFontSmall = CreateFontW(-MulDiv(10, g_dpi, 72), 0, 0, 0, FW_NORMAL,
                                   FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    g_hHint = CreateWindowExW(0, L"STATIC",
        L"① 粘贴文本或点“载入文件”　② 点“开始输入 / F9”　③ 在倒计时内切到目标窗口，之后程序自动逐字键入。\r\n"
        L"支持中文、Emoji、换行与制表符；不使用剪贴板，虚拟机 / 远程桌面内同样有效。",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hWnd, (HMENU)IDC_HINT, g_hInst, nullptr);

    g_hEdit = CreateWindowExW(0, L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_TABSTOP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT, g_hInst, nullptr);
    SendMessageW(g_hEdit, EM_SETLIMITTEXT, (WPARAM)0x7FFFFFFE, 0);

    g_hSpeedLbl = CreateWindowExW(0, L"STATIC", L"间隔(ms)",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hWnd, (HMENU)IDC_SPEED_LBL, g_hInst, nullptr);
    g_hSpeed = CreateWindowExW(0, L"EDIT", L"8",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_SPEED, g_hInst, nullptr);

    g_hCdLbl = CreateWindowExW(0, L"STATIC", L"倒计时(s)",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hWnd, (HMENU)IDC_CD_LBL, g_hInst, nullptr);
    g_hCd = CreateWindowExW(0, L"EDIT", L"3",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_CD, g_hInst, nullptr);

    g_hChkEnglish = CreateWindowExW(0, L"BUTTON", L"切换英文布局",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_AUTOCHECKBOX | WS_TABSTOP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_CHK_ENGLISH, g_hInst, nullptr);
    SetChecked(g_hChkEnglish, true);

    g_hChkRestore = CreateWindowExW(0, L"BUTTON", L"完成后恢复",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_AUTOCHECKBOX | WS_TABSTOP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_CHK_RESTORE, g_hInst, nullptr);
    SetChecked(g_hChkRestore, true);

    g_hBtnLoad = CreateWindowExW(0, L"BUTTON", L"载入文件…",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_LOAD, g_hInst, nullptr);
    g_hBtnClear = CreateWindowExW(0, L"BUTTON", L"清空",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_CLEAR, g_hInst, nullptr);
    g_hBtnStart = CreateWindowExW(0, L"BUTTON", L"开始输入",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_START, g_hInst, nullptr);
    g_hBtnStop = CreateWindowExW(0, L"BUTTON", L"停止",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_STOP, g_hInst, nullptr);
    g_hBtnSettings = CreateWindowExW(0, L"BUTTON", L"快捷键…",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_SETTINGS, g_hInst, nullptr);

    // 右下角仓库跳转按钮
    // 用 STATIC 承载：BUTTON 的 OWNERDRAW 会带系统底色，在毛玻璃上露出方块
    g_hRepo = CreateWindowExW(0, L"STATIC", L"v1.0.1",
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOTIFY,
        0, 0, 0, 0, hWnd, (HMENU)IDC_REPO, g_hInst, nullptr);

    g_hStatus = CreateWindowExW(0, L"STATIC", L"就绪。",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
        0, 0, 0, 0, hWnd, (HMENU)IDC_STATUS, g_hInst, nullptr);

    HWND all[] = { g_hHint, g_hEdit, g_hSpeedLbl, g_hSpeed, g_hCdLbl, g_hCd,
                   g_hChkEnglish, g_hChkRestore, g_hBtnLoad, g_hBtnClear,
                   g_hBtnStart, g_hBtnStop, g_hBtnSettings, g_hStatus };
    for (HWND h : all) ApplyFont(h);

    SubclassVersionLabel(g_hRepo);

    SubclassEdit(g_hEdit);
    SubclassEdit(g_hSpeed);
    SubclassEdit(g_hCd);

    SubclassButton(g_hChkEnglish,  ROLE_CHECKBOX);
    SubclassButton(g_hChkRestore,  ROLE_CHECKBOX);
    SubclassButton(g_hBtnLoad,     ROLE_NORMAL);
    SubclassButton(g_hBtnClear,    ROLE_NORMAL);
    SubclassButton(g_hBtnStart,    ROLE_PRIMARY);
    SubclassButton(g_hBtnStop,     ROLE_DANGER);
    SubclassButton(g_hBtnSettings, ROLE_NORMAL);

    UpdateHotkeyLabels();
}

// ============================================================
//  主窗口过程
// ============================================================
static void UpdateCharCount()
{
    int len = GetWindowTextLengthW(g_hEdit);
    wchar_t msg[96];
    swprintf_s(msg, L"字符数：%d", len);
    SetStatus(msg);
}

static LRESULT CALLBACK MainProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hWnd;
        LoadConfig();
        ApplyThemePalette();
        CreateControls(hWnd);
        Layout(hWnd);
        ApplyDarkTitleBar(hWnd, g_dark);
        ApplyWindowIcons(hWnd, g_dark);
        // 毛玻璃为深色主题默认；浅色主题走纯色（见 EnableAcrylic 注释）
        EnableAcrylic(hWnd, true);
        // 仅"系统不支持"才提示；浅色主题主动用纯色，不算降级
        if (!g_acrylicSupported && !g_acrylicThemeOff) {
            SetStatus(L"当前系统不支持毛玻璃，已回退为纯色背景。");
        }
        RegisterHotkeys(hWnd);
        if (!g_hkStartOK && g_hkStart.vk != 0) {
            SetStatus(L"警告：“开始”热键被占用，请点“快捷键…”更换。");
        }
        // 启动即做一次权限预检：仅在注意到目标更高时提示，不阻塞使用
        at::IntegrityInfo ii = at::ProbeForegroundIntegrity();
        if (ii.targetHigher && g_hkStartOK) {
            SetStatus(L"提示：目标窗口权限更高，发送前请以管理员身份运行本程序。");
        }
        if (g_selftest) {
            SetTimer(hWnd, TIMER_SELFTEST, 700, nullptr);
        }
        return 0;
    }

    case WM_SIZE:
        if (g_hEdit) {
            Layout(hWnd);
            // 缩小窗口时子控件收缩会露出旧像素，必须整窗重绘擦掉
            RedrawWindow(hWnd, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        return 0;

    case WM_ERASEBKGND:
        PaintWindowBackground(hWnd, (HDC)wp);
        return 1;

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        HWND ctl = (HWND)lp;
        SetBkMode(dc, TRANSPARENT);
        // 提示文字用次要色，状态栏用主色
        bool dim = (ctl == g_hHint);
        SetTextColor(dc, dim ? g_pal.textDim : g_pal.text);
        // 必须是实心刷；HOLLOW_BRUSH 会让旧文字残留
        return (LRESULT)(g_brBg ? g_brBg : (HBRUSH)(COLOR_BTNFACE + 1));
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, g_pal.editBg);
        SetTextColor(dc, g_pal.editText);
        return (LRESULT)(g_brEdit ? g_brEdit : (HBRUSH)(COLOR_WINDOW + 1));
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        if (!dis) break;

        if (dis->CtlType != ODT_BUTTON) break;

        LONG_PTR roleRaw = GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA);
        if ((BtnRole)roleRaw == ROLE_CHECKBOX) DrawOwnedCheckbox(dis);
        else                                   DrawOwnedButton(dis, (BtnRole)roleRaw);
        return TRUE;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = S(720);
        mmi->ptMinTrackSize.y = S(520);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);

        if (id == IDC_BTN_LOAD && code == BN_CLICKED) { DoLoadFile(); return 0; }
        if (id == IDC_BTN_CLEAR && code == BN_CLICKED) {
            SetWindowTextW(g_hEdit, L"");
            SetStatus(L"已清空。");
            SetFocus(g_hEdit);
            return 0;
        }
        if (id == IDC_BTN_START && code == BN_CLICKED) { BeginTyping(); return 0; }
        if (id == IDC_BTN_STOP && code == BN_CLICKED) { RequestStop(); return 0; }
        if (id == IDC_BTN_SETTINGS && code == BN_CLICKED) { ShowSettings(hWnd); return 0; }


        if (id == IDC_REPO && code == STN_CLICKED) {
            ShellExecuteW(hWnd, L"open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }

        if (id == IDC_CHK_ENGLISH && code == BN_CLICKED) { ToggleChecked(g_hChkEnglish); return 0; }
        if (id == IDC_CHK_RESTORE && code == BN_CLICKED) { ToggleChecked(g_hChkRestore); return 0; }

        if (id == IDC_EDIT && code == EN_CHANGE) { UpdateCharCount(); return 0; }
        return 0;
    }

    case WM_HOTKEY:
        if (wp == 1) { BeginTyping(); return 0; }
        if (wp == 2) { RequestStop(); return 0; }
        return 0;

    case WM_TIMER:
        if (wp == TIMER_COUNTDOWN) {
            if (g_countdownLeft > 0) {
                InvalidateRect(g_hCountdown, nullptr, TRUE);
            }
            --g_countdownLeft;
            if (g_countdownLeft <= 0) {
                KillTimer(hWnd, TIMER_COUNTDOWN);
                HideCountdownWindow();
                StartWorkerNow();
            } else {
                InvalidateRect(g_hCountdown, nullptr, TRUE);
            }
            return 0;
        }
        if (wp == TIMER_SELFTEST) {
            KillTimer(hWnd, TIMER_SELFTEST);
            // 自测：向自己的编辑框注入一段文本
            ShowWindow(hWnd, SW_SHOW);
            if (!ForceForegroundWindow(hWnd)) {
                // 拿不到前台就明确失败，避免把"注入打到别的窗口"误判成编码问题
                SetStatus(L"自测无法取得前台焦点，结果不可信。");
            }
            SetFocus(g_hEdit);
            g_testExpected = L"Hello 世界 123\r\n第二行 😀 end";
            std::wstring payload = g_testExpected;
            at::TypingOptions opts;      // selftest 固定用默认参数，不受用户配置影响
            opts.intervalMs = 4;
            opts.checkUipi = false;      // 目标是自身窗口，无需预检
            TypingWorker(payload, opts);
            SetTimer(hWnd, TIMER_SELFTEST2, 900, nullptr);
            return 0;
        }
        if (wp == TIMER_SELFTEST2) {
            KillTimer(hWnd, TIMER_SELFTEST2);
            std::wstring got = ReadEdit(g_hEdit);

            auto normalize = [](std::wstring s) {
                std::wstring r;
                for (wchar_t c : s) { if (c != L'\r') r.push_back(c); }
                return r;
            };
            bool pass = (normalize(got) == normalize(g_testExpected));

            wchar_t temp[MAX_PATH];
            GetTempPathW(MAX_PATH, temp);
            std::wstring outPath = std::wstring(temp) + L"autotyper_selftest.txt";

            HANDLE f = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                char utf8[2048] = { 0 };
                std::wstring report =
                    std::wstring(L"SELFTEST=") + (pass ? L"PASS" : L"FAIL") +
                    L"\r\nexpected=" + normalize(g_testExpected) +
                    L"\r\nactual  =" + normalize(got) + L"\r\n";
                int n = WideCharToMultiByte(CP_UTF8, 0, report.c_str(), (int)report.size(),
                                            utf8, (int)sizeof(utf8) - 1, nullptr, nullptr);
                DWORD wr = 0;
                if (n > 0) WriteFile(f, utf8, (DWORD)n, &wr, nullptr);
                CloseHandle(f);
            }

            DestroyWindow(hWnd);
            return 0;
        }
        return 0;

    case WM_APP_DONE:
        JoinWorker();
        g_running = false;
        {
            at::TypingStatus st = (at::TypingStatus)wp;
            if (st == at::TypingStatus::Blocked) {
                SetStatus(L"发送失败：目标窗口权限更高，请以管理员身份重新运行。");
                if (OfferElevate(hWnd)) return 0;
            } else if (st == at::TypingStatus::TargetHigherIL) {
                SetStatus(L"目标窗口权限更高，注入可能被拦截；可尝试以管理员身份运行。");
                if (OfferElevate(hWnd)) return 0;
            } else if (g_stop.load()) {
                SetStatus(L"已停止。");
            } else {
                SetStatus(L"输入完成。");
            }
        }
        MessageBeep(MB_OK);
        if (g_restoreAfter) {
            ShowWindow(hWnd, SW_SHOWNOACTIVATE);
            SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        return 0;

    case WM_CLOSE:
        g_stop = true;
        KillTimer(hWnd, TIMER_COUNTDOWN);
        HideCountdownWindow();
        JoinWorker();
        UnregisterHotKey(hWnd, 1);
        UnregisterHotKey(hWnd, 2);
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// ============================================================
//  GUI 模式
// ============================================================
static int RunGui(int nCmdShow)
{
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;   // 背景由 WM_ERASEBKGND 自绘
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc)) return 1;

    WNDCLASSEXW cc;
    ZeroMemory(&cc, sizeof(cc));
    cc.cbSize = sizeof(cc);
    cc.lpfnWndProc = CountProc;
    cc.hInstance = g_hInst;
    cc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cc.hbrBackground = nullptr;
    cc.lpszClassName = kCountClass;
    RegisterClassExW(&cc);

    int w = S(800), h = S(640);
    HWND hWnd = CreateWindowExW(
        WS_EX_APPWINDOW, kMainClass, L"AutoTyper — 自动键入（虚拟机/远程桌面可用）",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        nullptr, nullptr, g_hInst, nullptr);
    if (!hWnd) return 1;

    ShowWindow(hWnd, g_selftest ? SW_SHOW : nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_font) DeleteObject(g_font);
    if (g_countFont) DeleteObject(g_countFont);
    if (g_countFontSmall) DeleteObject(g_countFontSmall);
    return 0;
}

// ============================================================
//  无界面模式（命令行）
// ============================================================
using at::Unescape;

static void ConsolePrint(const wchar_t* text)
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteConsoleW(h, text, (DWORD)wcslen(text), &wr, nullptr);
        WriteConsoleW(h, L"\r\n", 2, &wr, nullptr);
    }
    FreeConsole();
}

static int RunHeadless(const std::wstring& text, int delaySec, int intervalMs,
                       bool forceEnglish, bool humanize)
{
    if (delaySec > 0) {
        wchar_t buf[160];
        swprintf_s(buf,
            L"[AutoTyper] %d 秒后开始输入 %zu 个字符，请切换到目标窗口…",
            delaySec, at::CountCodePoints(text));
        ConsolePrint(buf);
        Sleep((DWORD)delaySec * 1000);
    }

    // 命令行以参数为准，其余高级项沿用 ini
    at::TypingOptions opts = g_typeOpts;
    opts.intervalMs    = intervalMs;
    opts.forceEnglish  = forceEnglish;
    opts.restoreLayout = forceEnglish;
    if (humanize) opts.humanize.enabled = true;

    at::TypingReport rep = TypingWorker(text, opts);
    MessageBeep(MB_OK);

    switch (rep.status) {
    case at::TypingStatus::Blocked:
        ConsolePrint(L"[AutoTyper] 注入被拦截，请以管理员身份运行。");
        return 2;
    case at::TypingStatus::TargetHigherIL:
        ConsolePrint(L"[AutoTyper] 目标窗口权限更高，注入可能被拦截；请以管理员身份运行。");
        return 3;
    case at::TypingStatus::Empty:
        ConsolePrint(L"[AutoTyper] 文本为空。");
        return 1;
    default:
        break;
    }

    wchar_t done[160];
    swprintf_s(done, L"[AutoTyper] 完成：%d 个字符，%d 批。",
               rep.totalCodePoints, rep.batches);
    ConsolePrint(done);
    return 0;
}

// ============================================================
//  入口
// ============================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow)
{
    g_hInst = hInst;
    LoadAppIcons();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;

    std::wstring filePath;
    std::wstring inlineText;
    bool hasText = false;
    int delaySec = 3;
    int intervalMs = 8;
    bool forceEnglish = false;
    bool wantGui = false;
    bool humanize = false;
    int  batchChars = -1;      // -1 = 未指定，沿用 ini
    bool intervalSpecified = false;   // 未传 -i 时允许 ini 的 [typing] interval 生效

    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&](std::wstring& dst) -> bool {
            if (i + 1 >= argc) return false;
            dst = argv[++i];
            return true;
        };

        if (a == L"-f" || a == L"--file") {
            if (next(filePath)) hasText = true;
        } else if (a == L"-c" || a == L"--text") {
            std::wstring raw;
            if (next(raw)) { inlineText = Unescape(raw); hasText = true; }
        } else if (a == L"-d" || a == L"--delay") {
            std::wstring v;
            if (next(v)) delaySec = (int)wcstol(v.c_str(), nullptr, 10);
        } else if (a == L"-i" || a == L"--interval") {
            std::wstring v;
            if (next(v)) {
                intervalMs = (int)wcstol(v.c_str(), nullptr, 10);
                intervalSpecified = true;
            }
        } else if (a == L"--no-english") {
            forceEnglish = false;
        } else if (a == L"--english") {
            forceEnglish = true;
        } else if (a == L"--humanize") {
            humanize = true;
        } else if (a == L"--batch") {
            std::wstring v;
            if (next(v)) batchChars = (int)wcstol(v.c_str(), nullptr, 10);
        } else if (a == L"--selftest") {
            g_selftest = true;
        } else if (a == L"--gui" || a == L"-g") {
            wantGui = true;
        } else if (a == L"-h" || a == L"--help" || a == L"/?") {
            ConsolePrint(
                L"AutoTyper 用法：\r\n"
                L"  AutoTyper.exe                              打开图形界面\r\n"
                L"  AutoTyper.exe -c \"文本\" [-d 3] [-i 8]      直接键入指定文本\r\n"
                L"  AutoTyper.exe -f 文件.txt [-d 3] [-i 8]    直接键入文件内容\r\n"
                L"  AutoTyper.exe --selftest                   运行自测\r\n"
                L"参数：\r\n"
                L"  -d  倒计时秒数（默认 3，0 立即开始）\r\n"
                L"  -i  每字符间隔毫秒（默认 8，0 最快）\r\n"
                L"  --batch N  每批注入字符数（0=自动，默认沿用 ini）\r\n"
                L"  --english / --no-english  是否在发送前切换英文键盘布局（默认不切）\r\n"
                L"  --humanize  启用拟人化节奏（默认关闭）\r\n");
            LocalFree(argv);
            return 0;
        }
    }

    LocalFree(argv);

    if (delaySec < 0) delaySec = 0;
    if (delaySec > 60) delaySec = 60;
    if (intervalMs < 0) intervalMs = 0;
    if (intervalMs > 1000) intervalMs = 1000;

    if (g_selftest) {
        g_selftest = true;
        return RunGui(SW_SHOW);
    }

    if (hasText && !wantGui) {
        // 命令行模式也要读 ini 里的高级项（[typing] 段），但显式参数优先级更高
        LoadConfig();
        if (batchChars >= 0) {
            if (batchChars > 256) batchChars = 256;
            g_typeOpts.batchChars = batchChars;
        }
        if (!intervalSpecified) intervalMs = g_typeOpts.intervalMs;

        std::wstring text = inlineText;
        if (!filePath.empty()) {
            if (!ReadTextFile(filePath, text)) {
                ConsolePrint(L"[AutoTyper] 文件读取失败。");
                return 1;
            }
        }
        if (text.empty()) {
            ConsolePrint(L"[AutoTyper] 文本为空。");
            return 1;
        }
        return RunHeadless(text, delaySec, intervalMs, forceEnglish, humanize);
    }

    return RunGui(nCmdShow);
}
