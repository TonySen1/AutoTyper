"""单键热键真实触发验收：F9 按下后确实开始注入。"""
import ctypes
import os
import subprocess
import sys
import threading
import time
from ctypes import wintypes

u = ctypes.WinDLL("user32", use_last_error=True)
for fn, res, args in [
    ("FindWindowW", wintypes.HWND, [wintypes.LPCWSTR, wintypes.LPCWSTR]),
    ("FindWindowExW", wintypes.HWND, [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]),
    ("PostMessageW", wintypes.BOOL, [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]),
    ("SendMessageW", wintypes.LPARAM, None),
    ("SetWindowTextW", wintypes.BOOL, [wintypes.HWND, wintypes.LPCWSTR]),
    ("GetWindowTextLengthW", wintypes.INT, [wintypes.HWND]),
    ("IsWindowVisible", wintypes.BOOL, [wintypes.HWND]),
    ("ShowWindow", wintypes.BOOL, [wintypes.HWND, ctypes.c_int]),
    ("SetForegroundWindow", wintypes.BOOL, [wintypes.HWND]),
    ("GetForegroundWindow", wintypes.HWND, []),
    ("SendInput", wintypes.UINT, [wintypes.UINT, ctypes.c_void_p, ctypes.c_int]),
    ("GetDlgCtrlID", wintypes.INT, [wintypes.HWND]),
    ("EnumChildWindows", wintypes.BOOL, [wintypes.HWND, ctypes.c_void_p, wintypes.LPARAM]),
    ("SetFocus", wintypes.HWND, [wintypes.HWND]),
    ("GetWindowThreadProcessId", wintypes.DWORD, [wintypes.HWND, ctypes.c_void_p]),
    ("AttachThreadInput", wintypes.BOOL, [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]),
    ("BringWindowToTop", wintypes.BOOL, [wintypes.HWND]),
]:
    f = getattr(u, fn)
    f.restype = res
    if args is not None:
        f.argtypes = args


class KI(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.c_void_p)]


class _U(ctypes.Union):
    _fields_ = [("ki", KI), ("pad", ctypes.c_byte * 32)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("u", _U)]


def tap_vk(vk):
    a = (INPUT * 2)()
    for i, fl in enumerate((0, 0x0002)):
        a[i].type = 1
        a[i].u.ki.wVk = vk
        a[i].u.ki.dwFlags = fl
    return u.SendInput(2, ctypes.byref(a), ctypes.sizeof(INPUT))


ROOT = r"C:\Users\notfa\autotyper"
DIST = os.path.join(ROOT, "dist", "AutoTyper.exe")
HOST = os.path.join(ROOT, "obj", "testhost.exe")
INI = os.path.join(ROOT, "dist", "AutoTyper.ini")

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


def set_text(hwnd, text):
    return bool(u.SendMessageW(hwnd, 0x000C, 0, ctypes.c_wchar_p(text))) if hwnd else False


def children(hwnd):
    out = {}
    CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def cb(h, lp):
        cid = u.GetDlgCtrlID(h)
        if cid > 0:
            out[cid] = h
        return True

    u.EnumChildWindows(hwnd, CB(cb), 0)
    return out


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


def main():
    print("=== 用例: 单键 F9 触发输入 ===")
    if os.path.exists(INI):
        os.remove(INI)
    with open(INI, "w", encoding="utf-16-le") as f:
        f.write("[hotkey]\r\nstart=F9\r\nstop=F10\r\n")

    out = os.path.join(ROOT, "obj", "single.out")
    if os.path.exists(out):
        os.remove(out)

    host = subprocess.Popen([HOST, out])
    time.sleep(1.5)
    th = wait_window("AutoTyperTestHost")
    check("接收端就绪", th != 0)
    if not th:
        host.kill()
        return 1

    gui = subprocess.Popen([DIST])
    main_h = wait_window("AutoTyperMainWnd")
    check("主窗口出现", main_h != 0)
    if not main_h:
        host.kill()
        gui.kill()
        return 1

    c = children(main_h)
    start_txt = text_of_btn = None
    u.SendMessageW(c.get(1006, 0), 0x000C, 0, ctypes.c_wchar_p("1"))
    set_text(c.get(1006, 0), "1")
    set_text(c.get(1004, 0), "5")
    time.sleep(0.3)
    set_text(c.get(1002, 0), "SINGLEF9OK")
    time.sleep(0.5)

    # 确认确实注册为单键 F9（无修饰）
    ok = u.RegisterHotKey(None, 9300, 0, 0x78)
    if ok:
        u.UnregisterHotKey(None, 9300)
    check("F9 已被 AutoTyper 独占注册", not ok)

    th_edit = u.FindWindowExW(th, 0, "Edit", None)
    check("接收端已激活", activate(th, th_edit))

    u.PostMessageW(main_h, 0x0312, 1, 0)   # WM_HOTKEY, id=1
    time.sleep(0.5)
    cnt = wait_window("AutoTyperCountWnd", timeout=3)
    check("倒计时浮窗出现", bool(cnt and u.IsWindowVisible(cnt)))
    time.sleep(7.0)

    host.terminate()
    time.sleep(1.0)
    gui.kill()
    time.sleep(0.3)

    ok = os.path.exists(out)
    check("输出文件生成", ok)
    if ok:
        d = open(out, "rb").read()
        got = d[2:].decode("utf-16-le") if d[:2] == b"\xff\xfe" else ""
        print(f"    收到: {got!r}")
        check("单键 F9 触发了注入", "SINGLEF9OK" in got)

    if os.path.exists(INI):
        os.remove(INI)

    print()
    if failures:
        print("结果: FAILED ->", failures)
        return 1
    print("结果: ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
