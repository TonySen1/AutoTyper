# AutoTyper

在**无法复制粘贴**的环境里自动逐字键入长文本。适用于虚拟机（VMware / VirtualBox / Hyper-V）、远程桌面（RDP）、带远控的机器等剪贴板不可用的场景。

单文件可执行程序，无运行时依赖，下载即用。

## 特性

- **不走剪贴板**：用 `SendInput` + `KEYEVENTF_UNICODE` 逐字符注入，虚拟机/远程桌面内同样有效
- **完整 Unicode 支持**：中文、Emoji（含代理对）、全角符号、换行、制表符
- **单键或组合键热键**：默认 `F9` 开始 / `F10` 停止，可自定义为 `F8`、`Alt+F8`、`Ctrl+Shift+F9` 等
- **倒计时浮窗**：留出切换目标窗口的时间，浮窗置顶且不抢焦点
- **可调速度**：每字符间隔可设为 0–1000 ms，适配不同程序的输入处理能力
- **文件载入**：支持 UTF-8（含 BOM）、UTF-16 LE/BE、GBK(ANSI) 自动识别
- **深色界面**：亚克力背景（Win10 1803+），对话框全自绘，圆角控件
- **命令行模式**：可脚本化调用

## 使用

### 图形界面

1. 双击 `AutoTyper.exe`
2. 粘贴文本，或点「载入文件…」
3. 点「开始输入」或按 `F9`
4. 在倒计时内切换到目标窗口
5. 程序自动逐字键入；需要中断时按 `F10`

### 命令行

```bat
:: 直接键入指定文本（\n \t \\ 支持转义）
AutoTyper.exe -c "Hello 世界\n第二行" -d 3 -i 8

:: 从文件键入
AutoTyper.exe -f content.txt -d 5 -i 10

:: 发送前切换到英文键盘布局（可避免输入法吞字）
AutoTyper.exe -f content.txt --english
```

| 参数 | 说明 |
|---|---|
| `-c, --text` | 要键入的文本，支持 `\n` `\r` `\t` `\\` 转义 |
| `-f, --file` | 从文件读取 |
| `-d, --delay` | 倒计时秒数，默认 3，范围 0–60 |
| `-i, --interval` | 每字符间隔毫秒，默认 8，范围 0–1000 |
| `--english` | 发送前切换英文键盘布局 |
| `--selftest` | 运行内置自测 |
| `-g, --gui` | 强制打开图形界面 |

## 快捷键

默认 `F9` 开始、`F10` 停止。点主界面「快捷键…」可改：

- **单键**：`F1`–`F24`、`Pause`、`PrtSc`、`ScrollLock`、媒体键
- **组合键**：`Alt+F8`、`Ctrl+F10`、`Ctrl+Shift+F9`、`Win+Alt+K` 等

字母、数字、空格等字符键**必须**搭配修饰键——否则注册为全局热键后会挡住正常打字。

配置保存在 exe 同目录的 `AutoTyper.ini`（目录不可写时退回 `%APPDATA%\AutoTyper\`）。

## 构建

需要 Visual Studio 2022 Build Tools（含 C++ 工作负载）。

```bat
build.bat
```

产物在 `dist\AutoTyper.exe`。

依赖仅限系统 DLL：`USER32` `GDI32` `MSIMG32` `COMDLG32` `COMCTL32` `SHELL32` `KERNEL32`，静态链接 CRT，无需安装 VC++ 运行库。

## 兼容性

- Windows 10 / 11（x64）
- Windows 7 SP1 可用，但无亚克力背景，自动降级为纯色
- 32 位系统不支持

## 已知限制

- 目标是**管理员权限**窗口时，需右键「以管理员身份运行」本程序，否则 `SendInput` 会被 UIPI 拦截（程序会提示）
- 目标窗口若停留在中文输入法状态，快速注入可能被吞字或产生联想字，建议勾选「切换英文布局」或增大间隔
- 客户区背景为不透明模拟玻璃效果，非真透明（GDI 无逐像素 alpha，真亚克力需 DirectComposition）

## 许可

MIT
