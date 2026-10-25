"""快捷键形态验收：单键 F 系列、双键 Alt/Ctrl 组合、非法单键拒绝、持久化。"""
import ctypes
import os
import subprocess
import sys
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
    ("IsWindow", wintypes.BOOL, [wintypes.HWND]),
    ("GetDlgCtrlID", wintypes.INT, [wintypes.HWND]),
    ("EnumChildWindows", wintypes.BOOL, [wintypes.HWND, ctypes.c_void_p, wintypes.LPARAM]),
    ("RegisterHotKey", wintypes.BOOL, [wintypes.HWND, ctypes.c_int, wintypes.UINT, wintypes.UINT]),
    ("UnregisterHotKey", wintypes.BOOL, [wintypes.HWND, ctypes.c_int]),
]:
    f = getattr(u, fn)
    f.restype = res
    if args is not None:
        f.argtypes = args

ROOT = r"C:\Users\notfa\autotyper"
DIST = os.path.join(ROOT, "dist", "AutoTyper.exe")
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


def text_of(hwnd):
    if not hwnd:
        return ""
    n = u.GetWindowTextLengthW(hwnd)
    cap = max(n + 8, 512)
    b = ctypes.create_unicode_buffer(cap)
    u.SendMessageW(hwnd, 0x000D, cap, b)   # WM_GETTEXT
    return b.value


def set_text(hwnd, text):
    if not hwnd:
        return False
    return bool(u.SendMessageW(hwnd, 0x000C, 0, ctypes.c_wchar_p(text)))


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


MOD_ALT, MOD_CTRL, MOD_SHIFT, MOD_WIN = 0x0001, 0x0002, 0x0004, 0x0008


def is_hotkey_taken(mods, vk, probe_id=9200):
    """返回 True 表示该系统热键已被别的程序占用。"""
    ok = u.RegisterHotKey(None, probe_id, mods, vk)
    if ok:
        u.UnregisterHotKey(None, probe_id)
        return False
    return True


def capture(box, vk, mods=(0x11, 0x12, 0x10, 0x5B)):
    """向捕获框模拟按下组合键。mods 是要按住的修饰键列表。"""
    u.SendMessageW(box, 0x0007, 0, None)      # WM_SETFOCUS 触发捕获模式
    time.sleep(0.15)
    kb = ctypes.WinDLL("user32")
    held = [m for m in mods if m in (0x11, 0x12, 0x10, 0x5B)]
    for m in held:
        kb.keybd_event(m, 0, 0, 0)
    time.sleep(0.06)
    u.SendMessageW(box, 0x0100, vk, None)    # WM_KEYDOWN
    for m in reversed(held):
        kb.keybd_event(m, 0, 2, 0)
    time.sleep(0.25)


def open_settings(main):
    u.PostMessageW(main, 0x0111, 1014, 0)
    time.sleep(0.8)
    return wait_window("AutoTyperSettingsWnd", timeout=5)


def ok_settings(dlg):
    u.PostMessageW(dlg, 0x0111, 1, 0)   # IDOK
    time.sleep(1.0)


def kill_warn():
    w = wait_window("#32770", timeout=1.5)
    if w:
        u.PostMessageW(w, 0x0010, 0, 0)
        time.sleep(0.4)
        return True
    return False


def fresh_gui():
    if os.path.exists(INI):
        os.remove(INI)
    p = subprocess.Popen([DIST])
    main = wait_window("AutoTyperMainWnd")
    return p, main


def test_single_function_keys():
    print("\n=== 用例: 单键 F8 / F9 / F10 ===")
    p, main = fresh_gui()
    if not main:
        p.kill()
        return

    for name, vk, label in [("F8", 0x77, "F8"), ("F9", 0x78, "F9"), ("F10", 0x79, "F10")]:
        dlg = open_settings(main)
        if not dlg:
            check(f"设置窗口打开 ({name})", False)
            continue
        dc = children(dlg)
        capture(dc.get(2003, 0), vk, mods=())     # 不按任何修饰键
        got = text_of(dc.get(2003, 0))
        check(f"单键 {name} 被接受", got.replace(" ", "") == label, repr(got))
        u.PostMessageW(dlg, 0x0010, 0, 0)         # 关闭，不保存
        time.sleep(0.5)

    p.kill()
    time.sleep(0.4)
    if os.path.exists(INI):
        os.remove(INI)


def test_two_key_combos():
    print("\n=== 用例: 双键 Alt+F8 / Alt+F9 / Ctrl+F10 ===")
    p, main = fresh_gui()
    if not main:
        p.kill()
        return

    cases = [
        ("Alt+F8",  0x77, (0x12,)),
        ("Alt+F9",  0x78, (0x12,)),
        ("Ctrl+F10", 0x79, (0x11,)),
        ("Shift+F7", 0x76, (0x10,)),
    ]
    for label, vk, held in cases:
        dlg = open_settings(main)
        if not dlg:
            check(f"设置窗口打开 ({label})", False)
            continue
        dc = children(dlg)
        capture(dc.get(2003, 0), vk, mods=held)
        got = text_of(dc.get(2003, 0))
        check(f"双键 {label} 被接受", got.replace(" ", "") == label, repr(got))
        u.PostMessageW(dlg, 0x0010, 0, 0)
        time.sleep(0.5)

    p.kill()
    time.sleep(0.4)
    if os.path.exists(INI):
        os.remove(INI)


def test_character_key_rejected():
    print("\n=== 用例: 字母/数字单键应被拒绝 ===")
    p, main = fresh_gui()
    if not main:
        p.kill()
        return

    dlg = open_settings(main)
    if not dlg:
        check("设置窗口打开", False)
        p.kill()
        return
    dc = children(dlg)
    box = dc.get(2003, 0)

    for label, vk in [("A", 0x41), ("1", 0x31), ("Space", 0x20)]:
        capture(box, vk, mods=())
        got = text_of(box)
        # 被拒绝时框里显示的是提示语，而不是按键名
        rejected = ("单键仅支持" in got) and (got.strip() != label)
        check(f"单键 {label} 被拒绝", rejected, repr(got))

    u.PostMessageW(dlg, 0x0010, 0, 0)
    time.sleep(0.5)
    p.kill()
    time.sleep(0.4)
    if os.path.exists(INI):
        os.remove(INI)


def test_single_key_persist_and_register():
    print("\n=== 用例: 单键保存后真实注册并生效 ===")
    p, main = fresh_gui()
    if not main:
        p.kill()
        return

    dlg = open_settings(main)
    if not dlg:
        check("设置窗口打开", False)
        p.kill()
        return
    dc = children(dlg)
    capture(dc.get(2003, 0), 0x78, mods=())      # F9
    capture(dc.get(2004, 0), 0x79, mods=())      # F10
    ok_settings(dlg)
    kill_warn()

    c = children(main)
    start_txt = text_of(c.get(1011, 0))
    stop_txt = text_of(c.get(1012, 0))
    print(f"    开始按钮: {start_txt!r}")
    print(f"    停止按钮: {stop_txt!r}")
    check("开始按钮显示 F9", "F9" in start_txt and "+" not in start_txt)
    check("停止按钮显示 F10", "F10" in stop_txt and "+" not in stop_txt)

    ini = None
    if os.path.exists(INI):
        raw = open(INI, "rb").read()
        ini = raw[2:].decode("utf-16-le", errors="replace") if raw[:2] == b"\xff\xfe" else raw.decode("gbk", errors="replace")
    print(f"    INI: {ini!r}")
    check("INI 记录 start=F9", ini is not None and "start=F9" in ini.replace("\r\n", "\n"))
    check("INI 记录 stop=F10", ini is not None and "stop=F10" in ini.replace("\r\n", "\n"))

    # 系统层面确认已被 AutoTyper 占用（我们自己再注册应失败）
    check("F9 已被 AutoTyper 注册", is_hotkey_taken(0, 0x78))
    check("F10 已被 AutoTyper 注册", is_hotkey_taken(0, 0x79))

    p.kill()
    time.sleep(0.5)


def test_reload_from_ini():
    print("\n=== 用例: 重启后单键配置生效 ===")
    if os.path.exists(INI):
        os.remove(INI)
    with open(INI, "w", encoding="utf-16-le") as f:
        f.write("[hotkey]\r\nstart=F8\r\nstop=Alt+F9\r\n")

    p = subprocess.Popen([DIST])
    main = wait_window("AutoTyperMainWnd")
    check("主窗口出现", main != 0)
    if not main:
        p.kill()
        return
    c = children(main)
    start_txt = text_of(c.get(1011, 0))
    stop_txt = text_of(c.get(1012, 0))
    print(f"    开始按钮: {start_txt!r}")
    print(f"    停止按钮: {stop_txt!r}")
    check("单键 F8 生效", "F8" in start_txt and "+" not in start_txt)
    check("组合 Alt+F9 生效", "Alt+F9" in stop_txt.replace(" ", ""))
    p.kill()
    time.sleep(0.4)
    if os.path.exists(INI):
        os.remove(INI)


def main():
    test_single_function_keys()
    test_two_key_combos()
    test_character_key_rejected()
    test_single_key_persist_and_register()
    test_reload_from_ini()

    print()
    if failures:
        print("结果: FAILED ->", failures)
        return 1
    print("结果: ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
