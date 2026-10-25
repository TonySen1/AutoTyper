// 验收用接收端：独立进程的多行 EDIT 窗口，定时把内容落盘。
// 用法: testhost.exe <输出文件路径>
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <string>

#pragma comment(lib, "shell32.lib")

static HWND g_edit = nullptr;
static std::wstring g_out;

static void Dump()
{
    int len = GetWindowTextLengthW(g_edit);
    std::wstring buf((size_t)len + 1, L'\0');
    GetWindowTextW(g_edit, &buf[0], len + 1);
    buf.resize((size_t)len);

    HANDLE h = CreateFileW(g_out.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    // UTF-16LE + BOM
    DWORD wr = 0;
    const unsigned char bom[2] = { 0xFF, 0xFE };
    WriteFile(h, bom, 2, &wr, nullptr);
    if (!buf.empty())
        WriteFile(h, buf.data(), (DWORD)(buf.size() * sizeof(wchar_t)), &wr, nullptr);
    CloseHandle(h);
}

static LRESULT CALLBACK Proc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN,
            0, 0, 0, 0, hWnd, (HMENU)1, nullptr, nullptr);
        HFONT f = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                              L"Microsoft YaHei UI");
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)f, TRUE);
        SetTimer(hWnd, 1, 300, nullptr);
        return 0;
    }
    case WM_SIZE:
        MoveWindow(g_edit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_TIMER:
        Dump();
        return 0;
    case WM_DESTROY:
        Dump();
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int nShow)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2) g_out = argv[1];
    else g_out = L"testhost_out.txt";
    if (argv) LocalFree(argv);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AutoTyperTestHost";
    RegisterClassExW(&wc);

    HWND h = CreateWindowExW(WS_EX_APPWINDOW, L"AutoTyperTestHost",
                             L"AutoTyperTestHost",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             120, 120, 900, 600,
                             nullptr, nullptr, hInst, nullptr);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);

    SetForegroundWindow(h);
    SetActiveWindow(h);
    SetFocus(g_edit);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
