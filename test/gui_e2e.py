"""GUI 端到端验收：驱动真实 AutoTyper.exe 界面完成后台注入。"""
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
u.IsWindowVisible.argtypes = [wintypes.HWND]
u.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
u.IsIconic.argtypes = [wintypes.HWND]
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


def clear_modifiers():
    """释放所有可能残留的修饰键，避免跨用例串味。"""
    kb = ctypes.WinDLL("user32")
    for vk in (0x11, 0x12, 0x10, 0x5B, 0x5C):
        kb.keybd_event(vk, 0, 2, 0)      # KEYEVENTF_KEYUP
    time.sleep(0.25)


def activate(hwnd, focus_child=None):
    """用 AttachThreadInput 激活窗口，不注入任何按键。

    注入 ALT 解锁前台锁会和 AutoTyper 的 SendInput 抢事件队列，
    导致上下文串味，所以这里完全不碰键盘。
    """
    clear_modifiers()
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

WM_SETTEXT = 0x000C
WM_COMMAND = 0x0111
BN_CLICKED = 0

ROOT = r"C:\Users\notfa\autotyper"
DIST = os.path.join(ROOT, "dist", "AutoTyper.exe")
HOST = os.path.join(ROOT, "obj", "testhost.exe")
OUT = os.path.join(ROOT, "obj", "gui.out.txt")

failures = []


def text_of(hwnd):
    """跨进程读 EDIT 必须用 WM_GETTEXT。"""
    if not hwnd:
        return ""
    n = u.GetWindowTextLengthW(hwnd)
    cap = max(n + 8, 512)
    b = ctypes.create_unicode_buffer(cap)
    u.SendMessageW(hwnd, 0x000D, cap, b)   # WM_GETTEXT
    return b.value


def set_text(hwnd, text):
    """跨进程写 EDIT 必须用 WM_SETTEXT（SendMessageW 不声明 argtypes）。"""
    if not hwnd:
        return False
    return bool(u.SendMessageW(hwnd, 0x000C, 0, ctypes.c_wchar_p(text)))


def check(name, cond, extra=""):
    print(("  [PASS] " if cond else "  [FAIL] ") + name + (("  " + extra) if extra else ""))
    if not cond:
        failures.append(name)


def wait_window(cls, title=None, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        h = u.FindWindowW(cls, title)
        if h:
            return h
        time.sleep(0.05)
    return 0


def main():
    if os.path.exists(OUT):
        os.remove(OUT)

    host = subprocess.Popen([HOST, OUT])
    time.sleep(1.5)

    gui = subprocess.Popen([DIST])
    main_hwnd = wait_window("AutoTyperMainWnd")
    print("main hwnd:", main_hwnd)
    if not main_hwnd:
        print("GUI 未启动")
        host.kill()
        return 1

    # 找到编辑框并写入测试文本
    edit = u.FindWindowExW(main_hwnd, 0, "Edit", None)
    print("edit hwnd:", edit)
    check("编辑框存在", edit != 0)

    payload = "GUI-FLOW-OK" + chr(13)+chr(10) + "second line" + chr(13)+chr(10) + "third 123 ABC" + chr(13)+chr(10) + "GUI END"
    set_text(edit, payload)
    time.sleep(0.5)
    print(f"    主编辑框内容: {text_of(edit)!r} len={u.GetWindowTextLengthW(edit)}")

    # 先切到接收端，再点开始（AutoTyper 会在此后最小化自己）
    th = wait_window("AutoTyperTestHost", timeout=5)
    check("接收端窗口存在", th != 0)
    th_edit = u.FindWindowExW(th, 0, "Edit", None) if th else 0
    if th:
        check("目标窗口已置前", activate(th, th_edit))

    # 点击“开始输入”
    u.PostMessageW(main_hwnd, WM_COMMAND, 1011, 0)
    time.sleep(0.6)

    count = wait_window("AutoTyperCountWnd", timeout=3)
    check("倒计时浮窗出现", count != 0)
    if count:
        check("倒计时浮窗可见", bool(u.IsWindowVisible(count)))
        check("主窗口已最小化", bool(u.IsIconic(main_hwnd)))

    # 倒计时结束后 AutoTyper 才开始注入，此时再确认一次焦点
    for i in range(7):
        fg = u.GetForegroundWindow()
        print(f"    t={i}s 前台={fg} host={th} {'OK' if fg==th else '被抢'}")
        if fg != th:
            activate(th, u.FindWindowExW(th, 0, "Edit", None))
        time.sleep(1.0)
    time.sleep(0.2)
    if th and u.GetForegroundWindow() != th:
        print(f"    焦点被抢，重新激活 (当前={u.GetForegroundWindow()})")
        activate(th, u.FindWindowExW(th, 0, "Edit", None))

    check("倒计时浮窗已隐藏", not bool(u.IsWindowVisible(count)) if count else True)
    check("主窗口已恢复", bool(u.IsWindowVisible(main_hwnd)))
    if th_edit:
        print(f"    终止前接收端内容: {text_of(th_edit)!r}")
    print(f"    终止前主编辑框: {text_of(edit)!r}")

    gui.kill()
    time.sleep(0.8)
    try:
        host.terminate()
        host.wait(timeout=5)
    except Exception:
        host.kill()
    time.sleep(0.5)

    check("输出文件已生成", os.path.exists(OUT))
    if os.path.exists(OUT):
        raw = open(OUT, "rb").read()
        got = raw[2:].decode("utf-16-le") if raw[:2] == b"\xff\xfe" else raw.decode("utf-8")
        expect = payload
        ok = got.replace("\r\n", "\n") == expect.replace("\r\n", "\n")
        check("注入内容完全一致", ok)
        if not ok:
            print("     got   =", repr(got))
            print("     expect=", repr(expect))

    print()
    if failures:
        print("结果: FAILED ->", failures)
        return 1
    print("结果: ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
