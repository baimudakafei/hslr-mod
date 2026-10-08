/* GoldHand configuration: hit-ratio / stamina cost overrides + INI reader.
 *
 * Java 类比：本文件 = @ConfigurationProperties 的加载与解析。
 * 与加值 Mod（pinattr）的 config.h 同一套路：文件放游戏根目录（DLL 旁边），
 * 启动读一次、mtime 变化热重载——保存即生效，不用重启游戏。
 *
 * 本 Mod 的配置面（新功能 2026-09-22，数据钳值方案，用户拍板）：
 *   [goldhand]
 *   enable    = 1     ; 子开关（游戏内 Mod 开关之外的一层闸）
 *   hit_ratio = 100   ; 金之手/高级金之手 目标命中率（原 90）。100 = 必定命中，
 *                     ; 见 goldhand.c 里的「命中率内部放大」说明（游戏会从命中率
 *                     ; 里扣对方闪避，UI 显示的仍是 100%）。
 *   expend    = 1     ; 金之手/高级金之手 目标气力格数（原 3 / 高级 2）
 *   steal_always = 1  ; 1 = 金之手必定偷到东西（把道具表的 GetRatio 钳高，使
 *                     ; 偷窃判定 prob ≥ 100）。0 = 保持游戏原判定。
 *   kill_gold_ratio = 10 ; 【v1.0.9 新增】杀敌金钱倍数。战斗击杀结算时，
 *                     ; 把该次入账的金币 × N（默认 10）。1 = 关闭（原版）。
 *                     ; 只影响「杀敌获得」这一条路径，商店买卖/任务奖励/
 *                     ; 卡牌小游戏一律不受影响（挂在 StageManager.GetAtkShowDatas
 *                     ; 的 TurnAddGold 写点，见 goldhand.c）。
 *   verbose_log = 0   ; 逐对象钳值日志（排障用）
 *   force_enable = 0  ; 1 = 忽略游戏内 Mod 开关（排障用）
 *   mod_id    = 待机快捷键
 */
#ifndef HSLR_GOLDHAND_CONFIG_H
#define HSLR_GOLDHAND_CONFIG_H

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GH_CONFIG_MAX_BYTES (64 * 1024)
#define GH_MOD_ID_MAX 64
/* Built-in fallback in UTF-8 (converted at load time; avoids relying on the
 * compiler's wide-literal charset handling for CJK). */
#define GH_DEFAULT_MOD_ID_UTF8 "待机快捷键"

typedef struct {
    bool verbose_log;
    bool force_enable;
    /* 子开关：0 = 本功能整体停用（游戏内 Mod 开关之外的一层闸）。 */
    bool enable;
    /* 目标值：命中率（%）与气力格数。用户拍板默认 100 / 1。
     * hit_ratio = 100 时内部会放大写入值（见 goldhand.c），保证必定命中。 */
    int hit_ratio;
    int expend;
    /* 1 = 金之手必定偷到东西：把 DesItem.GetRatio 钳到 GH_STEAL_GAIN，
     * 使偷窃判定 prob = StealRatio + GetRatio + 10 ≥ 100 → _rand100 恒真。
     * 副作用：任何走 DoMagicFunc_StealItem 的偷窃（含敌方）都会成功。 */
    bool steal_always;
    /* 【v1.0.9】杀敌金钱倍数：战斗击杀结算时把本次入账金币 × N（默认 10）。
     * 1 = 关闭（原版）；上限见 gh_config_load 夹取。只作用于杀敌路径。 */
    int kill_gold_ratio;
    /* 【排障用，默认关】1 = 安装只读探针：临时钩住游戏的
     * _calcHitRatio / _rand(100) / _rand100 / DoMagicFunc_StealItem，把
     * 命中率与偷窃判定的**运行时真值**写进日志（v1.0.4 定位「改 100 仍 miss」用）。
     * 只读、不改任何游戏状态；关掉即完全不挂钩（发行形态仍是零钩子）。 */
    bool trace;
    int errors;
    WCHAR mod_id[GH_MOD_ID_MAX];
    int mod_id_chars;
} GHConfig;

static bool gh_config_set_mod_id_utf8(GHConfig *c, const char *value) {
    size_t len = strlen(value);
    if (len >= 2 && (value[0] == '"' || value[0] == '\'') && value[len - 1] == value[0]) {
        ++value;
        len -= 2;
    }
    if (!len) return false;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, (int)len,
                                c->mod_id, GH_MOD_ID_MAX - 1);
    if (n <= 0)
        n = MultiByteToWideChar(CP_ACP, 0, value, (int)len, c->mod_id, GH_MOD_ID_MAX - 1);
    if (n <= 0) return false;
    c->mod_id[n] = L'\0';
    c->mod_id_chars = n;
    return true;
}

static void gh_config_default_mod_id(GHConfig *c) {
    if (!gh_config_set_mod_id_utf8(c, GH_DEFAULT_MOD_ID_UTF8))
        gh_config_set_mod_id_utf8(c, "hslr-goldhand");
}

static void gh_config_defaults(GHConfig *c) {
    memset(c, 0, sizeof(*c));
    c->enable = true;
    c->hit_ratio = 100;   /* 原值 90；100 = 必定命中 */
    c->expend = 1;        /* 原值：金之手 3 / 高级金之手 2 */
    c->steal_always = true;  /* 金之手必定偷到 */
    c->kill_gold_ratio = 10; /* 杀敌金钱 ×10（1 = 原版） */
    c->trace = false;        /* 【v1.0.10】调试探针默认关闭（它 hook 函数入口，缩小风险面） */
    gh_config_default_mod_id(c);
}

static void gh_config_trim(char **s) {
    while (**s == ' ' || **s == '\t') ++*s;
    char *e = *s + strlen(*s);
    while (e > *s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        --e;
    *e = '\0';
}

static bool gh_config_parse_bool(const char *v, bool def) {
    if (!v || !*v) return def;
    return !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "on") || !strcmp(v, "yes");
}

static int gh_config_parse_int(const char *v, int def) {
    if (!v || !*v) return def;
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v) return def;
    return (int)n;
}

/* 解析一个键值。返回 true = 认识这个键。 */
static bool gh_config_apply(GHConfig *c, const char *key, const char *value) {
    if (!strcmp(key, "enable")) { c->enable = gh_config_parse_bool(value, true); return true; }
    if (!strcmp(key, "hit_ratio")) { c->hit_ratio = gh_config_parse_int(value, 100); return true; }
    if (!strcmp(key, "expend")) { c->expend = gh_config_parse_int(value, 1); return true; }
    if (!strcmp(key, "steal_always")) { c->steal_always = gh_config_parse_bool(value, true); return true; }
    if (!strcmp(key, "kill_gold_ratio")) { c->kill_gold_ratio = gh_config_parse_int(value, 10); return true; }
    if (!strcmp(key, "verbose_log")) { c->verbose_log = gh_config_parse_bool(value, false); return true; }
    if (!strcmp(key, "trace")) { c->trace = gh_config_parse_bool(value, false); return true; }
    if (!strcmp(key, "force_enable")) { c->force_enable = gh_config_parse_bool(value, false); return true; }
    if (!strcmp(key, "mod_id")) { gh_config_set_mod_id_utf8(c, value); return true; }
    return false;
}

/* 迷你 INI：只认 [goldhand] 段（其它段忽略，容错为主）。 */
static bool gh_config_load(GHConfig *c, const WCHAR *path) {
    gh_config_defaults(c);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    static char buf[GH_CONFIG_MAX_BYTES];
    DWORD got = 0;
    bool ok = ReadFile(f, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(f);
    if (!ok) return false;
    buf[got] = '\0';

    char *p = buf;
    /* UTF-8 BOM 跳过 */
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;
    bool in_section = false;
    c->errors = 0;
    while (*p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl) { *nl = '\0'; p = nl + 1; } else { p += strlen(p); }
        char *s = line;
        gh_config_trim(&s);
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            char *close = strchr(s, ']');
            in_section = close &&
                !strncmp(s + 1, "goldhand", (size_t)(close - s - 1 > 8 ? 8 : close - s - 1));
            continue;
        }
        if (!in_section) continue;
        char *eq = strchr(s, '=');
        if (!eq) { ++c->errors; continue; }
        *eq = '\0';
        char *k = s, *v = eq + 1;
        gh_config_trim(&k);
        gh_config_trim(&v);
        if (!gh_config_apply(c, k, v)) ++c->errors;
    }
    /* 目标值夹在合理区间（防止手滑写出离谱值伤到别的系统） */
    if (c->hit_ratio < 0) c->hit_ratio = 0;
    if (c->hit_ratio > 100) c->hit_ratio = 100;
    if (c->expend < 0) c->expend = 0;
    if (c->expend > 9) c->expend = 9;
    /* 杀敌金钱倍数：夹在 [1, 100000]（1 = 原版；上限防手滑写出天文数字把
     * int32 金币撑溢出——金币字段是 int32，见 goldhand.c 的写点说明）。 */
    if (c->kill_gold_ratio < 1) c->kill_gold_ratio = 1;
    if (c->kill_gold_ratio > 100000) c->kill_gold_ratio = 100000;
    return true;
}

/* 默认 ini 模板（UTF-8 BOM + CRLF 由调用方处理：模板正文 LF，写出前统一换行）。 */
static const char *gh_config_default_text =
    "# 金之手（GoldHand）Mod 配置。\n"
    "# 数据钳值方案：游戏表加载后把金之手/高级金之手的目标值改写为下面两项。\n"
    "# 保存即生效（热重载），不用重启游戏。\n"
    "\n"
    "[goldhand]\n"
    "# 子开关：0 = 本功能整体停用（并自动还原为游戏原值）。\n"
    "enable = 1\n"
    "# 目标命中率（%）。游戏原值 90。100 = 必定命中（游戏会从命中率里减去\n"
    "# 对方闪避，本 Mod 在内部把这个值放大到足够大来抵消，界面显示的仍是 100%）。\n"
    "# 回原版写 90 即可。\n"
    "hit_ratio = 100\n"
    "# 目标气力格数。游戏原值：金之手 3、高级金之手 2。回原版改回 3（高级会按\n"
    "# 各自原值还原，见日志）。\n"
    "expend = 1\n"
    "# 1 = 金之手必定偷到东西（把道具表的 GetRatio 钳高，使偷窃判定必过）。\n"
    "# 0 = 保持游戏原判定（可能出现「命中但偷不到」）。\n"
    "steal_always = 1\n"
    "# 杀敌金钱倍数：战斗击杀结算时把该次入账金币 × N（默认 10）。\n"
    "# 1 = 关闭（原版）。只影响「杀敌获得」这一条路径；商店买卖/任务奖励/\n"
    "# 卡牌小游戏一律不受影响。上限 100000。\n"
    "kill_gold_ratio = 10\n"
    "# 逐对象钳值日志（排障用，默认关）。\n"
    "verbose_log = 0\n"
    "# 【排障用，默认关】1 = 安装只读探针，把命中/偷窃判定的运行时真值写进日志\n"
    "# （临时钩住 4 个游戏函数；只读不改状态）。正常游玩保持 0。\n"
    "trace = 0\n"
    "# 1 = 忽略游戏内 Mod 管理开关（排障用，默认关）。\n"
    "force_enable = 0\n"
    "# 游戏 Mod 管理里那条记录的 ID（MODS 下那个目录里 mod.json 的 modId）。\n"
    "# 与待机快捷键主 Mod 联动；匹配不到条目时自动回退内置 ID（自愈）。\n"
    "mod_id = 待机快捷键\n";

/* 写默认配置（UTF-8 BOM + CRLF），成功返回 true。 */
static bool gh_config_write_default(const WCHAR *path) {
    /* 把 LF 模板转成 CRLF */
    size_t n = strlen(gh_config_default_text);
    char *crlf = (char *)HeapAlloc(GetProcessHeap(), 0, n * 2 + 1);
    if (!crlf) return false;
    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
        if (gh_config_default_text[i] == '\n' && (j == 0 || crlf[j - 1] != '\r'))
            crlf[j++] = '\r';
        crlf[j++] = gh_config_default_text[i];
    }
    crlf[j] = '\0';

    /* CREATE_NEW（不是 CREATE_ALWAYS）：只有配置文件不存在时才写默认值。
     * 用 CREATE_ALWAYS 会在每次启动时把玩家改过的设置清回默认 —— 其余三个
     * 模块一直用的是 CREATE_NEW，这里对齐。 */
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, crlf);
        return false;
    }
    static const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    DWORD w;
    WriteFile(f, bom, 3, &w, NULL);
    WriteFile(f, crlf, (DWORD)j, &w, NULL);
    CloseHandle(f);
    HeapFree(GetProcessHeap(), 0, crlf);
    return true;
}

#endif
