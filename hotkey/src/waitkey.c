/* Wait Hotkey for The Legend of Fancy Realm Remake (HSLR 2.2.4 / build 25767968).
 *
 * Adds a configurable keyboard shortcut for the character action menu buttons
 * ('待机' / Wait by default, bound to Space). Implemented the same way as the
 * audited reference Mod: an independent d3d11.dll proxy that hooks the Unity
 * main-thread input tick and drives the game's own UIButtonEx.InvokeClick
 * pipeline instead of synthesising input.
 *
 * What this mod does, in full:
 *   - reads the physical state of the keys named in hslr-wait-hotkey.ini
 *   - writes a log next to itself and, once, a default .ini if none exists
 *   - calls the game's own button click entry point for the requested action,
 *     only while the native action menu is confirmed actionable
 *   - v1.3.0 adds the optional auto-approach module (approach.h): a second,
 *     independent Mod entry ("hslr-auto-move") whose G-key toggle walks each
 *     acting unit toward the nearest enemy through the game's own move
 *     pipeline; any approach failure leaves the wait hotkey untouched
 * It installs no network code, starts no external process, reads no save file,
 * patches no game file on disk, and never touches the game's own save/load
 * shortcuts.
 *
 * Layout and the proxy/ABI spine follow the MIT-licensed reference
 * implementation by Doomsdayer (see LICENSE.txt). The action-menu gating and
 * dispatch in menu.h is carried over unchanged from that audited source.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#include "MinHook.h"
#include "exports.h"
#include "fingerprints.h"
#include "config.h"

#define WK_LOG_FILE L"hslr-wait-hotkey.log"
#define WK_CONFIG_FILE L"hslr-wait-hotkey.ini"
/* v2.0.0 整合版：本 DLL 升格为「伞形」主模块，战斗倍速（speedkey）与
 * 属性道具稳定加值（pinattr）作为卫星 DLL 随包分发、由本模块负责装载。 */
#define WK_VERSION "2.9.0"

/* Accepted GameAssembly.dll builds. Same pair the reference implementation
 * accepts: the stock build and the build patched by the unrelated
 * "nightmare randomiser" Mod.
 * 【v2.9.0】这份清单不再是「不命中就全停」的开关，而是「命中就走零开销的
 * NATIVE 恒等路径」。不命中时进入 ADAPTIVE 重定位（见 rva_reloc.h），
 * 只有连锚点都找不回来的功能才降级。 */
static const char *const wk_accepted_hashes[] = {
    "1fda3e3add16e97abfd8ced98c24de839f0b079db803ae6f5a7867800fe03f1e",
    "7333475e7176b1f7395c724470a80fffd08e706295ba58fbbf59f0105e6634f1",
    "6aea28ddb7951f5df240f2fecc8cb16a79a8ecd0eb1cdc2b1343798a02be198c",
    "d969e10b94b47d423c701c1dd5fa6986cc8c8c5774519a89b2605fe91285bc09",
    "b023545ec4bc3bc23bf6193e55bb61e8583173b9dc65b1dec2e452e580296b45"
};

static HMODULE self, ga;
static HANDLE logfile = INVALID_HANDLE_VALUE;

/* 本模块的 SHA256 白名单（known_build 用）。 */
#define HSLR_ACCEPTED_HASHES wk_accepted_hashes

/* 日志 / 长路径 / 构建校验 —— 四模块共用实现，见 shared/common.h。 */
#include "common.h"

/* Two independent game-side switches: the wait hotkey entry and the
 * auto-approach entry (approach.h). One proxy DLL, two Mod entries. */
static bool enabled_wait, enabled_ap, state_seen;
static WKConfig config;

#define PTR(p, o) (*(void **)((BYTE *)(p) + (o)))
#define VAL(t, p, o) (*(t *)((BYTE *)(p) + (o)))
/* 【v2.9.0 免版本跟进】所有经 FN 的直接调用都先过运行时重定位层 rva_of()：
 * 构建未变时恒等返回（零开销、零行为变化）；官方更新后用长掩码指纹在锚点
 * 附近窗口里找回真实地址；解析失败的锚点由各功能的门（wk_core_ok / wk_wait_ok）
 * 拦在上游，因此不会走到这里（rva_of 对失败点返回 0，绝不能真的被调用）。 */
#define FN(rva, ret, ...) ((ret(*)(__VA_ARGS__))((BYTE *)ga + rva_of(rva)))

/* The IL2CPP C-API entries menu.h needs. Resolved from GameAssembly once the
 * module is loaded; every class-name check goes through them. */
static const char *(*class_name)(void *);
static const char *(*class_ns)(void *);      /* il2cpp_class_get_namespace */
static void *(*class_image)(void *);         /* il2cpp_class_get_image */
static void (*class_init)(void *);           /* il2cpp_runtime_class_init */
/* 【2.2.0 排障】字段 API：运行时自带布局，免疫 Il2CppClass 结构体漂移 */
static void *(*ifn_class_get_fields)(void *, void **);
static const char *(*ifn_field_get_name)(void *);
static void *(*ifn_field_get_type)(void *);
static int (*ifn_field_get_offset)(void *);
static void (*ifn_field_static_get_value)(void *, void *);

/* 【v2.5.0 适配 build 25470727】用 IL2CPP C API 按名字解析类，取代随构建漂移的
 * metadata-usage 槽地址硬编码（老 0x55a1c00=Game、0x557d328=LogicMgr）。
 * 类名与构建无关：以后游戏更新只需刷新指纹表，这里不再改动。 */
static void *(*ifn_domain_get)(void);
static void **(*ifn_domain_assemblies)(void *, size_t *);
static void *(*ifn_assembly_image)(void *);
static void *(*ifn_class_from_name)(void *, const char *, const char *);

/* 按 namespace+name 在全部已装载程序集里找类；只缓存成功命中，
 * 失败返回 NULL、下一帧自动重试（调用方本就有 NULL 失败路径）。 */
static void *class_by_name(const char *ns, const char *name) {
    static const struct { const char *ns, *name; } wanted[] = {
        { "", "Game" }, { "", "LogicMgr" },
    };
    static void *cache[2];
    static int bound;
    if (!bound) {
        bound = 1;
        class_ns = (const char *(*)(void *))GetProcAddress(ga, "il2cpp_class_get_namespace");
        class_image = (void *(*)(void *))GetProcAddress(ga, "il2cpp_class_get_image");
        class_init = (void (*)(void *))GetProcAddress(ga, "il2cpp_runtime_class_init");
        ifn_class_get_fields = (void *(*)(void *, void **))GetProcAddress(ga, "il2cpp_class_get_fields");
        ifn_field_get_name = (const char *(*)(void *))GetProcAddress(ga, "il2cpp_field_get_name");
        ifn_field_get_type = (void *(*)(void *))GetProcAddress(ga, "il2cpp_field_get_type");
        ifn_field_get_offset = (int (*)(void *))GetProcAddress(ga, "il2cpp_field_get_offset");
        ifn_field_static_get_value =
            (void (*)(void *, void *))GetProcAddress(ga, "il2cpp_field_static_get_value");
        ifn_domain_get = (void *(*)(void))GetProcAddress(ga, "il2cpp_domain_get");
        ifn_domain_assemblies =
            (void **(*)(void *, size_t *))GetProcAddress(ga, "il2cpp_domain_get_assemblies");
        ifn_assembly_image = (void *(*)(void *))GetProcAddress(ga, "il2cpp_assembly_get_image");
        ifn_class_from_name =
            (void *(*)(void *, const char *, const char *))GetProcAddress(ga, "il2cpp_class_from_name");
    }
    for (unsigned i = 0; i < 2; i++)
        if (!strcmp(ns, wanted[i].ns) && !strcmp(name, wanted[i].name) && cache[i])
            return cache[i];
    void *result = NULL;
    if (ifn_domain_get && ifn_domain_assemblies && ifn_assembly_image && ifn_class_from_name) {
        void *domain = ifn_domain_get();
        size_t count = 0;
        void **asms = domain ? ifn_domain_assemblies(domain, &count) : NULL;
        for (size_t i = 0; asms && i < count && !result; i++) {
            void *img = ifn_assembly_image(asms[i]);
            if (img) result = ifn_class_from_name(img, ns, name);
        }
    }
    if (result)
        for (unsigned i = 0; i < 2; i++)
            if (!strcmp(ns, wanted[i].ns) && !strcmp(name, wanted[i].name))
                cache[i] = result;
    return result;
}

/* ------------------------------------------------------------------ */
/* 【v2.9.0 免版本跟进】运行时重定位层                                  */
/* ------------------------------------------------------------------ */
/* 把源码里写死的「上一次重解时的 RVA」翻译成当前版本的 RVA：
 *   NATIVE   GA 哈希命中白名单（构建没变）→ 恒等映射，行为与旧版完全一致；
 *   ADAPTIVE 官方更新了 → 用采样自上一版的长掩码指纹，在锚点 ±256 KB 窗口内
 *            做唯一命中搜索；找不到/不唯一的点只降级依赖它的功能。
 * 锚点表 rva_anchors.h 由 tools/analyze/capture_fingerprints.py 生成。
 * 分组：0 = 核心（输入钩 / 游戏内 Mod 开关 / 帧计数），1 = approach，2 = wait 菜单。
 * 详细设计见 hotkey/docs/免版本跟进-兼容方案.md §10。 */
#define RVA_LOG logmsg
#include "rva_reloc.h"

/* The metadata resolver accepts the address of an encoded or resolved slot and
 * resolves metadata without forcing the game's business static constructor.
 * Low addresses are valid allocations and must not be rejected by magnitude. */
static void *resolved_class(uintptr_t slot) {
    FN(0x536FB0, void, void *)((BYTE *)ga + slot);
    void *c = PTR(ga, slot);
    return c && !((uintptr_t)c & 7) ? c : NULL;
}
static void *ready_class_slot(uintptr_t slot) {
    void *c = resolved_class(slot);
    return c && VAL(int, c, 0xe4) != 0 ? c : NULL;
}

/* menu.h + approach.h live here because check_fingerprints below already
 * references the approach fingerprint table and its ap_fp_ok flag. menu.h
 * needs everything defined above (ga, PTR/VAL/FN, ready_class_slot,
 * class_name, logmsg, config) and nothing defined below. */
#include "menu.h"
#include "approach.h"

/* ------------------------------------------------------------------ */
/* Build identity                                                      */
/* ------------------------------------------------------------------ */

/* 【流程 3b】门：构建未变 → 逐点字节校验（与 v2.8.0 完全相同）；
 * 构建未知 → 靠运行时重定位结果逐点放行/降级（方案 C）。
 *
 * 【v2.9.0 免版本跟进】原逻辑是「任一处指纹不符就整体停机（fail-closed）」。
 * 那条规则在官方更新时会自伤：哈希不认识的第一秒，四个模块一起停，玩家看到
 * 的就是「全部失效」——官方并没有针对我们，是我们自己的闸门。现在改成：
 *   NATIVE  构建未变 → 保持原行为，零回归；
 *   ADAPTIVE 官方更新 → 每个锚点要么被唯一命中找回、要么判失败；失败点只降级
 *            依赖它的功能，其余功能照常。
 * 两种模式下的共同不变量：**被放行的功能，其每个地址都通过了校验。** */

/* 重定位后的门状态（仅 ADAPTIVE/SELFTEST 有意义；NATIVE 下恒为 true）。 */
static bool wk_core_ok = true;   /* 核心锚点（输入钩 / Mod 开关 / 帧计数）全可用 */
static bool wk_wait_ok = true;   /* wait 菜单热键所需锚点全可用 */

static bool verify_native_fingerprints(void) {
    bool wait_ok = true, ap_ok = true;
    for (unsigned i = 0; i < sizeof(wk_fingerprints) / sizeof(wk_fingerprints[0]); i++) {
        const WKFingerprint *f = &wk_fingerprints[i];
        if (memcmp((BYTE *)ga + f->rva, f->bytes, 16)) {
            logmsg("Fingerprint mismatch %s RVA=%llx", f->name, (unsigned long long)f->rva);
            wait_ok = false;
        }
    }
    for (unsigned i = 0; i < sizeof(ap_fingerprints) / sizeof(ap_fingerprints[0]); i++) {
        const APFingerprint *f = &ap_fingerprints[i];
        if (memcmp((BYTE *)ga + f->rva, f->bytes, 16)) {
            logmsg("Approach fingerprint mismatch %s RVA=%llx", f->name, (unsigned long long)f->rva);
            ap_ok = false;
        }
    }
    if (!wait_ok) return false;
    ap_fp_ok = ap_ok;   /* approach stays off until ap_il2cpp_resolve() (game thread) succeeds */
    logmsg("%u wait fingerprints verified; approach table %s",
           (unsigned)(sizeof(wk_fingerprints) / sizeof(wk_fingerprints[0])),
           ap_ok ? "verified" : "MISMATCHED (approach disabled)");
    return true;
}

/* 两张表分级：wait 表失败 = 整个 Mod 失效；approach 表失败 = 仅自动接近
 * 停用（ap_runtime_ok=false），待机热键照常。
 * Java 类比：调用下游前先做契约校验（consumer-driven contract test），
 * 契约不符拒绝上线——避免按错误偏移调用游戏函数这类"野指针"事故。 */
static bool check_fingerprints(void) {
    if (rva_mode == RVA_MODE_NATIVE) {
        wk_core_ok = true;
        wk_wait_ok = true;
        return verify_native_fingerprints();
    }
    wk_core_ok = rva_group_ok(0);
    wk_wait_ok = rva_all_ok();                       /* 0 && 1 && 2 */
    /* approach 与 wait 共用同一个按键读取器（Keyboard/Button 锚点在 wait 组），
     * 所以它同样要求全部锚点可用 —— 宁可整体不启用，也不按错地址读键。 */
    ap_fp_ok = rva_all_ok();
    logmsg("reloc gate: core=%d wait=%d approach=%d (%d of %u anchor(s) unlocated)",
           wk_core_ok ? 1 : 0, wk_wait_ok ? 1 : 0, ap_fp_ok ? 1 : 0,
           rva_failed(), (unsigned)RVA_ANCHOR_COUNT);
    if (!wk_core_ok) {
        logmsg("reloc: core anchors unavailable (input tick / in-game mod switch / "
               "frame counter); Mod inactive");
        return false;
    }
    if (!wk_wait_ok)
        logmsg("reloc: wait-hotkey anchors incomplete; the wait menu hotkey is disabled, "
               "but the satellites (speed / pinattr / goldhand) keep running");
    return true;
}

/* menu.h identifies classes through the IL2CPP C API. Resolving it is a
 * hard prerequisite: an unresolved slot would be called as a null pointer on
 * the first menu frame. */
static bool il2cpp_ready(void) {
    class_name = (const char *(*)(void *))(void *)GetProcAddress(ga, "il2cpp_class_get_name");
    if (!class_name) logmsg("Missing il2cpp_class_get_name; Mod inactive");
    return class_name != NULL;
}

/* ------------------------------------------------------------------ */
/* Mod enable switch (owned by the game's own Mod manager)             */
/* ------------------------------------------------------------------ */

/* The manifest's modId is an IL2CPP UTF-16 string: length at +0x10, chars at
 * +0x14. Compared against the id named in the .ini, which the installer keeps in
 * sync with the mod.json it installs. */
static bool id_matches2(void *s, const WCHAR *id, int chars) {
    if (!s || chars <= 0) return false;
    int slen = VAL(int, s, 0x10);
    if (slen == chars)
        return !memcmp((BYTE *)s + 0x14, id, (size_t)chars * 2);
    /* 【v2.5.2 工坊条目】游戏把工坊订阅条目的主键写成 "<modId>@<文件ID>"
     * （实测 mod_profile.json：待机快捷键@3802488917）。全等匹配永远打不中
     * 工坊条目 → 订阅玩家的四项功能全关。放宽为：前缀相同且紧跟 '@'。 */
    if (slen > chars) {
        const WCHAR *p = (const WCHAR *)((BYTE *)s + 0x14);
        return !memcmp(p, id, (size_t)chars * 2) && p[chars] == L'@';
    }
    return false;
}

static bool id_matches(void *s) {
    return id_matches2(s, config.mod_id, config.mod_id_chars);
}

static bool id_matches_approach(void *s) {
    return id_matches2(s, config.mod_id_approach, config.mod_id_approach_chars);
}

/* 【v2.5.4 modId 自愈白名单】主 mod_id 在游戏条目列表里一个都对不上时，按这
 * 些已知 ID 兜底（与三卫星 v2.3.0 同一机制）：
 *   - 「待机快捷键」：无 ini 的工坊订阅玩家（内置默认即此值，双保险）；
 *   - 「hslr-wait-hotkey」：旧版默认英文 ID——工坊目录可写的玩家在 v2.5.2
 *     首次运行时已把英文 ID 写进 ini，Steam 更新包不清理用户文件，残留至今。 */
#define WK_FALLBACK_IDS 2
static WCHAR wk_fallback_ids[WK_FALLBACK_IDS][WK_MOD_ID_MAX];
static int wk_fallback_chars[WK_FALLBACK_IDS];
static bool wk_fallback_ready;
static bool wk_fallback_active;   /* 当前是否正用回退 ID（供日志只提示一次） */

static void wk_fallback_init(void) {
    static const char *const ids[WK_FALLBACK_IDS] = {
        WK_DEFAULT_MOD_ID_UTF8,   /* 待机快捷键 */
        "hslr-wait-hotkey",       /* 旧版默认英文 ID：残留 ini 的玩家 */
    };
    for (int i = 0; i < WK_FALLBACK_IDS; ++i) {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ids[i], -1,
                                    wk_fallback_ids[i], WK_MOD_ID_MAX - 1);
        /* 返回值含结尾 NUL；ASCII 源不可能失败，中文源失败则该 ID 缺席。 */
        wk_fallback_chars[i] = (n > 0) ? n - 1 : 0;
    }
    wk_fallback_ready = true;
}

static bool wk_fallback_matches(void *s) {
    if (!wk_fallback_ready || !s) return false;
    int slen = VAL(int, s, 0x10);
    if (slen <= 0 || slen >= WK_MOD_ID_MAX) return false;
    for (int i = 0; i < WK_FALLBACK_IDS; ++i)
        if (wk_fallback_chars[i] == slen &&
            !memcmp((BYTE *)s + 0x14, wk_fallback_ids[i], (size_t)slen * 2))
            return true;
    return false;
}

/* Queried on Unity's main input tick, never from the worker thread.
 * One pass over the game's mod list resolves both feature switches:
 * the wait hotkey entry and the auto-approach entry. */
static void read_mods(void) {
    static int last_reason = -1, last_modflag = -2, last_compatible = -2, last_errors = -2;
    static int last_ap_on = -1, last_ap_reason = -1;
    static const char *reasons[] = {"enabled", "mod-manager-null", "mod-list-null",
        "invalid-mod-list", "mod-id-not-found", "mod-disabled", "mod-incompatible",
        "mod-validation-items"};
    if (config.force_enable) {
        if (!state_seen) logmsg("force_enable=1; ignoring the in-game Mod switches");
        state_seen = true;
        enabled_wait = true;
        enabled_ap = true;
        return;
    }
    bool next = false, next_ap = false;
    int reason = 1, modflag = -1, compatible = -1, errors = -1;
    int ap_reason = 1;
    int seen_count = 0;
    char seen_ids[400] = "";
    void *fb_mod = NULL;   /* 【v2.5.4 自愈】白名单 ID 命中的条目（仅 reason==4 时采用） */
    void *t = FN(0xC76810, void *, void *)(NULL);
    if (t) {
        void *l = PTR(t, 0x18);
        reason = 2;
        ap_reason = 2;
        if (l) {
            int n = VAL(int, l, 0x18);
            void *a = PTR(l, 0x10);
            reason = 3;
            ap_reason = 3;
            if (a && n >= 0 && n < 4096) {
                reason = 4;
                ap_reason = 4;
                for (int i = 0; i < n; i++) {
                    void *mod = PTR(a, 0x20 + i * 8);
                    if (!mod) continue;
                    void *man = PTR(mod, 0x10);
                    /* While the configured id has not been matched yet, remember
                     * what the game does have — that line turns "wrong modId"
                     * into a one-glance diagnosis from a remote log. */
                    if (!man) continue;
                    void *sid = PTR(man, 0x10);
                    int slen = sid ? VAL(int, sid, 0x10) : 0;
                    if (slen > 0 && slen <= 64 && seen_count < 8 &&
                        strlen(seen_ids) + slen * 3 + 4 < sizeof(seen_ids)) {
                        /* IL2CPP strings carry an explicit length and are not
                         * NUL-terminated; copy exactly slen chars first. */
                        WCHAR wtmp[65];
                        memcpy(wtmp, (BYTE *)sid + 0x14, (size_t)slen * 2);
                        wtmp[slen] = 0;
                        char one[196];
                        wide_to_utf8(wtmp, one, (int)sizeof(one));
                        seen_count++;
                        if (seen_count > 1) strncat(seen_ids, ", ", sizeof(seen_ids) - strlen(seen_ids) - 1);
                        strncat(seen_ids, one, sizeof(seen_ids) - strlen(seen_ids) - 1);
                    }
                    if (!fb_mod && wk_fallback_matches(sid)) fb_mod = mod;
                    if (id_matches(sid)) {
                        void *err = PTR(mod, 0x28);
                        int mf = VAL(BYTE, mod, 0x20), cp = VAL(BYTE, mod, 0x34);
                        int er = (mf && cp) ? (err ? VAL(int, err, 0x18) : 0) : -1;
                        bool on = mf && cp && er == 0;
                        /* 【v2.5.2】本地与工坊条目可能同时命中（modId / modId@文件ID）：
                         * 任一启用即算开；还没遇到启用条目时记住当前命中的状态。 */
                        if (on || !next) {
                            modflag = mf; compatible = cp; errors = er;
                            next = on;
                            reason = on ? 0 : !mf ? 5 : !cp ? 6 : 7;
                        }
                        continue;   /* keep scanning: the approach entry may follow */
                    }
                    if (id_matches_approach(sid)) {
                        void *err = PTR(mod, 0x28);
                        int mf = VAL(BYTE, mod, 0x20), cp = VAL(BYTE, mod, 0x34);
                        int er = (mf && cp) ? (err ? VAL(int, err, 0x18) : 0) : -1;
                        next_ap = mf && cp && er == 0;
                        ap_reason = next_ap ? 0 : !mf ? 5 : !cp ? 6 : 7;
                    }
                }
            }
        }
    }
    /* 【v2.5.4 自愈】主 mod_id 在完整条目列表里一个都没匹配上、但存在白名单
     * ID 的条目 → 按那个条目的开关状态生效（无 ini 的工坊玩家 / 残留旧英文
     * ini 的玩家免重装自愈；与三卫星 v2.3.0 机制对齐）。approach 已废弃，
     * 不参与自愈。 */
    bool used_fallback = false;
    if (reason == 4 && modflag == -1 && fb_mod) {
        void *err = PTR(fb_mod, 0x28);
        modflag = VAL(BYTE, fb_mod, 0x20);
        compatible = VAL(BYTE, fb_mod, 0x34);
        errors = (modflag && compatible) ? (err ? VAL(int, err, 0x18) : 0) : -1;
        next = modflag && compatible && errors == 0;
        reason = next ? 0 : !modflag ? 5 : !compatible ? 6 : 7;
        used_fallback = true;
    }
    if (!state_seen || next != enabled_wait || reason != last_reason || modflag != last_modflag ||
        compatible != last_compatible || errors != last_errors) {
        state_seen = true;
        enabled_wait = next;
        last_reason = reason;
        last_modflag = modflag;
        last_compatible = compatible;
        last_errors = errors;
        char idbuf[WK_MOD_ID_MAX * 4];
        wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
        logmsg("Wait-hotkey mod %s; id=%s; reason=%s; enabled=%d compatible=%d validationItems=%d",
               enabled_wait ? "enabled" : "disabled", idbuf, reasons[reason], modflag, compatible, errors);
        if (used_fallback && !wk_fallback_active)
            logmsg("FALLBACK: ini mod_id \"%s\" matches no game entry; using the "
                   "built-in id instead (stale hslr-wait-hotkey.ini?)", idbuf);
        wk_fallback_active = used_fallback;
        if (reason == 4)
            logmsg("Game Mod entries: %d seen; ids: %s", seen_count,
                   seen_ids[0] ? seen_ids : "(none readable)");
        else if (seen_count > 0)
            /* 【v2.5.2 取证】闸门状态每次变化都转储实际见到的条目 ID，
             * 工坊条目的真实键名（如 待机快捷键@3802488917）一眼可查。 */
            logmsg("Game Mod entries: %d seen; ids: %s", seen_count, seen_ids);
    }
    /* The switch value itself must update every frame — approach_tick and the
     * was_ap cleanup in hk_input_update read the live flag, not the log. */
    enabled_ap = next_ap;
    /* Log on a real transition (on/off or reason change) only — comparing the
     * reason code against the on/off flag instead would re-log every frame. */
    if (next_ap != (last_ap_on > 0) || ap_reason != last_ap_reason) {
        char idbuf[WK_MOD_ID_MAX * 4];
        wide_to_utf8(config.mod_id_approach, idbuf, sizeof(idbuf));
        logmsg("Approach mod %s; id=%s; reason=%s", next_ap ? "enabled" : "disabled",
               idbuf, reasons[ap_reason]);
        last_ap_on = next_ap ? 1 : 0;
        last_ap_reason = ap_reason;
    }
}

/* ------------------------------------------------------------------ */
/* Physical key reader (Unity Input System, main thread only)          */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned actions; /* bit N = action N requested by a configured key */
    bool shift, ctrl, alt, meta, available;
} WKKeyState;

/* Set once the reader has polled a frame; lets a key that is already held when
 * the menu appears register as a press, while a press made before the reader
 * was primed cannot be replayed later. */
static bool key_primed;

static bool modifier_held(uintptr_t ga_, void *keyboard, int key) {
    if (!keyboard) return false;
    void *control = ((void *(*)(void *, int, void *))(ga_ + rva_of(0x39F3E20)))(keyboard, key, 0);
    return control && ((bool (*)(void *, void *))(ga_ + rva_of(0x39A2A20)))(control, 0);
}

/* Poll once per game frame. passing focused=false clears the reader so an
 * unsafe press cannot become a deferred action when focus returns. */
static WKKeyState keys_poll(uintptr_t ga_, bool focused) {
    WKKeyState out = {0};
    if (!focused) {
        key_primed = false;
        return out;
    }
    void *keyboard = ((void *(*)(void *))(ga_ + rva_of(0x39F3FB0)))(0);
    if (!keyboard) {
        key_primed = false;
        return out;
    }
    out.available = true;
    unsigned actions = 0;
    for (int i = 0; i < config.count; ++i) {
        int code = config.bindings[i].key;
        void *control = ((void *(*)(void *, int, void *))(ga_ + rva_of(0x39F3E20)))(keyboard, code, 0);
        if (!control) continue;
        bool held = ((bool (*)(void *, void *))(ga_ + rva_of(0x39A2A20)))(control, 0);
        bool edge = ((bool (*)(void *, void *))(ga_ + rva_of(0x39A2AF0)))(control, 0);
        if (held || (edge && key_primed)) actions |= 1u << config.bindings[i].action;
    }
    out.shift = modifier_held(ga_, keyboard, 51) || modifier_held(ga_, keyboard, 52);
    out.alt = modifier_held(ga_, keyboard, 53) || modifier_held(ga_, keyboard, 54);
    out.ctrl = modifier_held(ga_, keyboard, 55) || modifier_held(ga_, keyboard, 56);
    out.meta = modifier_held(ga_, keyboard, 57) || modifier_held(ga_, keyboard, 58);
    key_primed = true;
    out.actions = actions;
    return out;
}

/* ------------------------------------------------------------------ */
/* Config hot reload                                                  */
/* ------------------------------------------------------------------ */

static WCHAR config_path[HSLR_PATH_MAX];
static FILETIME config_stamp;
static uint64_t config_checked_at;
static bool config_seen_once;

static void config_remember_stamp(void) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(config_path, GetFileExInfoStandard, &fad)) {
        config_stamp = fad.ftLastWriteTime;
        config_seen_once = true;
    } else {
        ZeroMemory(&config_stamp, sizeof(config_stamp));
        config_seen_once = false;
    }
}

static void config_log_summary(bool reload) {
    char idbuf[WK_MOD_ID_MAX * 4], apidbuf[WK_MOD_ID_MAX * 4];
    wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
    wide_to_utf8(config.mod_id_approach, apidbuf, sizeof(apidbuf));
    logmsg("Configuration %s: %d binding(s), requireNoModifier=%d, forceEnable=%d, verboseLog=%d, modId=%s, approachId=%s, apPathRange=%d, parseErrors=%d",
           reload ? "reloaded" : "loaded", config.count, config.require_no_modifier ? 1 : 0,
           config.force_enable ? 1 : 0, config.verbose_log ? 1 : 0, idbuf, apidbuf,
           config.ap_path_range, config.errors);
    /* 【v2.3.0】分功能总控状态随配置摘要一起打日志，远程排障一眼可见。
     * speed/pinattr 两键伞形不消费，但同样打出来——卫星侧的日志会各自
     * 记录自己读到的值，两边可对账。 */
    logmsg("Features: wait=%d approach=%d speed=%d pinattr=%d",
           config.feat_wait ? 1 : 0, config.feat_approach ? 1 : 0,
           config.feat_speed ? 1 : 0, config.feat_pinattr ? 1 : 0);
    for (int i = 0; i < config.count; ++i)
        logmsg("  binding: %s -> %s", config.bindings[i].key_name, config.bindings[i].action_name);
}

static void config_setup(void) {
    path_for(config_path, WK_CONFIG_FILE);
    if (GetFileAttributesW(config_path) == INVALID_FILE_ATTRIBUTES) {
        if (wk_config_write_default(config_path)) {
            logmsg("Wrote the default configuration next to the Mod DLL");
        } else {
            logmsg("Could not create default configuration; using built-in defaults");
        }
    }
    if (!wk_config_load(&config, config_path)) {
        logmsg("Configuration unreadable or empty; using built-in defaults (Space -> wait)");
        wk_config_defaults(&config);
    }
    config_log_summary(false);
    config_remember_stamp();
    config_checked_at = GetTickCount64();
}

/* Cheap: a stat call at most once per second. */
static void config_maybe_reload(void) {
    uint64_t now = GetTickCount64();
    if (now - config_checked_at < 1000) return;
    config_checked_at = now;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(config_path, GetFileExInfoStandard, &fad)) {
        if (config_seen_once) {
            config_seen_once = false;
            logmsg("Configuration file disappeared; keeping the last loaded bindings");
        }
        return;
    }
    if (config_seen_once && !memcmp(&fad.ftLastWriteTime, &config_stamp, sizeof(FILETIME))) return;
    WKConfig candidate;
    if (wk_config_load(&candidate, config_path)) {
        config = candidate;
        config_log_summary(true);
        config_remember_stamp();
    } else {
        logmsg("Configuration reload failed or empty; keeping the previous bindings");
        config_remember_stamp();
    }
}

/* ------------------------------------------------------------------ */
/* Hooks                                                              */
/* ------------------------------------------------------------------ */

static uint32_t last_input_frame;
static bool input_frame_seen;
static void (*orig_input_update)(void *, void *);
static void (*orig_input_focus)(void *, bool, void *);

/* 【v2.0.3】输入钩独占的配套面：卫星不再自己钩输入对（三个独立 MinHook 实例
 * 抢同一目标地址，只有最后写入者的 detour 存活——v2.0.2 实机实锤，待机/倍速
 * 全部静默失效）。伞形的输入钩子每帧代为调用卫星导出的 tick，焦点变化转发
 * 给卫星的 focus 回调。任一导出缺失只降级该卫星功能并记日志。 */
static void (*sk_frame_tick_fn)(void);
static void (*sk_input_focus_fn)(void);
static void (*pa_frame_tick_fn)(void);
static void (*pa_input_focus_fn)(int);
/* 【v2.4.0 新增】金之手卫星：数据钳值 Mod，无按键，只要每帧 tick。 */
static void (*gh_frame_tick_fn)(void);
static void (*gh_input_focus_fn)(int);

/* 【v2.4.1 血泪教训】卫星每帧驱动表：新增卫星只需把 tick 指针加进这张表，
 * 就不会再出现「装载成功、日志写着 frame tick ok、却从来没人调用」的哑火。
 * v2.4.0 金之手就是因为手写 if 链漏掉一行，整版功能静默失效（实机定案）。 */
static void (**const g_sat_ticks[])(void) = {
    &sk_frame_tick_fn, &pa_frame_tick_fn, &gh_frame_tick_fn,
};

static void drive_satellites(void) {
    for (unsigned i = 0; i < sizeof(g_sat_ticks) / sizeof(g_sat_ticks[0]); ++i)
        if (*g_sat_ticks[i]) (*g_sat_ticks[i])();
}

static void hk_input_focus(void *input, bool focus, void *method) {
    orig_input_focus(input, focus, method);
    key_primed = false;
    /* 【v2.0.3】输入钩独占：焦点变化转发给卫星 */
    if (sk_input_focus_fn) sk_input_focus_fn();
    if (pa_input_focus_fn) pa_input_focus_fn(focus ? 1 : 0);
    if (gh_input_focus_fn) gh_input_focus_fn(focus ? 1 : 0);
}

static bool process_focused(void) {
    DWORD pid = 0;
    HWND window = GetForegroundWindow();
    if (!window) return false;
    GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}

static unsigned action_label(unsigned held7) {
    unsigned index = 0;
    while (!(held7 & (1u << index))) ++index;
    return index;
}

static void hk_input_update(void *input, void *method) {
    orig_input_update(input, method);
    uint32_t frame = FN(0x3FFA550, uint32_t, void *)(NULL);
    if (input_frame_seen && frame == last_input_frame) return;
    input_frame_seen = true;
    last_input_frame = frame;

    config_maybe_reload();
    /* 【v2.3.0 分功能总控】功能有效开关 = 游戏内条目开关 && ini [features]。
     * 翻转检测（menu_reset/ap_cleanup）改看「有效开关」的下降沿——ini 里
     * 关掉 wait/approach 也要立刻收起菜单状态/清掉接近路径，与游戏内
     * 关开关同一待遇。enabled_wait/enabled_ap 保持游戏内开关的纯语义。 */
    static bool prev_feat_wait = true, prev_feat_ap = true;
    read_mods();
    bool feat_wait = enabled_wait && config.feat_wait;
    bool feat_ap = enabled_ap && config.feat_approach;
    bool was_wait = prev_feat_wait, was_ap = prev_feat_ap;
    prev_feat_wait = feat_wait;
    prev_feat_ap = feat_ap;
    if (was_wait && !feat_wait) menu_reset();
    if (was_ap && !feat_ap) ap_cleanup();
    /* 【v2.0.3】输入钩独占：卫星的每帧逻辑（含各自的 modId 闸门）由这里代为驱动 */
    drive_satellites();

    /* 【v2.9.0】热键锚点没全部找回时，keys_poll 里的 Keyboard/Button 地址是未
     * 解析的（rva_of 对失败点返回 0）——只驱动卫星，不读键、不进菜单。 */
    if (!wk_wait_ok) return;

    /* Poll every frame, even while disabled, so the reader stays primed and a
     * press cannot be replayed once the Mod is switched on. */
    bool focused = process_focused();
    WKKeyState keys = keys_poll((uintptr_t)ga, focused);

    /* Auto-approach toggle: bit 7 of the action vector (binding action 7).
     * Fully independent of the wait pipeline — own Mod switch, own key, and
     * require_no_modifier gates the toggle exactly like the wait keys. */
    bool ap_toggle = false;
    if (keys.available && ((keys.actions >> 7) & 1u)) {
        bool modifier = keys.shift || keys.ctrl || keys.alt || keys.meta;
        ap_toggle = !(config.require_no_modifier && modifier);
    }
    ap_resolve_tick();   /* 【v2.0.2】懒解析：只在游戏线程（本钩子）碰 IL2CPP API */
    approach_tick(input, focused, feat_ap, ap_toggle);

    if (!feat_wait || !focused || !keys.available) return;
    if (!keys.actions) return;
    if (config.require_no_modifier && (keys.shift || keys.ctrl || keys.alt || keys.meta)) {
        if (config.verbose_log)
            logmsg("Hotkey ignored: a modifier key is held (require_no_modifier=1)");
        return;
    }
    unsigned held7 = keys.actions & 0x7F;
    if (!held7) return;   /* approach-only press: bit 7 belongs to approach.h */
    if (config.verbose_log) {
        unsigned index = action_label(held7);
        logmsg("Hotkey request: actions=%#x priorityAction=%u", held7, index == 6 ? 0 : index + 1);
    }
    menu_tick(input, held7, false);
}

static bool addhook(uintptr_t rva, void *fn, void **original) {
    /* 【v2.9.0】先过重定位层。解析失败的点返回 0：宁可少装一个钩子，
     * 绝不按错误地址挂钩（v2.7.0 事故的形态就是装错地址后自洽地闪退）。 */
    uintptr_t at = rva_of(rva);
    if (!at) {
        logmsg("Hook skipped: anchor RVA=%llx could not be relocated", (unsigned long long)rva);
        return false;
    }
    if (at != rva)
        logmsg("Hook anchor RVA=%llx relocated to %llx", (unsigned long long)rva,
               (unsigned long long)at);
    MH_STATUS status = MH_CreateHook((BYTE *)ga + at, fn, original);
    if (status != MH_OK) {
        logmsg("Hook creation failed RVA=%llx status=%d", (unsigned long long)at, status);
        return false;
    }
    return true;
}

/* 【v2.0.0 伞形装载】战斗倍速与属性加值以卫星 DLL 随包放在本 DLL 同目录
 * （mod-speedkey.dll / mod-pinattr.dll，代码即原独立 Mod，各自做指纹校验、
 * 装自己的钩子、读自己目录里的 hslr-speed-hotkey.ini / hslr-pinattr.ini）。
 *
 * 时机与安全：
 *   - 只在 worker 线程里 LoadLibrary（绝不进 DllMain，加载器锁铁律）；
 *   - 卫星缺席（老包升级场景）只记日志、静默跳过；装载失败只损失对应
 *     功能，绝不影响待机快捷键本身；
 *   - 本模块自身指纹校验失败时仍会装载卫星 —— 三方指纹表各不相同，
 *     卫星能过就还能用，互不拖累；
 *   - 卫星与本模块钩同一对输入函数（0x65A4E0/0x657A40），MinHook 跳板
 *     按创建顺序自然链式转发，与多 Mod 各装一份时的现状行为一致。
 * Java 类比：主应用启动完成后按需装载可选模块，模块失败不回滚主应用。 */
/* 【v2.0.3】卫星指针的声明在输入钩子区（sk_frame_tick_fn 等），此处只负责装载。 */
static void load_satellites(void) {
    static const struct { const WCHAR *file; const char *label; } sats[] = {
        {L"mod-speedkey.dll", "battle-speed"},
        {L"mod-pinattr.dll",  "pinattr"},
        {L"mod-goldhand.dll", "gold-hand"},
    };
    WCHAR path[HSLR_PATH_MAX];
    for (size_t i = 0; i < sizeof(sats) / sizeof(sats[0]); ++i) {
        path_for(path, sats[i].file);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            logmsg("Satellite %s: %ls not found next to the DLL; feature not bundled",
                   sats[i].label, sats[i].file);
            continue;
        }
        /* LoadLibraryW 计入引用计数；进程生命周期内不卸载（卸载会撤走卫星
         * 已装的 MinHook 钩子，等于自拆跳板，因此故意无对应 FreeLibrary）。 */
        HMODULE mod = LoadLibraryW(path);
        if (!mod) {
            logmsg("Satellite %s load FAILED err=%lu; feature unavailable",
                   sats[i].label, (unsigned long)GetLastError());
            continue;
        }
        if (!strcmp(sats[i].label, "battle-speed")) {
            sk_frame_tick_fn = (void (*)(void))GetProcAddress(mod, "sk_frame_tick");
            sk_input_focus_fn = (void (*)(void))GetProcAddress(mod, "sk_input_focus");
            logmsg("Satellite %s loaded: %ls (frame tick %s)",
                   sats[i].label, sats[i].file, sk_frame_tick_fn ? "ok" : "MISSING");
        } else if (!strcmp(sats[i].label, "gold-hand")) {
            /* 【v2.4.0 新增】金之手卫星（数据钳值，导出 gh_frame_tick/gh_input_focus） */
            gh_frame_tick_fn = (void (*)(void))GetProcAddress(mod, "gh_frame_tick");
            gh_input_focus_fn = (void (*)(int))GetProcAddress(mod, "gh_input_focus");
            logmsg("Satellite %s loaded: %ls (frame tick %s)",
                   sats[i].label, sats[i].file, gh_frame_tick_fn ? "ok" : "MISSING");
        } else {
            pa_frame_tick_fn = (void (*)(void))GetProcAddress(mod, "pa_frame_tick");
            pa_input_focus_fn = (void (*)(int))GetProcAddress(mod, "pa_input_focus");
            logmsg("Satellite %s loaded: %ls (frame tick %s)",
                   sats[i].label, sats[i].file, pa_frame_tick_fn ? "ok" : "MISSING");
        }
    }
    /* 【v2.4.1】把「已接线的 tick 数」也记进日志：这条数字对不上卫星数，
     * 就是又有人只装了指针没接驱动表（v2.4.0 金之手哑火的形态）。 */
    unsigned wired = 0;
    for (unsigned i = 0; i < sizeof(g_sat_ticks) / sizeof(g_sat_ticks[0]); ++i)
        if (*g_sat_ticks[i]) ++wired;
    logmsg("Satellite frame ticks wired: %u/%u (driven every frame from the input hook)",
           wired, (unsigned)(sizeof(g_sat_ticks) / sizeof(g_sat_ticks[0])));
}

static DWORD WINAPI worker(void *unused) {
    (void)unused;
    hslr_log_open(WK_LOG_FILE);
    logmsg("HSLR Wait Hotkey + Auto-Approach %s; umbrella d3d11.dll "
           "(all-in-one: + battle speed + pinattr + gold hand); game 2.2.4 / build 25767968",
           WK_VERSION);
    hslr_log_self_path();
    /* 卫星装载放在本模块一切自检之前：三方指纹表各不相同，本模块校验
     * 失败不应连坐卫星能过的那份。装载结果只写日志，不影响后续流程。 */
    load_satellites();
    config_setup();
    wk_fallback_init();   /* 【v2.5.4】modId 自愈白名单（静态 UTF-8 → UTF-16，绝不失败到空） */
    int build_ok = known_build();   /* 1 = 白名单命中；0 = 官方更新了；-1 = 读不到 */
    if (build_ok < 0) {
        logmsg("GameAssembly.dll unreadable; Mod inactive and nothing is hooked");
        return 0;
    }
    for (int i = 0; i < 600 && !ga; i++) {
        ga = GetModuleHandleW(L"GameAssembly.dll");
        if (!ga) Sleep(100);
    }
    if (!ga) {
        logmsg("GameAssembly not loaded; Mod inactive");
        return 0;
    }
    /* 【v2.9.0 免版本跟进】重定位层必须在**任何** FN/addhook 之前就绪：它决定
     * rva_of() 是恒等映射还是窗口搜索。配置读伞形 ini（四个模块共享同一份
     * rva_mode / rva_window / rva_shot）。 */
    rva_reloc_configure(config_path);
    rva_reloc_init(build_ok == 1);
    if (!check_fingerprints()) {
        logmsg("Anchor gate failed; Mod inactive and nothing is hooked");
        return 0;
    }
    if (!il2cpp_ready()) return 0;
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK) {
        logmsg("MinHook initialization failed status=%d; Mod inactive", init_status);
        return 0;
    }
    if (rva_mode == RVA_MODE_NATIVE) {
        /* 构建未变：保持 v2.8.0 的原始行为 —— 三条钩子任一失败即整体放弃。 */
        if (!addhook(0x65A4E0, hk_input_update, (void **)&orig_input_update) ||
            !addhook(0x657A40, hk_input_focus, (void **)&orig_input_focus) ||
            !addhook(0x93B290, hk_menu_show, (void **)&orig_menu_show) ||
            MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            logmsg("Hook installation failed; Mod inactive");
            return 0;
        }
    } else {
        /* 【v2.9.0 单点降级】只为解析成功的锚点挂钩。输入帧钩是卫星唯一的
         * 驱动源，缺它就没意义；其余两个缺哪个只损失对应能力。 */
        if (!addhook(0x65A4E0, hk_input_update, (void **)&orig_input_update)) {
            logmsg("reloc: input-tick anchor unavailable; Mod inactive");
            MH_Uninitialize();
            return 0;
        }
        if (!addhook(0x657A40, hk_input_focus, (void **)&orig_input_focus))
            logmsg("reloc: focus anchor unavailable; focus changes are no longer forwarded");
        if (!addhook(0x93B290, hk_menu_show, (void **)&orig_menu_show))
            logmsg("reloc: AMenu-show anchor unavailable; menu-open tracking degraded");
        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            logmsg("reloc: MH_EnableHook failed; Mod inactive");
            return 0;
        }
    }
    /* 【2.2.0 适配】LogicMgr$$Update 实例捕获 hook：非致命，失败只退化菜单门。
     * 注意它在 MH_EnableHook(MH_ALL_HOOKS) 之后创建，必须显式 Enable。 */
    uintptr_t lg_rva = rva_of(0x668DD0);
    MH_STATUS lg_st = lg_rva ? MH_CreateHook((BYTE *)ga + lg_rva, hk_logicmgr_update,
                                             (void **)&orig_logicmgr_update)
                             : MH_ERROR_NOT_EXECUTABLE;
    if (lg_st == MH_OK)
        lg_st = MH_EnableHook((BYTE *)ga + lg_rva);
    wk_logicmgr_hook_ok = (lg_st == MH_OK);
    if (!wk_logicmgr_hook_ok)
        logmsg("LogicMgr Update hook failed status=%d; menu gate runs without LogicMgr check",
               lg_st);
    char idbuf[WK_MOD_ID_MAX * 4], apidbuf[WK_MOD_ID_MAX * 4];
    wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
    wide_to_utf8(config.mod_id_approach, apidbuf, sizeof(apidbuf));
    logmsg("Hooks ready; waiting for modId=\"%s\" (wait) / \"%s\" (approach) "
           "to be switched on in the game's Mod manager", idbuf, apidbuf);
    /* 【v2.0.2】approach 的 IL2CPP 解析不再在本（自建）线程做 —— 两次实机
     * "Collecting from unknown thread" 崩溃证明：运行时半就绪时，未 attach
     * 线程碰任何会分配的 IL2CPP API 都会 Fatal。解析已挪到游戏线程输入帧
     * 里的 ap_resolve_tick()（见 hk_input_update），worker 从此零 IL2CPP 调用。 */
    return 0;
}

/* 【流程 2】入口引导：DLL 被加载器映射进来时只做最轻的一件事——拉起 worker
 * 线程（流程 3+4 的全部初始化都在那边做）。DllMain 里严禁干重活：加载器锁
 * 之下 LoadLibrary/COM 都可能死锁。Java 类比：构造器里不做重活，
 * 在 ServletContextListener 里提交 @Async 初始化任务，主线程立刻返回。 */
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}

/* Some system dependencies call D3D11 before our process-attach entry point.
 * Export trampolines therefore resolve their target on demand. Do not load
 * another library from DllMain or jump through an uninitialized slot. */
FARPROC proxy_resolve(unsigned index) {
    if (index >= sizeof(export_names) / sizeof(export_names[0])) ExitProcess(ERROR_PROC_NOT_FOUND);
    if (proxy_slots[index]) return proxy_slots[index];
    WCHAR path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n >= MAX_PATH - 12) ExitProcess(ERROR_MOD_NOT_FOUND);
    memcpy(path + n, L"\\d3d11.dll", sizeof(L"\\d3d11.dll"));
    HMODULE real = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE own = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&proxy_resolve, &own);
    if (!real || real == own) ExitProcess(ERROR_MOD_NOT_FOUND);
    FARPROC result = GetProcAddress(real, export_names[index]);
    if (!result) ExitProcess(ERROR_PROC_NOT_FOUND);
    InterlockedCompareExchangePointer((PVOID volatile *)&proxy_slots[index], (PVOID)result, NULL);
    return result;
}
