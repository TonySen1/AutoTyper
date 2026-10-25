"""快捷键设置窗口：捕获后输入框显示验收。

验证两个字段分别按 F9 / F10 后，框里显示的是新值（而不是回退成默认值），
并在切换焦点后仍然保持新值。
"""
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
    ("GetWindowTextLengthW", wintypes.INT, [wintypes.HWND]),
    ("IsWindow", wintypes.BOOL, [wintypes.HWND]),
    ("GetDlgCtrlID", wintypes.INT, [wintypes.HWND]),
    ("EnumChildWindows", wintypes.BOOL, [wintypes.HWND, ctypes.c_void_p, wintypes.LPARAM]),
    ("SetFocus", wintypes.HWND, [wintypes.HWND]),
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


def press_key(box, vk):
    """模拟在捕获框里按下一个键（不带修饰键）。"""
    u.SendMessageW(box, 0x0007, 0, None)     # WM_SETFOCUS -> 进入捕获态
    time.sleep(0.15)
    u.SendMessageW(box, 0x0100, vk, None)    # WM_KEYDOWN
    time.sleep(0.25)


def main():
    print("=== 截图问题复现：F9 / F10 捕获后的显示 ===")
    if os.path.exists(INI):
        os.remove(INI)

    gui = subprocess.Popen([DIST])
    main_h = wait_window("AutoTyperMainWnd")
    check("主窗口出现", main_h != 0)
    if not main_h:
        gui.kill()
        return 1

    # 打开快捷键设置
    u.PostMessageW(main_h, 0x0111, 1014, 0)   # IDC_BTN_SETTINGS
    time.sleep(0.9)
    dlg = wait_window("AutoTyperSettingsWnd", 5)
    check("设置窗口出现", dlg != 0)
    if not dlg:
        gui.kill()
        return 1

    c = children(dlg)
    box_start = c.get(2003, 0)
    box_stop = c.get(2004, 0)
    check("开始捕获框存在", box_start != 0)
    check("停止捕获框存在", box_stop != 0)
    if not box_start or not box_stop:
        gui.kill()
        return 1

    print(f"    初始 开始={text_of(box_start)!r}  停止={text_of(box_stop)!r}")

    # ① 开始框按 F9
    press_key(box_start, 0x78)
    got = text_of(box_start)
    check("开始框按下 F9 后显示 F9", got.replace(" ", "") == "F9", repr(got))

    # ② 停止框按 F10 —— 这里之前会让开始框回退成默认值
    press_key(box_stop, 0x79)
    got_stop = text_of(box_stop)
    check("停止框按下 F10 后显示 F10", got_stop.replace(" ", "") == "F10", repr(got_stop))

    got_start_after = text_of(box_start)
    check("切到停止框后，开始框仍显示 F9（未回退默认）",
          got_start_after.replace(" ", "") == "F9", repr(got_start_after))

    # ③ 两个框之间来回切换焦点（真实点击会成对发 KILLFOCUS / SETFOCUS）
    def switch_focus(old, new):
        u.SendMessageW(old, 0x0008, 0, None)   # WM_KILLFOCUS
        time.sleep(0.15)
        u.SendMessageW(new, 0x0007, 0, None)   # WM_SETFOCUS
        time.sleep(0.2)

    switch_focus(box_stop, box_start)      # 停止 -> 开始
    switch_focus(box_start, box_stop)      # 开始 -> 停止

    # 此刻焦点在停止框：它显示占位符是正确的（正在等待新按键）
    check("获焦的停止框显示占位符",
          "按下新的组合键" in text_of(box_stop), repr(text_of(box_stop)))
    # 失焦的开始框必须显示它自己的编辑值
    check("失焦的开始框仍显示 F9",
          text_of(box_start).replace(" ", "") == "F9", repr(text_of(box_start)))

    # 把焦点让回开始框，停止框失焦后应显示 F10
    switch_focus(box_stop, box_start)
    check("失焦的停止框仍显示 F10",
          text_of(box_stop).replace(" ", "") == "F10", repr(text_of(box_stop)))

    # ④ 点确定，确认真的保存
    u.PostMessageW(dlg, 0x0111, 1, 0)   # IDOK
    time.sleep(1.0)

    ini = None
    if os.path.exists(INI):
        raw = open(INI, "rb").read()
        ini = raw[2:].decode("utf-16-le", errors="replace") if raw[:2] == b"\xff\xfe" else raw.decode("gbk", errors="replace")
    print(f"    INI: {ini!r}")
    check("配置已保存 start=F9", ini is not None and "start=F9" in ini.replace("\r\n", "\n"))
    check("配置已保存 stop=F10", ini is not None and "stop=F10" in ini.replace("\r\n", "\n"))

    # ⑤ 主界面按钮同步
    c2 = children(main_h)
    start_txt = text_of(c2.get(1011, 0))
    stop_txt = text_of(c2.get(1012, 0))
    print(f"    主界面 开始={start_txt!r}  停止={stop_txt!r}")
    check("主界面开始按钮显示 F9", "F9" in start_txt)
    check("主界面停止按钮显示 F10", "F10" in stop_txt)

    gui.kill()
    time.sleep(0.4)
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
