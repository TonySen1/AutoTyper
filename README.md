# AutoTyper

在**无法复制粘贴**的环境里自动逐字键入长文本。适用于虚拟机（VMware / VirtualBox / Hyper-V）、远程桌面（RDP）、带远控的机器等剪贴板不可用的场景。

单文件可执行程序，无运行时依赖，下载即用。

## 特性

- **不走剪贴板**：用 `SendInput` + `KEYEVENTF_UNICODE` 注入，虚拟机/远程桌面内同样有效
- **批量注入**：一批字符合成为单个 `SendInput` 调用；默认配置下仍逐字，行为不变
- **完整 Unicode 支持**：中文、Emoji（含代理对，批边界不会拆开）、全角符号、换行、制表符
- **单键或组合键热键**：默认 `F9` 开始 / `F10` 停止，可自定义为 `F8`、`Alt+F8`、`Ctrl+Shift+F9` 等
- **倒计时浮窗**：留出切换目标窗口的时间，浮窗置顶且不抢焦点
- **可调速度**：每字符间隔可设为 0–1000 ms，适配不同程序的输入处理能力
- **文件载入**：支持 UTF-8（含 BOM）、UTF-16 LE/BE、GBK(ANSI) 自动识别
- **浅色界面**：白色主题，对话框全自绘，圆角控件；深色配色保留在代码中，可一行切回
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
| `-i, --interval` | 每字符间隔毫秒，默认读取 ini（缺省 8），范围 0–1000 |
| `--batch N` | 每批注入的字符数，`0` = 自动。默认沿用 ini |
| `--english` | 发送前切换英文键盘布局 |
| `--humanize` | 启用拟人化节奏（默认关闭） |
| `--selftest` | 运行内置自测 |
| `-g, --gui` | 强制打开图形界面 |

## 配置文件

`AutoTyper.ini` 保持向后兼容：旧的 `[hotkey]` 段无需任何改动。高级选项放在可选的 `[typing]` 段，
**不写就等于 v1.0.0 的行为**。

```ini
[hotkey]
start=F9
stop=F10

[typing]
; 每批字符数：0=自动（interval>=4 时自动退化为逐字），1..256
batch=0
; 未显式传 -i 时使用的默认间隔
interval=8
; 是否预检前台窗口完整性级别（只提示，不阻止）
check_uipi=1

; ---- 拟人化（默认全关）----
humanize=0
humanize_mean_ms=45
humanize_sigma_ms=18
humanize_pause_punct=1
humanize_pause_newline=1
humanize_pause_word=1
humanize_fatigue=1
; 误字概率，百分比 0..5
humanize_typo_percent=0
```

> `batch` 的自动策略：`interval = 0` → 64 字符/批；`interval <= 3` → 8 字符/批；其余保持逐字。
> 这样默认 8 ms 配置的行为与 v1.0.0 完全一致，只有显式调快或用 `--batch` 时才改变发送节奏。

## 快捷键

默认 `F9` 开始、`F10` 停止。点主界面「快捷键…」可改：

- **单键**：`F1`–`F24`、`Pause`、`PrtSc`、`ScrollLock`、媒体键
- **组合键**：`Alt+F8`、`Ctrl+F10`、`Ctrl+Shift+F9`、`Win+Alt+K` 等

字母、数字、空格等字符键**必须**搭配修饰键——否则注册为全局热键后会挡住正常打字。

配置保存在 exe 同目录的 `AutoTyper.ini`（目录不可写时退回 `%APPDATA%\AutoTyper\`）。

设置窗口里的「拟人化节奏」复选框对应 `[typing] humanize`。

## 构建

需要 Visual Studio 2022 Build Tools（含 C++ 工作负载）。

```bat
build.bat
```

产物在 `dist\AutoTyper.exe`，同时构建测试宿主与单元测试。

### 测试

```bat
obj\typing_test.exe --verbose
```

测试自建宿主窗口，用 `SendInput` 注入后经 `WM_GETTEXT` 回读比对，覆盖 ASCII、中文、
Emoji 代理对、CRLF/LF、Tab、批量长文本、拟人化模式，以及 UTF-8/UTF-16/GBK 解码与转义。
需要交互式桌面会话（注入依赖前台窗口）。

依赖仅限系统 DLL：`USER32` `GDI32` `MSIMG32` `COMDLG32` `COMCTL32` `SHELL32` `KERNEL32`，静态链接 CRT，无需安装 VC++ 运行库。

## 兼容性

- Windows 10 / 11（x64）
- Windows 7 SP1 可用（浅色主题为纯色背景，无额外依赖）
- 32 位系统不支持

## 已知限制

- 目标是**管理员权限**窗口时，`SendInput` 会被 UIPI 拦截。程序启动与每次发送前都会读取双方
  完整性级别，检测到目标更高时提示，并询问是否以管理员身份重启（不会默认提权）
- 目标窗口若停留在中文输入法状态，快速注入仍可能被吞字或产生联想字。勾选「切换英文布局」会
  请求目标线程切到 `00000409` 并在结束后恢复；这是尽力而为的降级策略（见下）
- **批量注入的收益受限于目标程序**：实测 8000 字符耗时与逐字发送接近（约 0.66 ms/字符），
  瓶颈在目标窗口逐条处理 `WM_CHAR`，而不是 `SendInput` 调用次数。批量主要降低系统调用与
  调度开销，不改变目标程序的消化速度
- 背景为不透明纯色 + 顶部极淡渐变，非真透明。GDI 无逐像素 alpha；浅色下的 DWM 客户区
  亚克力实测会把整块客户区合成成纯白、盖住子控件，故 `1.0.1` 起浅色主题不使用该效果。
  深色配色仍保留亚克力路径（`g_dark = true` 即恢复）

## 许可

MIT
