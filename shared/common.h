/* common.h —— 四个模块共用的基础设施
 *                （UTF-8 日志 / 长路径解析 / GameAssembly 构建校验）
 *
 * 为什么单独抽出来
 * ----------------
 * 四个模块（hotkey 伞形 / speedkey / pinattr / goldhand）是四个互相独立的
 * DLL，各自带一份「最小内核」：写日志、把资源定位到 DLL 自己所在目录、
 * 校验 GameAssembly.dll 的 SHA256 是否在白名单里。这四份实现原本是逐字
 * 复制的 —— 复制粘贴在「同一处逻辑要改四遍」时会变成 bug 温床（v2.3.1
 * 的长路径改造就漏过一个模块）。这里把它们收成一份头文件。
 *
 * 为什么是头文件而不是 .c
 * -----------------------
 * 四个 DLL 互相独立、不共享运行时，也不能互相导入（否则任何一个缺失都会
 * 让其他三个连带失败）。因此全部实现写成 static，每个 DLL 各编译一份，
 * 链接期各自内联、互不可见 —— 与 rva_reloc.h 同一策略。
 *
 * 使用前提（模块 .c 必须先满足）
 * ------------------------------
 *   1) 已 #include <windows.h> <wincrypt.h> <stdio.h> <stdint.h>
 *      <stdbool.h> <stdarg.h> <string.h>；
 *   2) 已声明两个模块级全局：
 *        static HMODULE self;                    // 本 DLL 的模块句柄
 *        static HANDLE  logfile;                 // 日志句柄（INVALID_HANDLE_VALUE = 关闭）
 *   3) 已定义本模块的 SHA256 白名单数组，并通过宏告诉本文件：
 *        #define HSLR_ACCEPTED_HASHES  <name>_accepted_hashes
 *      （不定义则编译期报错 —— 宁可编不过，也不要静默接受任意构建。）
 *
 * 命名：对外符号统一加 hslr_ 前缀，避免与模块内同名函数冲突。
 */

#ifndef HSLR_COMMON_H
#define HSLR_COMMON_H

#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#ifndef HSLR_ACCEPTED_HASHES
#error "common.h: 请先 #define HSLR_ACCEPTED_HASHES <本模块的 SHA256 白名单数组名>"
#endif

#if defined(__GNUC__)
#define HSLR_MAYBE_UNUSED __attribute__((unused))
#else
#define HSLR_MAYBE_UNUSED
#endif

/* ------------------------------------------------------------------ */
/* 长路径                                                              */
/* ------------------------------------------------------------------ */
/* Windows 传统路径上限是 MAX_PATH(260)。游戏装在很深的目录、或路径里套了
 * 多层中文文件夹时，DLL 旁边的 ini/log 会因为路径超长而打开失败 —— 表现
 * 是「功能静默失效，连日志文件都不生成」，从用户侧几乎查不出来。
 *
 * 统一走长路径：
 *   ① 用 32K 缓冲解析自身完整路径（GetModuleFileNameW 本身不受 260 限制）；
 *   ② 拼出目标路径后，若长度 >= MAX_PATH 就补上 \\?\ 前缀（Windows 长路径
 *      的标准写法），让 CreateFileW / LoadLibraryW 等 API 接受；
 *   ③ 长度 < MAX_PATH 时一字不改 —— 老环境行为 100% 不变，零回归风险。
 *
 * \\?\ 的前提是「完全限定的规范化绝对路径」（不含 . / ..），而
 * GetModuleFileNameW 返回的路径正好满足；网络路径要换成 \\?\UNC\ 形式。 */
#define HSLR_PATH_MAX 32768

/* 把 <本 DLL 所在目录>\<file> 写进 p（p 需有 HSLR_PATH_MAX 容量）。
 * 解析不出自身路径时留空串：调用方会失败，但绝不越界。 */
static HSLR_MAYBE_UNUSED void path_for(WCHAR *p, const WCHAR *file) {
    p[0] = L'\0';
    DWORD n = GetModuleFileNameW(self, p, HSLR_PATH_MAX);
    if (!n || n >= HSLR_PATH_MAX) return;
    WCHAR *q = wcsrchr(p, L'\\');
    if (!q) return;
    if ((size_t)(q + 1 - p) + wcslen(file) + 1 > HSLR_PATH_MAX) return;
    wcscpy(q + 1, file);
    size_t len = wcslen(p);
    if (len + 1 > MAX_PATH) {
        if (p[0] == L'\\' && p[1] == L'\\') {
            /* UNC: \\server\share\... → \\?\UNC\server\share\... */
            memmove(p + 8, p + 2, (wcslen(p + 2) + 1) * sizeof(WCHAR));
            memcpy(p, L"\\\\?\\UNC\\", 8 * sizeof(WCHAR));
        } else {
            memmove(p + 4, p, (len + 1) * sizeof(WCHAR));
            memcpy(p, L"\\\\?\\", 4 * sizeof(WCHAR));
        }
    }
}

/* UTF-16 → UTF-8。日志是 UTF-8 文本，而游戏内输入/路径里的中文很常见。 */
static HSLR_MAYBE_UNUSED void wide_to_utf8(const WCHAR *w, char *out, int cap) {
    if (cap <= 0) return;
    out[0] = '\0';
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* 日志                                                                */
/* ------------------------------------------------------------------ */

/* 追加一行（自动带启动以来的毫秒时间戳与 CRLF）。logfile 未打开时静默丢弃：
 * 日志永远不能成为功能路径上的失败点。 */
static HSLR_MAYBE_UNUSED void logmsg(const char *fmt, ...) {
    char b[1000];
    int n = snprintf(b, sizeof(b), "[%llu] ", (unsigned long long)GetTickCount64());
    va_list v;
    va_start(v, fmt);
    int k = vsnprintf(b + n, sizeof(b) - n - 3, fmt, v);
    va_end(v);
    if (k < 0) return;
    if (k > (int)sizeof(b) - n - 4) k = sizeof(b) - n - 4;
    n += k;
    b[n++] = '\r';
    b[n++] = '\n';
    DWORD w;
    if (logfile != INVALID_HANDLE_VALUE) WriteFile(logfile, b, n, &w, NULL);
}

/* 打开 DLL 旁边的日志文件（每次启动覆盖）。
 * 双向 FILE_SHARE 是为了让玩家能在游戏还开着的时候 tail 这个文件。 */
static HSLR_MAYBE_UNUSED void hslr_log_open(const WCHAR *file) {
    WCHAR path[HSLR_PATH_MAX];
    path_for(path, file);
    logfile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, CREATE_ALWAYS, 0, NULL);
}

/* 把本 DLL 的实际路径写进日志。装错位置 / 被杀软隔离产生副本时，
 * 远端用户把日志发回来就能一眼看出问题 —— 这是最高频的求助原因。 */
static HSLR_MAYBE_UNUSED void hslr_log_self_path(void) {
    WCHAR self_path[HSLR_PATH_MAX];
    char dirbuf[2048];   /* UTF-8：中文按 3 字节算，超长只截断显示、不影响功能 */
    GetModuleFileNameW(self, self_path, HSLR_PATH_MAX);
    wide_to_utf8(self_path, dirbuf, (int)sizeof(dirbuf));
    logmsg("Mod DLL path: %s", dirbuf);
}

/* ------------------------------------------------------------------ */
/* GameAssembly 构建校验                                               */
/* ------------------------------------------------------------------ */

/* 计算 DLL 旁边 GameAssembly.dll 的 SHA256，与本模块的白名单比对。
 *
 * 返回值（三态）：
 *    1  哈希命中白名单 —— 构建未变，走 NATIVE 恒等映射（零开销）
 *    0  哈希合法但不认识 —— 官方更新了，走 ADAPTIVE 重定位（不再整体停机）
 *   -1  读不到 / 算不出哈希 —— 安装位置不对或环境异常，仍然停机
 *
 * 白名单的作用是「提前知道当前是哪个构建」，不是安全闸门：命中与否只决定
 * 走恒等映射还是走重定位，两者都不会去调用未经校验的地址。 */
static HSLR_MAYBE_UNUSED int known_build(void) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, L"GameAssembly.dll");
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        logmsg("GameAssembly.dll cannot be opened next to the Mod DLL (err=%lu); "
               "is the Mod DLL really inside the game directory?",
               (unsigned long)GetLastError());
        return -1;
    }
    HCRYPTPROV cp = 0;
    HCRYPTHASH h = 0;
    BYTE buf[65536], digest[32];
    DWORD got, n = 32;
    int ok = -1;          /* 默认：连哈希都没算出来 */
    char hex[65] = "";
    if (CryptAcquireContextW(&cp, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(cp, CALG_SHA_256, 0, 0, &h)) {
        bool good = true;
        for (;;) {
            if (!ReadFile(f, buf, sizeof(buf), &got, NULL)) { good = false; break; }
            if (!got) break;
            if (!CryptHashData(h, buf, got, 0)) { good = false; break; }
        }
        if (good && CryptGetHashParam(h, HP_HASHVAL, digest, &n, 0)) {
            for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
            logmsg("GameAssembly SHA256 %s", hex);
            ok = 0;       /* 哈希算出来了，先按「不认识的构建」记账，命中白名单再升为 1 */
            for (unsigned i = 0; i < sizeof(HSLR_ACCEPTED_HASHES) / sizeof(HSLR_ACCEPTED_HASHES[0]); i++)
                if (!strcmp(hex, HSLR_ACCEPTED_HASHES[i])) { ok = 1; break; }
        } else {
            logmsg("GameAssembly SHA256 computation failed; Mod inactive");
        }
    } else {
        logmsg("CryptoAPI unavailable; cannot verify GameAssembly; Mod inactive");
    }
    if (ok == 0) {
        logmsg("This GameAssembly build is not in the recorded list (game updated?). Recorded builds:");
        for (unsigned i = 0; i < sizeof(HSLR_ACCEPTED_HASHES) / sizeof(HSLR_ACCEPTED_HASHES[0]); i++)
            logmsg("  recorded: %s", HSLR_ACCEPTED_HASHES[i]);
        logmsg("Entering ADAPTIVE relocation (anchors re-located from their long masked "
               "fingerprints, see rva_anchors.h)");
    }
    if (h) CryptDestroyHash(h);
    if (cp) CryptReleaseContext(cp, 0);
    CloseHandle(f);
    return ok;
}

#endif /* HSLR_COMMON_H */
