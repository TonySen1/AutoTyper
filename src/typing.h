// ============================================================
//  AutoTyper 注入引擎
//  - 批量构建 INPUT 数组，减少 SendInput 调用次数
//  - 代理对不跨批拆分
//  - 保留 interval 语义（批间等待 = interval × 批内码点数）
//  - 可选拟人化节奏（默认关闭）
//  - UIPI 完整性级别预检
//  - 英文键盘布局请求/恢复（尽力而为，可降级）
// ============================================================
#pragma once

#include <windows.h>

#include <atomic>
#include <string>
#include <vector>

namespace at {

// ---------------- 拟人化（高级可选，默认关闭） ----------------
struct HumanizeOptions {
    bool     enabled          = false;
    double   meanMs           = 45.0;   // 高斯均值（额外停顿）
    double   sigmaMs          = 18.0;   // 高斯标准差
    int      minMs            = 8;      // 单字符下限
    int      maxMs            = 400;    // 单字符上限
    bool     pauseAfterPunct  = true;   // 标点后停顿
    bool     pauseAfterNewline= true;   // 换行后停顿
    bool     pauseAfterLongWord = true; // 长单词后停顿
    bool     fatigue          = true;   // 疲劳度模拟
    bool     fatigueGrows     = true;   // 越到后面越慢
    double   typoRate         = 0.0;    // 误字概率 [0, 0.05]，默认关闭
    int      typoFixDelayMs   = 120;    // 误字到回删的间隔
};

// ---------------- 注入选项 ----------------
struct TypingOptions {
    int      intervalMs    = 8;      // 与 v1.0.0 一致：每码点等待毫秒
    int      batchChars    = 0;      // 0 = 自动；>0 强制批大小
    bool     forceEnglish  = false;  // 请求目标窗口切换英文布局
    bool     restoreLayout = false;  // 注入后恢复原布局
    bool     checkUipi     = true;   // 预检前台窗口完整性级别
    HumanizeOptions humanize;
};

enum class TypingStatus {
    Ok = 0,
    Blocked,          // SendInput 被系统拦截（UIPI / 安全桌面）
    TargetHigherIL,   // 目标窗口完整性级别更高（预检结果）
    Empty,            // 文本为空
};

struct TypingReport {
    TypingStatus status           = TypingStatus::Ok;
    int          totalCodePoints  = 0;   // 输入文本的码点数（代理对计 1）
    int          injectedPoints   = 0;   // 成功注入的 wchar 单元数（与旧 interval 口径一致）
    int          blockedEvents    = 0;
    int          batches          = 0;
    bool         layoutChanged    = false;
    bool         layoutRestored   = false;
    unsigned     selfIntegrity    = 0;
    unsigned     targetIntegrity  = 0;
    bool         targetHigher     = false;
};

// ---------------- 注入单元（可独立测试） ----------------
struct TypingUnit {
    enum Kind { Unicode, VirtualKey } kind = Unicode;
    wchar_t ch = 0;          // kind == Unicode
    WORD    vk = 0;          // kind == VirtualKey
    bool    highSurrogate = false;  // 代理对的高位
    bool    lowSurrogate  = false;  // 代理对的低位
};

// 把文本转成注入单元：CRLF 合并为单个回车，\r / \n 都是回车，\t 是 Tab。
std::vector<TypingUnit> BuildUnits(const std::wstring& text);

// 自动批大小（0 = 自动时使用）
int AutoBatchChars(const TypingOptions& opts);

// 前台窗口与自身的完整性级别预检
struct IntegrityInfo {
    unsigned self         = 0;
    unsigned target       = 0;
    bool     targetHigher = false;
    HWND     targetWindow = nullptr;
};
IntegrityInfo ProbeForegroundIntegrity();

// 主入口：向当前前台窗口注入文本。
// stop 可为 nullptr；非空时在每个批次之间检查并提前退出。
TypingReport TypeText(const std::wstring& text, const TypingOptions& opts,
                      const std::atomic<bool>* stop);

// 单个码点的 Unicode 注入（供自测与特殊路径复用）
bool SendUnicodeChar(wchar_t ch);
bool SendVkKey(WORD vk);

} // namespace at
