"""快捷键设置验收：默认值、配置持久化、捕获、冲突回滚、热键生效。"""
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
    # SendMessageW 故意不声明 argtypes：声明成 LPVOID 会让字符串指针被截断，
    # 跨进程 WM_SETTEXT / WM_GETTEXT 就无法工作。

    ("SendInput", wintypes.UINT, [wintypes.UINT, ctypes.c_void_p, ctypes.c_int]),
    ("SetForegroundWindow", wintypes.BOOL, [wintypes.HWND]),
    ("GetForegroundWindow", wintypes.HWND, []),
    ("ShowWindow", wintypes.BOOL, [wintypes.HWND, ctypes.c_int]),
    ("GetWindowTextW", wintypes.INT, [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]),
    ("IsWindowVisible", wintypes.BOOL, [wintypes.HWND]),
    ("IsWindow", wintypes.BOOL, [wintypes.HWND]),
    ("EnumChildWindows", wintypes.BOOL, [wintypes.HWND, ctypes.c_void_p, wintypes.LPARAM]),
    ("GetDlgCtrlID", wintypes.INT, [wintypes.HWND]),
    ("GetWindowTextLengthW", wintypes.INT, [wintypes.HWND]),
    ("SetWindowTextW", wintypes.BOOL, [wintypes.HWND, wintypes.LPCWSTR]),
]:
    f = getattr(u, fn)
    f.restype = res
    f.argtypes = args


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.c_void_p)]


class _U(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("pad", ctypes.c_byte * 32)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("u", _U)]


def tap_vk(vk):
    a = (INPUT * 2)()
    for i, fl in enumerate((0, 0x0002)):
        a[i].type = 1
        a[i].u.ki.wVk = vk
        a[i].u.ki.dwFlags = fl
    u.SendInput(2, ctypes.byref(a), ctypes.sizeof(INPUT))


class ForegroundGuard:
    """注入期间持续保持目标窗口在前台，避免微信等程序抢焦点。"""

    def __init__(self, hwnd):
        import threading
        self.hwnd = hwnd
        self._stop = threading.Event()
        self._t = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        while not self._stop.is_set():
            try:
                if u.GetForegroundWindow() != self.hwnd:
                    _tap_vk(0x12)          # 解除前台锁
                    u.ShowWindow(self.hwnd, 5)
                    u.SetForegroundWindow(self.hwnd)
            except Exception:
                pass
            self._stop.wait(0.15)

    def __enter__(self):
        self._t.start()
        return self

    def __exit__(self, *a):
        self._stop.set()
        self._t.join(timeout=2)


def force_foreground(hwnd):
    tap_vk(0x12)
    time.sleep(0.15)
    u.ShowWindow(hwnd, 9)
    ok = u.SetForegroundWindow(hwnd)
    time.sleep(0.6)
    return bool(ok)


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


def text_of(hwnd):
    """跨进程读文本必须用 WM_GETTEXT，GetWindowText 对别的进程的 EDIT 无效。"""
    if not hwnd:
        return ""
    n = u.GetWindowTextLengthW(hwnd)
    cap = max(n + 8, 512)
    b = ctypes.create_unicode_buffer(cap)
    u.SendMessageW(hwnd, 0x000D, cap, b)     # WM_GETTEXT
    return b.value


def set_text(hwnd, text):
    """跨进程写 EDIT 必须用 WM_SETTEXT；参数不能声明成 LPVOID。"""
    if not hwnd:
        return False
    return bool(u.SendMessageW(hwnd, 0x000C, 0, ctypes.c_wchar_p(text)))


def children(hwnd):
    """返回 {ctrl_id: hwnd}"""
    out = {}
    CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def cb(h, lp):
        cid = u.GetDlgCtrlID(h)
        if cid > 0:
            out[cid] = h
        return True

    u.EnumChildWindows(hwnd, CB(cb), 0)
    return out


def read_ini():
    if not os.path.exists(INI):
        return None
    raw = open(INI, "rb").read()
    if raw[:2] == b"\xff\xfe":
        return raw[2:].decode("utf-16-le", errors="replace")
    if raw[:2] == b"\xfe\xff":
        return raw[2:].decode("utf-16-be", errors="replace")
    if raw[:3] == b"\xef\xbb\xbf":
        return raw[3:].decode("utf-8", errors="replace")
    return raw.decode("gbk", errors="replace")


def clean_ini():
    if os.path.exists(INI):
        os.remove(INI)


def test_default_and_button_labels():
    print("\n=== 用例: 默认快捷键与按钮标签 ===")
    clean_ini()
    gui = subprocess.Popen([DIST])
    main = wait_window("AutoTyperMainWnd")
    check("主窗口出现", main != 0)
    if not main:
        gui.kill()
        return
    c = children(main)
    start_txt = text_of(c.get(1011, 0))
    stop_txt = text_of(c.get(1012, 0))
    set_btn = text_of(c.get(1014, 0))
    print(f"    开始按钮: {start_txt!r}")
    print(f"    停止按钮: {stop_txt!r}")
    print(f"    设置按钮: {set_btn!r}")
    check("开始按钮显示默认热键", "Ctrl+Shift+F9" in start_txt)
    check("停止按钮显示默认热键", "Ctrl+Shift+F10" in stop_txt)
    check("存在快捷键按钮", "快捷键" in set_btn)
    gui.kill()
    time.sleep(0.4)


def test_settings_window_capture():
    print("\n=== 用例: 设置窗口捕获新组合键并保存 ===")
    clean_ini()
    gui = subprocess.Popen([DIST])
    main = wait_window("AutoTyperMainWnd")
    check("主窗口出现", main != 0)
    if not main:
        gui.kill()
        return

    # 打开设置
    u.PostMessageW(main, 0x0111, 1014, 0)
    time.sleep(0.8)
    dlg = wait_window("AutoTyperSettingsWnd", timeout=5)
    check("设置窗口出现", dlg != 0)
    if not dlg:
        gui.kill()
        return

    c = children(dlg)
    box_start = c.get(2003, 0)
    box_stop = c.get(2004, 0)
    check("开始捕获框存在", box_start != 0)
    check("停止捕获框存在", box_stop != 0)
    if not box_start:
        gui.kill()
        return

    print(f"    初始开始框: {text_of(box_start)!r}")
    print(f"    初始停止框: {text_of(box_stop)!r}")

    # 聚焦开始框，按 Ctrl+Alt+K
    force_foreground(dlg)
    u.SendMessageW(box_start, 0x0007, 0, None)  # WM_SETFOCUS 模拟
    time.sleep(0.3)

    VK_CONTROL, VK_MENU, VK_SHIFT = 0x11, 0x12, 0x10
    # 用 keybd_event 的真实按下状态让 GetKeyState 生效
    ctypes.WinDLL("user32").keybd_event(VK_CONTROL, 0, 0, 0)
    ctypes.WinDLL("user32").keybd_event(VK_MENU, 0, 0, 0)
    time.sleep(0.1)
    u.SendMessageW(box_start, 0x0100, 0x4B, None)  # WM_KEYDOWN 'K'
    ctypes.WinDLL("user32").keybd_event(VK_MENU, 0, 2, 0)
    ctypes.WinDLL("user32").keybd_event(VK_CONTROL, 0, 2, 0)
    time.sleep(0.4)

    new_txt = text_of(box_start)
    print(f"    捕获后开始框: {new_txt!r}")
    check("捕获到 Ctrl+Alt+K", new_txt.replace(" ", "") == "Ctrl+Alt+K")

    # 点确定
    u.PostMessageW(dlg, 0x0111, 1, 0)  # IDOK
    time.sleep(0.9)
    check("设置窗口已关闭", not bool(u.IsWindow(dlg)))

    ini = read_ini()
    print(f"    INI: {ini!r}")
    check("配置文件已生成", ini is not None)
    check("INI 记录 start=Ctrl+Alt+K", ini is not None and "Ctrl+Alt+K" in ini)

    c2 = children(main)
    start_txt = text_of(c2.get(1011, 0))
    check("主界面按钮同步更新", "Ctrl+Alt+K" in start_txt, repr(start_txt))

    gui.kill()
    time.sleep(0.4)


def test_conflict_rejected():
    print("\n=== 用例: 开始/停止重复时拒绝保存 ===")
    clean_ini()
    gui = subprocess.Popen([DIST])
    main = wait_window("AutoTyperMainWnd")
    if not main:
        gui.kill()
        return

    u.PostMessageW(main, 0x0111, 1014, 0)
    time.sleep(0.8)
    dlg = wait_window("AutoTyperSettingsWnd", timeout=5)
    check("设置窗口出现", dlg != 0)
    if not dlg:
        gui.kill()
        return

    c = children(dlg)
    box_stop = c.get(2004, 0)
    force_foreground(dlg)
    u.SendMessageW(box_stop, 0x0007, 0, None)
    time.sleep(0.2)

    ctypes.WinDLL("user32").keybd_event(0x11, 0, 0, 0)
    ctypes.WinDLL("user32").keybd_event(0x10, 0, 0, 0)
    time.sleep(0.1)
    u.SendMessageW(box_stop, 0x0100, 0x71, None)  # F2
    ctypes.WinDLL("user32").keybd_event(0x10, 0, 2, 0)
    ctypes.WinDLL("user32").keybd_event(0x11, 0, 2, 0)
    time.sleep(0.3)

    # 把开始也设成 Ctrl+Shift+F2
    box_start = c.get(2003, 0)
    u.SendMessageW(box_start, 0x0007, 0, None)
    time.sleep(0.2)
    ctypes.WinDLL("user32").keybd_event(0x11, 0, 0, 0)
    ctypes.WinDLL("user32").keybd_event(0x10, 0, 0, 0)
    time.sleep(0.1)
    u.SendMessageW(box_start, 0x0100, 0x71, None)
    ctypes.WinDLL("user32").keybd_event(0x10, 0, 2, 0)
    ctypes.WinDLL("user32").keybd_event(0x11, 0, 2, 0)
    time.sleep(0.3)

    print(f"    开始框: {text_of(box_start)!r}")
    print(f"    停止框: {text_of(box_stop)!r}")

    # 点确定应弹出警告，窗口不关闭
    u.PostMessageW(dlg, 0x0111, 1, 0)
    time.sleep(1.0)

    # 关掉可能出现的警告框
    warn = wait_window("#32770", timeout=2)
    if warn:
        print("    出现警告对话框（预期）")
        u.PostMessageW(warn, 0x0010, 0, 0)  # WM_CLOSE
        time.sleep(0.5)

    check("冲突时设置窗口未关闭", bool(u.IsWindow(dlg)))
    check("冲突时未写入配置", not os.path.exists(INI))

    u.PostMessageW(dlg, 0x0010, 0, 0)
    time.sleep(0.4)
    gui.kill()
    time.sleep(0.4)


def test_ini_reload():
    print("\n=== 用例: ini 重启后生效 ===")
    os.makedirs(os.path.dirname(INI), exist_ok=True)
    with open(INI, "w", encoding="utf-16-le") as f:
        f.write("[hotkey]\r\nstart=Ctrl+Alt+J\r\nstop=Alt+F12\r\n")

    gui = subprocess.Popen([DIST])
    main = wait_window("AutoTyperMainWnd")
    check("主窗口出现", main != 0)
    if not main:
        gui.kill()
        clean_ini()
        return

    c = children(main)
    start_txt = text_of(c.get(1011, 0))
    stop_txt = text_of(c.get(1012, 0))
    print(f"    开始按钮: {start_txt!r}")
    print(f"    停止按钮: {stop_txt!r}")
    check("ini 的 start 生效", "Ctrl+Alt+J" in start_txt)
    check("ini 的 stop 生效", "Alt+F12" in stop_txt)

    gui.kill()
    time.sleep(0.4)
    clean_ini()


def test_hotkey_actually_triggers():
    print("\n=== 用例: 自定义热键真实触发输入 ===")
    clean_ini()
    out = os.path.join(ROOT, "obj", "hk.out")
    if os.path.exists(out):
        os.remove(out)

    host = subprocess.Popen([HOST, out])
    time.sleep(1.5)
    th = wait_window("AutoTyperTestHost")
    check("接收端就绪", th != 0)
    if not th:
        host.kill()
        return

    gui = subprocess.Popen([DIST])
    main = wait_window("AutoTyperMainWnd")
    check("主窗口出现", main != 0)
    if not main:
        host.kill()
        gui.kill()
        return

    # 自定义快捷键为 Ctrl+Alt+J
    u.PostMessageW(main, 0x0111, 1014, 0)
    time.sleep(0.7)
    dlg = wait_window("AutoTyperSettingsWnd", timeout=5)
    if dlg:
        dc = children(dlg)
        for box in (dc.get(2003, 0), dc.get(2004, 0)):
            u.SendMessageW(box, 0x0007, 0, None)
            time.sleep(0.1)
            ctypes.WinDLL("user32").keybd_event(0x11, 0, 0, 0)
            ctypes.WinDLL("user32").keybd_event(0x12, 0, 0, 0)
            time.sleep(0.05)
            if box == dc.get(2003, 0):
                u.SendMessageW(box, 0x0100, 0x4A, None)  # J
            else:
                u.SendMessageW(box, 0x0100, 0x4B, None)  # K
            ctypes.WinDLL("user32").keybd_event(0x12, 0, 2, 0)
            ctypes.WinDLL("user32").keybd_event(0x11, 0, 2, 0)
            time.sleep(0.2)
        u.PostMessageW(dlg, 0x0111, 1, 0)
        time.sleep(0.9)

    # 确认设置窗口已关闭、INI 已写入
    check("设置窗口已关闭", not bool(u.IsWindow(dlg)) if dlg else True)
    time.sleep(0.8)

    # 设置窗口关闭后再写入文本与参数
    c = children(main)
    set_text(c.get(1006, 0), "1")   # 倒计时
    set_text(c.get(1004, 0), "5")   # 间隔
    time.sleep(0.3)
    set_text(c.get(1002, 0), "HOTKEY-OK")
    time.sleep(0.5)

    start_txt = text_of(c.get(1011, 0))
    check("开始热键已改为 Ctrl+Alt+J", "Ctrl+Alt+J" in start_txt, repr(start_txt))
    body = text_of(c.get(1002, 0))
    print(f"    主编辑框内容: {body!r}  (len={u.GetWindowTextLengthW(c.get(1002, 0))})")
    check("测试文本已写入主编辑框", "HOTKEY-OK" in body)

    # 切到接收端（带前台锁解除），然后按 Ctrl+Alt+J 触发
    force_foreground(th)
    time.sleep(0.5)
    print(f"    前台窗口: {u.GetForegroundWindow()}  接收端: {th}")

    # 先验证热键确实被 AutoTyper 占用（最可靠的注册判据）
    u.RegisterHotKey.argtypes = [wintypes.HWND, ctypes.c_int, wintypes.UINT, wintypes.UINT]
    u.RegisterHotKey.restype = wintypes.BOOL
    u.UnregisterHotKey.argtypes = [wintypes.HWND, ctypes.c_int]
    u.UnregisterHotKey.restype = wintypes.BOOL

    taken = not u.RegisterHotKey(None, 9100, 0x0001 | 0x0002, 0x4A)
    if not taken:
        u.UnregisterHotKey(None, 9100)
    check("Ctrl+Alt+J 已被 AutoTyper 注册", taken)

    # 通过 WM_HOTKEY 直接触发（绕开模拟按键，避免前台锁/ALT 菜单干扰）
    WM_HOTKEY = 0x0312
    guard = ForegroundGuard(th)
    guard.__enter__()
    time.sleep(0.4)
    u.PostMessageW(main, WM_HOTKEY, 1, 0)

    time.sleep(0.3)
    th_edit = u.FindWindowExW(th, 0, "Edit", None)
    print(f"    接收端 edit hwnd={th_edit}")

    time.sleep(0.5)
    cnt = wait_window("AutoTyperCountWnd", timeout=2)
    vis = bool(u.IsWindowVisible(cnt)) if cnt else False
    print(f"    倒计时窗口 hwnd={cnt} visible={vis}")
    check("热键触发了倒计时", vis)
    time.sleep(6.0)

    # 读状态栏与编辑框内容，便于定位
    c2 = children(main)
    print(f"    主界面状态栏: {text_of(c2.get(1013, 0))!r}")
    print(f"    倒计时仍可见: {bool(u.IsWindowVisible(cnt)) if cnt else False}")
    print(f"    当前前台: {u.GetForegroundWindow()}  (host={th})")

    if th_edit:
        print(f"    终止前接收端内容: {text_of(th_edit)!r}")
    guard.__exit__()

    host.terminate()
    time.sleep(0.9)
    gui.kill()
    time.sleep(0.3)

    ok = os.path.exists(out)
    check("输出文件生成", ok)
    if ok:
        d = open(out, "rb").read()
        got = d[2:].decode("utf-16-le") if d[:2] == b"\xff\xfe" else ""
        print(f"    收到: {got!r}")
        check("自定义热键触发了输入", "HOTKEY-OK" in got)

    clean_ini()


def main():
    test_default_and_button_labels()
    test_settings_window_capture()
    test_conflict_rejected()
    test_ini_reload()
    test_hotkey_actually_triggers()

    print()
    if failures:
        print("结果: FAILED ->", failures)
        return 1
    print("结果: ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
