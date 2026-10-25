"""抓取屏幕上某个窗口可见区域的真实像素（含 DWM 合成效果）。"""
import ctypes
import sys
from ctypes import wintypes

from PIL import Image

u = ctypes.WinDLL("user32", use_last_error=True)
gdi = ctypes.WinDLL("gdi32", use_last_error=True)


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


u.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
u.FindWindowW.restype = wintypes.HWND
u.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
u.GetWindowRect.restype = wintypes.BOOL
u.GetDC.argtypes = [wintypes.HWND]
u.GetDC.restype = wintypes.HDC
u.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
u.SetForegroundWindow.argtypes = [wintypes.HWND]
u.SetForegroundWindow.restype = wintypes.BOOL
u.BringWindowToTop.argtypes = [wintypes.HWND]

gdi.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi.CreateCompatibleDC.restype = wintypes.HDC
gdi.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi.CreateCompatibleBitmap.restype = wintypes.HBITMAP
gdi.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi.SelectObject.restype = wintypes.HGDIOBJ
gdi.BitBlt.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                       ctypes.c_int, wintypes.HDC, ctypes.c_int, ctypes.c_int, wintypes.DWORD]
gdi.BitBlt.restype = wintypes.BOOL
gdi.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT,
                          ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]
gdi.GetDIBits.restype = ctypes.c_int
gdi.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi.DeleteDC.argtypes = [wintypes.HDC]


def capture_screen_rect(hwnd, path):
    r = wintypes.RECT()
    u.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top

    hdc_screen = u.GetDC(None)
    mdc = gdi.CreateCompatibleDC(hdc_screen)
    bmp = gdi.CreateCompatibleBitmap(hdc_screen, w, h)
    old = gdi.SelectObject(mdc, bmp)

    gdi.BitBlt(mdc, 0, 0, w, h, hdc_screen, r.left, r.top, 0x00CC0020)  # SRCCOPY

    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(bi)
    bi.biWidth = w
    bi.biHeight = -h
    bi.biPlanes = 1
    bi.biBitCount = 32
    bi.biCompression = 0

    buf = ctypes.create_string_buffer(w * h * 4)
    gdi.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)

    Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB").save(path)

    gdi.SelectObject(mdc, old)
    gdi.DeleteObject(bmp)
    gdi.DeleteDC(mdc)
    u.ReleaseDC(None, hdc_screen)
    return w, h


if __name__ == "__main__":
    cls, out = sys.argv[1], sys.argv[2]
    hwnd = u.FindWindowW(cls, None)
    if not hwnd:
        print("NOT_FOUND")
        sys.exit(1)
    u.BringWindowToTop(hwnd)
    u.SetForegroundWindow(hwnd)
    import time
    time.sleep(0.8)
    w, h = capture_screen_rect(hwnd, out)
    print(f"OK {out} {w}x{h}")
