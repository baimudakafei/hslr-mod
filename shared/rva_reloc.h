/* rva_reloc.h —— 捕获版 RVA → 当前版本 RVA 的运行时重定位层
 *                （免版本跟进 · 阶段一：闸门降级 C + 锚点窗口搜索 B）
 *
 * 背景（见 hotkey/docs/免版本跟进-兼容方案.md §9）
 * ---------------------------------------------------------------
 * 现状把「上一次重解时的 RVA」硬编码进源码与指纹表。官方小版本一更新，
 * 哪怕只是新增了别的功能、位移了代码段，这些 RVA 也会全部指错；此时
 * 白名单哈希一比不中，四个模块在第一秒就整体停机 —— 玩家看到的是
 * 「全部失效」。本文件把这段耦合拆开：
 *
 *   - 源码里写死的 RVA 从此只当「锚点（anchor）」，含义是「我上一次
 *     重解时它在哪」；
 *   - 启动时按锚点附近的窗口做掩码字节搜索，唯一命中 → 采用；否则该
 *     点标记失败、只降级依赖它的功能，其余功能照常。
 *
 * 三种模式
 * --------
 *   NATIVE   GA 哈希命中白名单（构建没变）→ 恒等映射，行为与旧版 100%
 *            一致，零额外开销、零风险。这是默认路径。
 *   ADAPTIVE 哈希不认识（官方更新了）→ 逐点扫描重定位 + 单点降级。
 *   SELFTEST 本地验证用：即使构建没变也强制走扫描，并把「查询锚点」人为
 *            偏移 rva_shot 字节，用来证明扫描器真的能从错误锚点恢复。
 *
 * 为什么 GATE 用「唯一命中」而不是「取最近」
 * ------------------------------------------
 * 实测 16 B 掩码指纹在 4 MB 窗口内有上千个误命中（256 KB 窗口内 256 B
 * 长指纹才收敛到 93%）。「取最近」在多命中时无判据，一旦选错就是按错误
 * 偏移调用游戏函数（v2.7.0 事故的形态）。因此本层只认「窗口内恰好一个
 * 命中」，多命中一律判失败并降级 —— 宁可少一个功能，不可乱调一个地址。
 *
 * 使用方式（每个模块）
 * -------------------
 *   1) 由 tools/analyze/capture_fingerprints.py 生成 rva_anchors.h
 *      （本文件 #include 它）；
 *   2) 在 worker() 里 GA 就绪后调用 rva_reloc_init(native_hash_ok)；
 *   3) 把 FN()/addhook() 宏的底层寻址改成 rva_of(rva)；
 *   4) 用 rva_group_ok(g) 决定某组功能是否放行。
 *
 * 头文件里全是 static：每个 DLL 一份独立实例，互不干扰。
 */

#ifndef HSLR_RVA_RELOC_H
#define HSLR_RVA_RELOC_H

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* 生成物：RvaAnchor rva_anchors[] + RVA_ANCHOR_COUNT */
#include "rva_anchors.h"

/* 模块注入的日志函数（缺省静默） */
#ifndef RVA_LOG
#define RVA_LOG(...) ((void)0)
#endif

/* 头文件里部分接口并非每个模块都用到（如 rva_group_ok 只有伞形用）。
 * 标成 unused 可让「-Wall -Wextra 零告警」成立——否则某些构建脚本会把
 * 编译器 stderr 误当致命错误。 */
#if defined(__GNUC__)
#define RVA_MAYBE_UNUSED __attribute__((unused))
#else
#define RVA_MAYBE_UNUSED
#endif

/* 搜索窗口半宽。实测最优 256 KB：再窄误命中回升，再宽误命中增多。 */
#ifndef RVA_WINDOW_DEFAULT
#define RVA_WINDOW_DEFAULT 0x40000u
#endif
/* 命中数超过这个值就当作「没找到」早退，避免在噪声模式上白扫。 */
#ifndef RVA_MAX_HITS
#define RVA_MAX_HITS 64
#endif
#define RVA_MAX_REGIONS 8

/* 模式 */
#define RVA_MODE_AUTO     0
#define RVA_MODE_NATIVE   1
#define RVA_MODE_ADAPTIVE 2
#define RVA_MODE_SELFTEST 3

/* 单点状态 */
#define RVA_ST_PENDING 0
#define RVA_ST_NATIVE  1
#define RVA_ST_SCANNED 2
#define RVA_ST_FAILED  3

static uintptr_t rva_slot[RVA_ANCHOR_COUNT];
static uint8_t   rva_state[RVA_ANCHOR_COUNT];
static int       rva_mode = RVA_MODE_AUTO;
static uintptr_t rva_window = RVA_WINDOW_DEFAULT;
static intptr_t  rva_shot = 0;
static int       rva_failed_count = 0;
static int       rva_scan_hits_total = 0;
static bool      rva_ready = false;
static bool      rva_configured = false;

typedef struct { uintptr_t lo, hi; } RvaRegion;
static RvaRegion rva_regions[RVA_MAX_REGIONS];
static int       rva_region_count = 0;

/* ------------------------------------------------------------------ */
/* 配置（读伞形 ini 的 [options]，四个模块共用同一份文件）             */
/* ------------------------------------------------------------------ */

/* 极简 INI 取值：找到 [options] 段下名为 key 的行，返回其值字符串。
 * 只认 ASCII 键名，且只在本文件内部使用——各模块自己的 ini 解析器不受影响。 */
static bool rva_ini_lookup(const char *text, const char *key, char *out, int cap) {
    bool in_options = false;
    const char *p = text;
    while (*p && cap > 1) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (!*p) break;
        if (*p == '#' || *p == ';') {                     /* 注释行 */
            while (*p && *p != '\n') ++p;
            continue;
        }
        const char *eol = p;
        while (*eol && *eol != '\n' && *eol != '\r') ++eol;
        if (*p == '[') {                                  /* 段头 */
            in_options = (eol - p >= 8 && !_strnicmp(p, "[options]", 9));
        } else if (in_options) {
            const char *eq = p;
            while (eq < eol && *eq != '=') ++eq;
            if (eq < eol) {
                const char *k = p, *ke = eq;
                while (k < ke && (*k == ' ' || *k == '\t')) ++k;
                while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) --ke;
                if ((int)(ke - k) == (int)strlen(key) && !_strnicmp(k, key, (size_t)(ke - k))) {
                    const char *v = eq + 1, *ve = eol;
                    while (v < ve && (*v == ' ' || *v == '\t')) ++v;
                    while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) --ve;
                    int n = (int)(ve - v);
                    if (n > cap - 1) n = cap - 1;
                    memcpy(out, v, (size_t)n);
                    out[n] = 0;
                    return true;
                }
            }
        }
        p = eol;
    }
    return false;
}

/* 解析一个整数字面量，接受 0x 十六进制与十进制。 */
static uintptr_t rva_parse_num(const char *s, bool *negative) {
    *negative = false;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '-') { *negative = true; ++s; }
    else if (*s == '+') ++s;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (uintptr_t)strtoul(s + 2, NULL, 16);
    return (uintptr_t)strtoul(s, NULL, 10);
}

/* 供模块调用：ini_path 指向伞形配置文件（hslr-wait-hotkey.ini）。
 * 文件缺失 / 键缺失 / 值非法一律保持默认（AUTO + 256 KB + 无偏移）。 */
static void rva_reloc_configure(const WCHAR *ini_path) {
    if (rva_configured) return;
    rva_configured = true;
    if (!ini_path || !ini_path[0]) return;
    HANDLE f = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    char text[8192];
    DWORD got = 0;
    BOOL ok = ReadFile(f, text, (DWORD)sizeof(text) - 1, &got, NULL);
    CloseHandle(f);
    if (!ok || got == 0) return;
    text[got] = 0;
    const char *scan = text;
    if (got >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        scan += 3;

    char v[32];
    if (rva_ini_lookup(scan, "rva_mode", v, sizeof(v))) {
        if (!_stricmp(v, "auto"))          rva_mode = RVA_MODE_AUTO;
        else if (!_stricmp(v, "native"))   rva_mode = RVA_MODE_NATIVE;
        else if (!_stricmp(v, "adaptive")) rva_mode = RVA_MODE_ADAPTIVE;
        else if (!_stricmp(v, "selftest")) rva_mode = RVA_MODE_SELFTEST;
    }
    if (rva_ini_lookup(scan, "rva_shot", v, sizeof(v))) {
        bool neg = false;
        uintptr_t n = rva_parse_num(v, &neg);
        rva_shot = neg ? -(intptr_t)n : (intptr_t)n;
    }
    if (rva_ini_lookup(scan, "rva_window", v, sizeof(v))) {
        bool neg = false;
        uintptr_t n = rva_parse_num(v, &neg);
        if (n >= 0x1000 && n <= 0x800000) rva_window = n;
    }
}

/* ------------------------------------------------------------------ */
/* PE 段范围（只扫可执行段：.text 与 il2cpp）                          */
/* ------------------------------------------------------------------ */

static void rva_collect_regions(void *module) {
    rva_region_count = 0;
    if (!module) return;
    const BYTE *b = (const BYTE *)module;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)b;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(b + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        DWORD ch = sec->Characteristics;
        bool exec = (ch & IMAGE_SCN_MEM_EXECUTE) != 0;
        bool code = (ch & IMAGE_SCN_CNT_CODE) != 0;
        char nm[9];
        memcpy(nm, sec->Name, 8);
        nm[8] = 0;
        bool named = !strcmp(nm, ".text") || !strcmp(nm, "il2cpp");
        if (!exec && !code && !named) continue;
        uint32_t sz = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
        if (!sz) continue;
        if (rva_region_count >= RVA_MAX_REGIONS) break;
        rva_regions[rva_region_count].lo = sec->VirtualAddress;
        rva_regions[rva_region_count].hi = (uintptr_t)sec->VirtualAddress + sz;
        ++rva_region_count;
    }
}

/* ------------------------------------------------------------------ */
/* 锚点窗口掩码搜索                                                    */
/* ------------------------------------------------------------------ */

/* 在 [query-W, query+W] 与本进程可执行段的交集里，找 pattern 的掩码匹配。
 * 返回命中数；hits==1 时 *nearest 为唯一命中；否则 *nearest 为离 query 最近者。
 * 命中数 > RVA_MAX_HITS 时提前返回（当作"噪声"）。 */
static int rva_scan(const RvaAnchor *a, uintptr_t query, uintptr_t *nearest) {
    int k0 = -1;
    for (int i = 0; i < (int)a->len; ++i)
        if (a->msk[i]) { k0 = i; break; }
    if (k0 < 0) return -1;                 /* 全通配 → 无判据 */

    int hits = 0;
    int64_t best = 0;
    const BYTE *img = (const BYTE *)ga;
    for (int r = 0; r < rva_region_count; ++r) {
        uintptr_t lo = rva_regions[r].lo, hi = rva_regions[r].hi;
        uintptr_t wlo = query > rva_window ? query - rva_window : 0;
        uintptr_t whi = query + rva_window;
        if (wlo < lo) wlo = lo;
        if (whi > hi) whi = hi;
        if (whi <= wlo || whi - wlo < a->len) continue;

        const BYTE *base = img + wlo;
        const BYTE *end = base + (whi - wlo) - a->len;   /* 最后一个合法起点 */
        const BYTE *p = base;
        while (p <= end) {
            const BYTE *probe = p + k0;
            size_t left = (size_t)(end + k0 - probe) + 1;
            const BYTE *hit = (const BYTE *)memchr(probe, a->pat[k0], left);
            if (!hit) break;
            const BYTE *s = hit - k0;
            bool ok = true;
            for (int i = 0; i < (int)a->len; ++i)
                if (a->msk[i] && s[i] != a->pat[i]) { ok = false; break; }
            if (ok) {
                uintptr_t rva = (uintptr_t)(s - img);
                int64_t d = (int64_t)rva - (int64_t)query;
                if (d < 0) d = -d;
                if (hits == 0 || d < best) { best = d; *nearest = rva; }
                if (++hits > RVA_MAX_HITS) return hits;
            }
            p = s + 1;
        }
    }
    return hits;
}

/* ------------------------------------------------------------------ */
/* 解析全部锚点                                                        */
/* ------------------------------------------------------------------ */

static void rva_resolve_all(void) {
    bool selftest = (rva_mode == RVA_MODE_SELFTEST);
    int selftest_pass = 0;
    for (int i = 0; i < (int)RVA_ANCHOR_COUNT; ++i) {
        const RvaAnchor *a = &rva_anchors[i];
        uintptr_t truth = (uintptr_t)a->anchor;
        /* selftest：假装我们手里的锚点已经过时 rva_shot 字节 */
        uintptr_t query = selftest ? (uintptr_t)((intptr_t)truth + rva_shot) : truth;
        uintptr_t found = 0;
        int hits = rva_scan(a, query, &found);
        rva_scan_hits_total += hits > 0 ? hits : 0;
        bool good;
        if (selftest)
            good = (hits == 1 && found == truth);     /* 必须精确回到真值 */
        else
            good = (hits == 1);
        if (good) {
            rva_slot[i] = found;
            rva_state[i] = RVA_ST_SCANNED;
            if (selftest) ++selftest_pass;
            RVA_LOG("reloc %s %-40s %X -> %X (hits=1%s)", selftest ? "PASS" : "ok",
                    a->name, (unsigned)truth, (unsigned)found,
                    selftest ? ", selftest" : "");
        } else {
            rva_slot[i] = 0;
            rva_state[i] = RVA_ST_FAILED;
            ++rva_failed_count;
            RVA_LOG("reloc FAIL %-40s %X hits=%d -> point disabled (group %u)",
                    a->name, (unsigned)truth, hits, (unsigned)a->group);
        }
    }
    if (selftest)
        RVA_LOG("reloc SELFTEST: %d/%d anchors recovered with a %+lld byte stale anchor "
                "(window 0x%X)", selftest_pass, RVA_ANCHOR_COUNT, (long long)rva_shot,
                (unsigned)rva_window);
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */

static int rva_index_of(uintptr_t anchor) {
    for (int i = 0; i < (int)RVA_ANCHOR_COUNT; ++i)
        if ((uintptr_t)rva_anchors[i].anchor == anchor) return i;
    return -1;
}

/* 锚点 → 运行时 RVA。未初始化 / NATIVE / 表外锚点一律原值返回；
 * ADAPTIVE 下该点解析失败返回 0（调用方由 rva_group_ok 保证不会走到）。 */
static uintptr_t rva_of(uintptr_t anchor) {
    if (!rva_ready || rva_mode == RVA_MODE_NATIVE) return anchor;
    int i = rva_index_of(anchor);
    if (i < 0) return anchor;
    if (rva_state[i] == RVA_ST_FAILED) return 0;
    return rva_slot[i];
}

/* 某一组锚点是否全部可用（group 由采样器按来源文件打：0=主/伞形，1=approach）。
 * 只有伞形（waitkey）用得到分组门；三个卫星只认 rva_all_ok()，所以这里必须
 * 标 unused，否则 -Wall -Wextra 会在卫星 TU 里报「未使用函数」——而 speedkey
 * 的 build.ps1 把任何编译器 stderr 当作致命错误，会因此误判构建失败。 */
static RVA_MAYBE_UNUSED bool rva_group_ok(int group) {
    for (int i = 0; i < (int)RVA_ANCHOR_COUNT; ++i)
        if ((int)rva_anchors[i].group == group && rva_state[i] == RVA_ST_FAILED)
            return false;
    return true;
}

static RVA_MAYBE_UNUSED bool rva_all_ok(void) { return rva_failed_count == 0; }
static RVA_MAYBE_UNUSED int  rva_failed(void) { return rva_failed_count; }

/* native_hash_ok = GA 哈希命中白名单（构建未变）。
 * 返回「全部锚点可用」；调用方仍需用 rva_group_ok 做功能级放行。 */
static bool rva_reloc_init(bool native_hash_ok) {
    for (int i = 0; i < (int)RVA_ANCHOR_COUNT; ++i) {
        rva_slot[i] = (uintptr_t)rva_anchors[i].anchor;
        rva_state[i] = RVA_ST_PENDING;
    }
    rva_failed_count = 0;
    rva_scan_hits_total = 0;
    rva_collect_regions(ga);

    if (rva_region_count == 0) {
        RVA_LOG("reloc: no executable section found; keeping native RVAs");
        rva_mode = RVA_MODE_NATIVE;
    }
    if (rva_mode == RVA_MODE_AUTO)
        rva_mode = native_hash_ok ? RVA_MODE_NATIVE : RVA_MODE_ADAPTIVE;

    if (rva_mode == RVA_MODE_NATIVE) {
        for (int i = 0; i < (int)RVA_ANCHOR_COUNT; ++i) rva_state[i] = RVA_ST_NATIVE;
        rva_ready = true;
        RVA_LOG("reloc: NATIVE (build known; %d anchors used as recorded, zero scan)",
                RVA_ANCHOR_COUNT);
        return true;
    }

    RVA_LOG("reloc: %s engine; %d anchors; window=0x%X; stale-anchor offset=%+lld (game updated?)",
            rva_mode == RVA_MODE_SELFTEST ? "SELFTEST" : "ADAPTIVE",
            RVA_ANCHOR_COUNT, (unsigned)rva_window, (long long)rva_shot);
    rva_resolve_all();
    rva_ready = true;
    RVA_LOG("reloc: %d/%d anchors relocated; %d point(s) disabled; total candidate hits=%d",
            RVA_ANCHOR_COUNT - rva_failed_count, RVA_ANCHOR_COUNT, rva_failed_count,
            rva_scan_hits_total);
    return rva_all_ok();
}

#endif /* HSLR_RVA_RELOC_H */
