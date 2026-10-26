#include "typing.h"
#include "textfmt.h"

#include <mmsystem.h>

#include <cmath>
#include <cstring>
#include <cwctype>
#include <random>

#pragma comment(lib, "winmm.lib")

namespace at {

// ============================================================
//  基础注入
// ============================================================
bool SendUnicodeChar(wchar_t ch)
{
    INPUT in[2];
    ZeroMemory(in, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = 0;
    in[0].ki.wScan = ch;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    return SendInput(2, in, sizeof(INPUT)) == 2;
}

bool SendVkKey(WORD vk)
{
    INPUT in[2];
    ZeroMemory(in, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, in, sizeof(INPUT)) == 2;
}

// ============================================================
//  单元构建
// ============================================================
std::vector<TypingUnit> BuildUnits(const std::wstring& text)
{
    std::vector<TypingUnit> units;
    units.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t ch = text[i];

        // CRLF 只发一个回车；单独的 \r 也当回车
        if (ch == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            TypingUnit u;
            u.kind = TypingUnit::VirtualKey;
            u.vk = VK_RETURN;
            units.push_back(u);
            continue;
        }
        if (ch == L'\n') {
            TypingUnit u;
            u.kind = TypingUnit::VirtualKey;
            u.vk = VK_RETURN;
            units.push_back(u);
            continue;
        }
        if (ch == L'\t') {
            TypingUnit u;
            u.kind = TypingUnit::VirtualKey;
            u.vk = VK_TAB;
            units.push_back(u);
            continue;
        }

        TypingUnit u;
        u.kind = TypingUnit::Unicode;
        u.ch = ch;
        if (ch >= 0xD800 && ch <= 0xDBFF) {
            // 合法代理对：标记高低位，批边界不得拆分
            if (i + 1 < text.size() &&
                text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
                u.highSurrogate = true;
            }
        } else if (ch >= 0xDC00 && ch <= 0xDFFF) {
            u.lowSurrogate = true;
        }
        units.push_back(u);
    }
    return units;
}

int AutoBatchChars(const TypingOptions& opts)
{
    if (opts.humanize.enabled) return 1;   // 拟人化逐字
    if (opts.batchChars > 0) {
        if (opts.batchChars > 256) return 256;
        return opts.batchChars;
    }
    // 自动策略：间隔越大，说明目标程序越吃不下，批越小。
    // interval == 0 时本来就无节拍，直接大批量（syscall 收益最大）。
    if (opts.intervalMs <= 0) return 64;
    if (opts.intervalMs <= 3) return 8;
    return 1;                              // 默认 8ms 等配置保持逐字，完全兼容
}

// ============================================================
//  完整性级别（UIPI 预检）
// ============================================================
static unsigned RidFromToken(HANDLE token)
{
    DWORD len = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &len);
    if (!len) return 0;

    std::vector<BYTE> buf(len);
    if (!GetTokenInformation(token, TokenIntegrityLevel, buf.data(), len, &len))
        return 0;

    auto* label = (TOKEN_MANDATORY_LABEL*)buf.data();
    if (!label->Label.Sid) return 0;
    DWORD cnt = *GetSidSubAuthorityCount(label->Label.Sid);
    if (!cnt) return 0;
    return *GetSidSubAuthority(label->Label.Sid, cnt - 1);
}

static unsigned IntegrityOfProcess(HANDLE proc)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(proc, TOKEN_QUERY, &token)) return 0;
    unsigned rid = RidFromToken(token);
    CloseHandle(token);
    return rid;
}

IntegrityInfo ProbeForegroundIntegrity()
{
    IntegrityInfo info;
    info.targetWindow = GetForegroundWindow();

    info.self = IntegrityOfProcess(GetCurrentProcess());
    if (!info.targetWindow) return info;

    DWORD pid = 0;
    GetWindowThreadProcessId(info.targetWindow, &pid);
    if (!pid) return info;

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) {
        // 打不开句柄通常意味着对方权限更高（受保护进程同理）
        if (info.self) info.targetHigher = true;
        return info;
    }
    info.target = IntegrityOfProcess(proc);
    CloseHandle(proc);

    if (info.self && info.target)
        info.targetHigher = info.target > info.self;
    return info;
}

// ============================================================
//  英文布局（尽力而为，失败即降级）
// ============================================================
struct LayoutState {
    HWND  target = nullptr;
    DWORD threadId = 0;
    HKL   original = nullptr;
    bool  changed = false;
};

static bool RequestEnglishLayout(LayoutState& st, HWND target)
{
    if (!target) return false;
    if (!IsWindow(target)) return false;

    st.target = target;
    st.threadId = GetWindowThreadProcessId(target, nullptr);
    st.original = GetKeyboardLayout(st.threadId);

    HKL en = LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE);
    if (!en) return false;

    // 已经是英文布局就不动，避免无谓的输入状态抖动
    if (st.original == en) return false;

    PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)en);
    Sleep(60);
    st.changed = true;
    return true;
}

static void RestoreLayout(const LayoutState& st)
{
    if (!st.changed || !st.original) return;
    if (!st.target || !IsWindow(st.target)) return;
    PostMessageW(st.target, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)st.original);
    Sleep(20);
}

// ============================================================
//  等待
// ============================================================
static bool WaitMs(int ms, const std::atomic<bool>* stop)
{
    if (ms <= 0) return true;
    int left = ms;
    while (left > 0) {
        if (stop && stop->load(std::memory_order_relaxed)) return false;
        int slice = left > 4 ? 4 : left;
        Sleep((DWORD)slice);
        left -= slice;
    }
    return !(stop && stop->load(std::memory_order_relaxed));
}

// ============================================================
//  批量发送
// ============================================================
// 返回成功注入的事件数（0 表示被拦截）
static int SendUnicodeBatch(const TypingUnit* units, size_t n,
                            std::vector<INPUT>& scratch)
{
    if (!n) return 0;

    scratch.clear();
    scratch.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        INPUT down;
        ZeroMemory(&down, sizeof(down));
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = 0;
        down.ki.wScan = units[i].ch;
        down.ki.dwFlags = KEYEVENTF_UNICODE;

        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        scratch.push_back(down);
        scratch.push_back(up);
    }

    UINT req = (UINT)scratch.size();
    UINT sent = SendInput(req, scratch.data(), sizeof(INPUT));
    return (int)sent;
}

// ============================================================
//  拟人化节奏
// ============================================================
static int ClampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool IsPausePunct(wchar_t c)
{
    switch (c) {
    case L'.': case L',': case L';': case L':': case L'!': case L'?':
    case L'。': case L'，': case L'；': case L'：': case L'！': case L'？':
    case L'、': case L'…':
        return true;
    default:
        return false;
    }
}

static bool IsAsciiLetter(wchar_t c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

// 邻键表仅覆盖 ASCII 小写，够模拟"手滑"
static wchar_t NeighborKey(wchar_t c)
{
    static const wchar_t* layout = L"qwertyuiopasdfghjklzxcvbnm";
    const wchar_t* p = wcschr(layout, c);
    if (!p) return c;
    size_t idx = (size_t)(p - layout);
    size_t n = wcslen(layout);
    size_t other = (idx % 2 == 0) ? (idx + 1) % n : (idx == 0 ? n - 1 : idx - 1);
    return layout[other];
}

struct Humanizer {
    std::mt19937 rng;
    double progress = 0.0;

    Humanizer() : rng((unsigned)GetTickCount64() ^ (unsigned)(size_t)this) {}

    double Gaussian(double mean, double sigma)
    {
        if (sigma <= 0.01) return mean;
        std::normal_distribution<double> d(mean, sigma);
        double v = d(rng);
        return v;
    }

    int DelayFor(const HumanizeOptions& o, wchar_t ch, bool wordEnd)
    {
        double mean = o.meanMs;
        if (o.fatigue && o.fatigueGrows)
            mean *= 1.0 + 0.35 * progress;      // 越到后面越慢

        double base = Gaussian(mean, o.sigmaMs);

        if (o.pauseAfterPunct && IsPausePunct(ch))
            base += Gaussian(140.0, 40.0);
        else if (o.pauseAfterNewline && (ch == L'\r' || ch == L'\n'))
            base += Gaussian(240.0, 60.0);
        else if (o.pauseAfterLongWord && wordEnd)
            base += Gaussian(90.0, 30.0);

        if (base < o.minMs) base = o.minMs;
        if (base > o.maxMs) base = o.maxMs;
        return (int)(base + 0.5);
    }
};

// ============================================================
//  主入口
// ============================================================
TypingReport TypeText(const std::wstring& text, const TypingOptions& opts,
                      const std::atomic<bool>* stop)
{
    TypingReport report;
    report.totalCodePoints = (int)CountCodePoints(text);

    if (text.empty()) {
        report.status = TypingStatus::Empty;
        return report;
    }

    // ---- UIPI 预检（只提示，不阻止：与旧行为一致，仍尝试注入）----
    if (opts.checkUipi) {
        IntegrityInfo ii = ProbeForegroundIntegrity();
        report.selfIntegrity   = ii.self;
        report.targetIntegrity = ii.target;
        report.targetHigher    = ii.targetHigher;
    }

    // ---- 英文布局（尽力而为）----
    LayoutState layout;
    if (opts.forceEnglish) {
        HWND fg = GetForegroundWindow();
        if (fg && fg != GetConsoleWindow())
            report.layoutChanged = RequestEnglishLayout(layout, fg);
    }

    // 提升定时器精度，让 intervalMs 语义真实（默认 Sleep 分辨率约 15.6ms）
    timeBeginPeriod(1);

    std::vector<TypingUnit> units = BuildUnits(text);
    const int batchLimit = AutoBatchChars(opts);
    const bool humanize = opts.humanize.enabled;

    std::vector<INPUT> scratch;
    scratch.reserve((size_t)batchLimit * 2);

    std::vector<TypingUnit> pending;
    pending.reserve((size_t)batchLimit);

    Humanizer hz;
    const size_t totalUnits = units.size();
    int fails = 0;
    bool blocked = false;

    // 发送当前批次。返回值 = 成功注入的输入单元数（wchar 计数），
    // 用于按 interval × 单元数计算批间等待，保持 v1.0.0 的总时长语义。
    auto flush = [&]() -> int {
        if (pending.empty()) return 0;
        int want = (int)pending.size();
        int sent = SendUnicodeBatch(pending.data(), pending.size(), scratch);
        int req = want * 2;

        report.batches++;
        int okUnits = 0;
        if (sent == req) {
            okUnits = want;
            report.injectedPoints += want;
            fails = 0;
        } else {
            int lost = req - sent;
            if (lost < 0) lost = 0;
            report.blockedEvents += lost;
            okUnits = (int)((double)sent / 2.0 + 0.5);
            report.injectedPoints += okUnits;
            if (++fails >= 5) blocked = true;
        }
        pending.clear();
        return okUnits;
    };

    // 非拟人化：一次等待覆盖整批，总时长仍为 interval × 码点数
    auto waitBatch = [&](int unitsInBatch) -> bool {
        if (unitsInBatch <= 0) return true;
        long long total = (long long)opts.intervalMs * unitsInBatch;
        if (total > 60000) total = 60000;
        return WaitMs((int)total, stop);
    };

    // 长单词判定（拟人化）
    int wordLen = 0;

    for (size_t i = 0; i < totalUnits && !blocked; ++i) {
        if (stop && stop->load(std::memory_order_relaxed)) break;

        const TypingUnit& u = units[i];

        if (u.kind == TypingUnit::VirtualKey) {
            int okUnits = flush();
            if (blocked) break;
            if (okUnits > 0 && !humanize) {
                if (!waitBatch(okUnits)) break;
            }

            bool ok = SendVkKey(u.vk);
            report.batches++;
            if (ok) {
                report.injectedPoints++;
                fails = 0;
            } else {
                report.blockedEvents += 2;
                if (++fails >= 5) blocked = true;
            }

            if (humanize) {
                hz.progress = totalUnits ? (double)(i + 1) / totalUnits : 1.0;
                int d = hz.DelayFor(opts.humanize,
                                    u.vk == VK_RETURN ? L'\n' : L'\t', false);
                if (!WaitMs(d, stop)) break;
            } else if (!WaitMs(opts.intervalMs, stop)) {
                break;
            }
            wordLen = 0;
            continue;
        }

        // 拟人化：极低概率的错字 + 退格修正（默认 typoRate = 0，完全关闭）
        if (humanize && opts.humanize.typoRate > 0.0 &&
            u.ch > 0 && u.ch < 128 && IsAsciiLetter(u.ch)) {
            double rate = opts.humanize.typoRate;
            if (rate > 0.05) rate = 0.05;
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            if (dist(hz.rng) < rate) {
                int okUnits = flush();
                if (blocked) break;
                if (okUnits > 0 && !waitBatch(okUnits)) break;

                wchar_t wrong = NeighborKey((wchar_t)towlower(u.ch));
                if (!SendUnicodeChar(wrong)) {
                    report.blockedEvents += 2;
                    if (++fails >= 5) { blocked = true; break; }
                } else {
                    report.batches++;
                    fails = 0;
                }
                if (!WaitMs(opts.humanize.typoFixDelayMs, stop)) break;

                if (!SendVkKey(VK_BACK)) {
                    report.blockedEvents += 2;
                } else {
                    report.batches++;
                }
                if (!WaitMs(opts.humanize.minMs, stop)) break;
            }
        }

        // 代理对不得被批边界拆开
        bool nextIsLow = (i + 1 < totalUnits) && units[i + 1].lowSurrogate;
        size_t needed = (u.highSurrogate && nextIsLow) ? 2 : 1;

        if ((int)(pending.size() + needed) > batchLimit && !pending.empty()) {
            int okUnits = flush();
            if (blocked) break;
            if (okUnits > 0) {
                if (humanize) {
                    hz.progress = totalUnits ? (double)i / totalUnits : 1.0;
                    if (!WaitMs(hz.DelayFor(opts.humanize, L' ', false), stop)) break;
                } else if (!waitBatch(okUnits)) {
                    break;
                }
            }
        }

        pending.push_back(u);
        if (needed == 2) pending.push_back(units[++i]);

        // 批满即发
        if ((int)pending.size() >= batchLimit) {
            int okUnits = flush();
            if (blocked) break;
            if (okUnits > 0) {
                if (humanize) {
                    hz.progress = totalUnits ? (double)(i + 1) / totalUnits : 1.0;
                    if (!WaitMs(hz.DelayFor(opts.humanize, u.ch, false), stop)) break;
                } else if (!waitBatch(okUnits)) {
                    break;
                }
            }
        }

        if (humanize) {
            // 长单词后的额外停顿
            if (IsAsciiLetter(u.ch)) {
                ++wordLen;
            } else {
                if (wordLen >= 8 && opts.humanize.pauseAfterLongWord)
                    if (!WaitMs((int)hz.Gaussian(90.0, 30.0), stop)) break;
                wordLen = 0;
            }
        }
    }

    if (!blocked) flush();

    timeEndPeriod(1);

    // ---- 恢复布局 ----
    if (opts.restoreLayout && layout.changed) {
        RestoreLayout(layout);
        report.layoutRestored = true;
    }

    if (blocked)              report.status = TypingStatus::Blocked;
    else if (report.targetHigher && report.injectedPoints == 0)
                              report.status = TypingStatus::TargetHigherIL;
    else                      report.status = TypingStatus::Ok;

    return report;
}

} // namespace at
