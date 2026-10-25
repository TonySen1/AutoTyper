"""文件载入 + 停止/热键 + 边界场景验收。"""
import ctypes
import os
import subprocess
import sys
import time
from ctypes import wintypes

u = ctypes.WinDLL("user32", use_last_error=True)
u.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
u.FindWindowW.restype = wintypes.HWND
u.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
u.FindWindowExW.restype = wintypes.HWND
u.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
u.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPVOID]
u.SendMessageW.restype = wintypes.LPARAM
u.SetForegroundWindow.argtypes = [wintypes.HWND]
u.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
u.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
u.IsWindowVisible.argtypes = [wintypes.HWND]

u.SendInput.argtypes = [wintypes.UINT, ctypes.c_void_p, ctypes.c_int]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.c_void_p)]


class _U(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("pad", ctypes.c_byte * 32)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("u", _U)]


def _tap_vk(vk):
    a = (INPUT * 2)()
    for i, fl in enumerate((0, 0x0002)):
        a[i].type = 1
        a[i].u.ki.wVk = vk
        a[i].u.ki.dwFlags = fl
    u.SendInput(2, ctypes.byref(a), ctypes.sizeof(INPUT))


def activate(hwnd, focus_child=None):
    """用 AttachThreadInput 激活窗口，不注入任何按键。

    注入 ALT 解锁前台锁会和 AutoTyper 的 SendInput 抢事件队列，
    导致上下文串味，所以这里完全不碰键盘。
    """
    fg = u.GetForegroundWindow()
    if fg == hwnd:
        if focus_child:
            u.SetFocus(focus_child)
        return True

    t_fg = u.GetWindowThreadProcessId(fg, None)
    t_t = u.GetWindowThreadProcessId(hwnd, None)
    t_me = ctypes.WinDLL("kernel32").GetCurrentThreadId()

    u.AttachThreadInput(t_me, t_fg, True)
    u.AttachThreadInput(t_me, t_t, True)
    u.ShowWindow(hwnd, 5)
    u.BringWindowToTop(hwnd)
    u.SetForegroundWindow(hwnd)
    if focus_child:
        u.SetFocus(focus_child)
    u.AttachThreadInput(t_me, t_t, False)
    u.AttachThreadInput(t_me, t_fg, False)

    time.sleep(0.8)
    return u.GetForegroundWindow() == hwnd

ROOT = r"C:\Users\notfa\autotyper"
DIST = os.path.join(ROOT, "dist", "AutoTyper.exe")
HOST = os.path.join(ROOT, "obj", "testhost.exe")

failures = []


def check(name, cond, extra=""):
    print(("  [PASS] " if cond else "  [FAIL] ") + name + (("  " + extra) if extra else ""))
    if not cond:
        failures.append(name)


def wait_window(cls, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        h = u.FindWindowW(cls, None)
        if h:
            return h
        time.sleep(0.05)
    return 0


def get_status(main_hwnd):
    st = u.FindWindowExW(main_hwnd, 0, "Static", None)
    # 状态栏是最后一个 Static；改为按 ID 找不到，直接枚举
    buf = ctypes.create_unicode_buffer(512)
    u.GetWindowTextW(st, buf, 512)
    return buf.value


def text_of(hwnd):
    buf = ctypes.create_unicode_buffer(65536)
    u.GetWindowTextW(hwnd, buf, 65536)
    return buf.value


def run_utf8_file():
    """UTF-8 BOM 文件 -> 载入 -> 注入"""
    print("\n=== 用例: UTF-8 BOM 文件载入 ===")
    path = os.path.join(ROOT, "obj", "case_utf8.txt")
    content = "UTF8 文件测试\r\n中文：自动键入\r\nEmoji 😀\r\n"
    with open(path, "wb") as f:
        f.write(b"\xef\xbb\xbf" + content.replace("\r\n", "\n").encode("utf-8"))

    out = os.path.join(ROOT, "obj", "case_utf8.out")
    if os.path.exists(out):
        os.remove(out)
    host = subprocess.Popen([HOST, out])
    time.sleep(1.2)

    gui = subprocess.Popen([DIST, "-f", path, "-d", "2", "-i", "3"])
    time.sleep(0.5)
    th = wait_window("AutoTyperTestHost")
    if th:
        activate(th)
    gui.wait(timeout=40)
    time.sleep(1.2)
    host.terminate()
    time.sleep(0.4)

    ok = os.path.exists(out)
    check("输出文件生成", ok)
    if ok:
        raw = open(out, "rb").read()
        got = raw[2:].decode("utf-16-le")
        exp = content.replace("\r\n", "\n")
        check("UTF-8 文件内容一致", got.replace("\r\n", "\n") == exp,
              "" if got.replace("\r\n", "\n") == exp else repr(got))


def run_ansi_file():
    """GBK 文件 -> 载入 -> 注入"""
    print("\n=== 用例: GBK(ANSI) 文件载入 ===")
    path = os.path.join(ROOT, "obj", "case_gbk.txt")
    content = "GBK 编码测试\r\n中文短句\r\n"
    with open(path, "wb") as f:
        f.write(content.replace("\r\n", "\n").encode("gbk"))

    out = os.path.join(ROOT, "obj", "case_gbk.out")
    if os.path.exists(out):
        os.remove(out)
    host = subprocess.Popen([HOST, out])
    time.sleep(1.2)

    gui = subprocess.Popen([DIST, "-f", path, "-d", "2", "-i", "3"])
    time.sleep(0.5)
    th = wait_window("AutoTyperTestHost")
    if th:
        activate(th)
    gui.wait(timeout=40)
    time.sleep(1.2)
    host.terminate()
    time.sleep(0.4)

    ok = os.path.exists(out)
    check("输出文件生成", ok)
    if ok:
        raw = open(out, "rb").read()
        got = raw[2:].decode("utf-16-le")
        exp = content.replace("\r\n", "\n")
        check("GBK 文件内容一致", got.replace("\r\n", "\n") == exp,
              "" if got.replace("\r\n", "\n") == exp else repr(got))


def run_gui_stop():
    """GUI 中启动后立刻取消倒计时"""
    print("\n=== 用例: 倒计时阶段取消 ===")
    gui = subprocess.Popen([DIST])
    main_hwnd = wait_window("AutoTyperMainWnd")
    check("主窗口出现", main_hwnd != 0)
    if not main_hwnd:
        gui.kill()
        return

    edit = u.FindWindowExW(main_hwnd, 0, "Edit", None)
    u.SetWindowTextW(edit, "SHOULD NOT APPEAR " * 30)
    time.sleep(0.2)

    u.PostMessageW(main_hwnd, 0x0111, 1011, 0)  # 开始
    time.sleep(0.5)
    count = wait_window("AutoTyperCountWnd", timeout=3)
    check("倒计时浮窗出现", count != 0)

    u.PostMessageW(main_hwnd, 0x0111, 1012, 0)  # 停止
    time.sleep(0.6)
    check("倒计时浮窗已关闭", not bool(u.IsWindowVisible(count)) if count else True)
    check("主窗口已还原", bool(u.IsWindowVisible(main_hwnd)))

    gui.kill()
    time.sleep(0.3)


def run_gui_empty():
    """空文本不应启动"""
    print("\n=== 用例: 空文本 ===")
    gui = subprocess.Popen([DIST])
    main_hwnd = wait_window("AutoTyperMainWnd")
    check("主窗口出现", main_hwnd != 0)
    if not main_hwnd:
        gui.kill()
        return

    u.PostMessageW(main_hwnd, 0x0111, 1011, 0)
    time.sleep(0.8)
    count = wait_window("AutoTyperCountWnd", timeout=2)
    check("空文本不启动倒计时", count == 0 or not bool(u.IsWindowVisible(count)))

    gui.kill()
    time.sleep(0.3)


def run_multiline_headless():
    """命令行 -f 与 -c 混合"""
    print("\n=== 用例: -c 转义解析 ===")
    out = os.path.join(ROOT, "obj", "case_esc.out")
    if os.path.exists(out):
        os.remove(out)
    host = subprocess.Popen([HOST, out])
    time.sleep(1.2)

    gui = subprocess.Popen([DIST, "-c", "A\\tB\\nC\\\\D \\x", "-d", "2", "-i", "3"])
    time.sleep(0.5)
    th = wait_window("AutoTyperTestHost")
    if th:
        activate(th)
    gui.wait(timeout=40)
    time.sleep(1.2)
    host.terminate()
    time.sleep(0.4)

    ok = os.path.exists(out)
    check("输出文件生成", ok)
    if ok:
        raw = open(out, "rb").read()
        got = raw[2:].decode("utf-16-le")
        exp = "A\tB\nC\\D \\x"
        check("转义序列正确", got.replace("\r\n", "\n") == exp,
              "" if got.replace("\r\n", "\n") == exp else repr(got))


def main():
    run_utf8_file()
    run_ansi_file()
    run_multiline_headless()
    run_gui_empty()
    run_gui_stop()

    print()
    if failures:
        print("结果: FAILED ->", failures)
        return 1
    print("结果: ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
