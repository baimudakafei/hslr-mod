/* Wait-hotkey configuration: a small, dependency-free INI reader.
 *
 * The file lives next to the mod DLL (the game root, not the Workshop folder,
 * so a Workshop update can never overwrite the player's bindings). It is read
 * at start-up and re-read whenever its last-write time changes, so edits apply
 * without restarting the game and without rebuilding the mod.
 *
 * Java 类比：本文件 = application.properties 的加载与解析
 * （@ConfigurationProperties，配合 waitkey.c 的【流程 5b】热重载）。
 * mod_id 字段则是【流程 5c】开关轮询要在游戏 Mod 管理器里匹配的主键。
 * 流程编号总表见 waitkey.c 文件头的「代码导读」。
 */
#ifndef HSLR_WAITKEY_CONFIG_H
#define HSLR_WAITKEY_CONFIG_H

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keymap.h"

#define WK_MAX_BINDINGS 16
#define WK_CONFIG_MAX_BYTES (64 * 1024)

/* Longest accepted Mod id, in UTF-16 code units. The id lives in the game's
 * mod.json; the Mod 创建 page derives it from whatever the author typed, so
 * Chinese ids are normal (5 units covers 「待机快捷键」). */
#define WK_MOD_ID_MAX 64
/* 【v2.5.4】默认 mod_id 用 UTF-8 内置 + 运行时转 UTF-16（与三卫星同一写法），
 * 规避源文件编码歧义；转换失败才落到 ASCII 兜底值。工坊订阅玩家没有 ini，
 * 闸门匹配全靠这个默认值——历史版本在这里写的是旧英文 ID「hslr-wait-hotkey」，
 * 且伞形闸门没有卫星那套自愈白名单，是「工坊玩家空格待机失效」的根因。 */
#define WK_DEFAULT_MOD_ID_UTF8 "待机快捷键"
#define WK_DEFAULT_MOD_ID_ASCII L"hslr-wait-hotkey"
#define WK_DEFAULT_MOD_ID_APPROACH L"hslr-auto-move"

static void wk_config_set_default_id(WCHAR *dst, int *chars_out, const WCHAR *id) {
    int n = 0;
    while (id[n] && n < WK_MOD_ID_MAX - 1) ++n;
    memcpy(dst, id, (size_t)(n + 1) * sizeof(WCHAR));
    dst[WK_MOD_ID_MAX - 1] = L'\0';
    *chars_out = n;
}

typedef struct {
    int key;
    int action;
    char key_name[24];
    char action_name[24];
} WKBinding;

typedef struct {
    WKBinding bindings[WK_MAX_BINDINGS];
    int count;
    bool require_no_modifier;
    bool verbose_log;
    bool force_enable;
    /* 【v2.3.0 分功能总控】[features] 段。伞形自己只消费 wait/approach；
     * speed/pinattr 两键由对应卫星（mod-speedkey / mod-pinattr）在启动和
     * 热重载时同读本文件——玩家在一个 ini 里即可总控全部四个功能。
     * 缺省全 1：老配置文件没有 [features] 段时行为与旧版完全一致。 */
    bool feat_wait;
    bool feat_approach;
    bool feat_speed;
    bool feat_pinattr;
    int errors;
    /* Which game-side Mod entry's on/off switch this build obeys. Matched
     * against the manifest's modId, so it must equal mod.json's modId. */
    WCHAR mod_id[WK_MOD_ID_MAX];
    int mod_id_chars;
    /* The auto-approach feature follows a second, independent Mod entry
     * (「自动移动」) so players can switch it off without touching wait. */
    WCHAR mod_id_approach[WK_MOD_ID_MAX];
    int mod_id_approach_chars;
    /* Move-range fallback (chunks) used when the game's moveScope buffer is
     * empty while computing an approach destination. */
    int ap_path_range;
} WKConfig;

typedef struct {
    const char *name;
    int action;
} WKActionName;

/* Action names accepted in [hotkeys]. Semantic buttons, not displayed rows.
 * Action 7 = auto-approach toggle (bit 7, consumed by approach.h). */
static const WKActionName wk_action_names[] = {
    {"move",0},   {"移动",0},
    {"attack",1}, {"攻击",1},
    {"skill",2},  {"特殊技",2},
    {"magic",3},  {"魔法",3},
    {"item",4},   {"道具",4},
    {"status",5}, {"状态",5},
    {"wait",6},   {"待机",6},
    {"approach",7},{"接近",7},{"自动移动",7},{"自动接近",7},
};

static int wk_action_code(const char *value) {
    char lowered[24];
    if (!value || !*value) return -1;
    wk_lower_into(lowered, sizeof(lowered), value);
    for (size_t i = 0; i < sizeof(wk_action_names) / sizeof(wk_action_names[0]); ++i)
        if (!strcmp(wk_action_names[i].name, lowered)) return wk_action_names[i].action;
    return -1;
}

/* 【v2.5.4】UTF-8 → UTF-16（与 ini 值同一条转换路径，行为一致）。 */
static bool wk_config_set_mod_id_utf8(WKConfig *c, const char *value) {
    size_t len = strlen(value);
    if (!len) return false;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, (int)len,
                                c->mod_id, WK_MOD_ID_MAX - 1);
    if (n <= 0)
        n = MultiByteToWideChar(CP_ACP, 0, value, (int)len, c->mod_id, WK_MOD_ID_MAX - 1);
    if (n <= 0) return false;
    c->mod_id[n] = L'\0';
    c->mod_id_chars = n;
    return true;
}

static void wk_config_default_mod_id(WKConfig *c) {
    /* 【v2.5.4】运行时转换内置 UTF-8（与三卫星同一写法）；ASCII 兜底必成功。 */
    if (!wk_config_set_mod_id_utf8(c, WK_DEFAULT_MOD_ID_UTF8))
        wk_config_set_default_id(c->mod_id, &c->mod_id_chars, WK_DEFAULT_MOD_ID_ASCII);
}

static void wk_config_default_mod_id_approach(WKConfig *c) {
    wk_config_set_default_id(c->mod_id_approach, &c->mod_id_approach_chars,
                             WK_DEFAULT_MOD_ID_APPROACH);
}

static void wk_config_defaults(WKConfig *c) {
    memset(c, 0, sizeof(*c));
    c->require_no_modifier = true;
    c->verbose_log = false;
    c->force_enable = false;
    c->feat_wait = true;       /* 分功能总控缺省全开 */
    c->feat_approach = true;
    c->feat_speed = true;
    c->feat_pinattr = true;
    c->bindings[0].key = 1; /* space */
    c->bindings[0].action = 6; /* wait */
    strcpy(c->bindings[0].key_name, "space");
    strcpy(c->bindings[0].action_name, "wait");
    c->bindings[1].key = 21; /* g (Unity Key.G; keymap.h: f=20, g=21) */
    c->bindings[1].action = 7; /* approach toggle */
    strcpy(c->bindings[1].key_name, "g");
    strcpy(c->bindings[1].action_name, "approach");
    c->count = 2;
    c->ap_path_range = 6;
    wk_config_default_mod_id(c);
    wk_config_default_mod_id_approach(c);
}

/* The .ini is UTF-8, and so is any id typed into the game's Mod page; fall back
 * to the system code page so an .ini re-saved as ANSI still resolves. A bad or
 * oversized value keeps the built-in default rather than disabling the check. */
static bool wk_config_set_id(WCHAR *dst, int *chars_out, const char *value) {
    size_t len = strlen(value);
    if (len >= 2 && (value[0] == '"' || value[0] == '\'') && value[len - 1] == value[0]) {
        ++value;
        len -= 2;
    }
    if (!len) return false;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, (int)len,
                               dst, WK_MOD_ID_MAX - 1);
    if (n <= 0)
        n = MultiByteToWideChar(CP_ACP, 0, value, (int)len, dst, WK_MOD_ID_MAX - 1);
    if (n <= 0) return false;
    dst[n] = L'\0';
    *chars_out = n;
    return true;
}

static bool wk_config_set_mod_id(WKConfig *c, const char *value) {
    if (!wk_config_set_id(c->mod_id, &c->mod_id_chars, value)) {
        wk_config_default_mod_id(c);
        return false;
    }
    return true;
}

static char *wk_trim(char *s) {
    char *end;
    while (*s == ' ' || *s == '\t') ++s;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = '\0';
    return s;
}

static bool wk_value_is_true(const char *v) {
    return !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes") ||
           !strcmp(v, "on");
}

static void wk_binding_add(WKConfig *c, const char *keyname, const char *actionname) {
    int code = wk_key_code(keyname);
    int action = wk_action_code(actionname);
    if (code < 0 || action < 0) {
        ++c->errors;
        return;
    }
    /* Later duplicates of the same physical key replace the earlier binding. */
    for (int i = 0; i < c->count; ++i) {
        if (c->bindings[i].key == code) {
            c->bindings[i].action = action;
            wk_lower_into(c->bindings[i].action_name, sizeof(c->bindings[i].action_name), actionname);
            return;
        }
    }
    if (c->count >= WK_MAX_BINDINGS) {
        ++c->errors;
        return;
    }
    WKBinding *b = &c->bindings[c->count++];
    b->key = code;
    b->action = action;
    wk_lower_into(b->key_name, sizeof(b->key_name), keyname);
    wk_lower_into(b->action_name, sizeof(b->action_name), actionname);
}

/* Parse INI text. Unknown keys and unparsable lines are counted in c->errors
 * and skipped; a malformed file can never remove the built-in fallback. */
static bool wk_config_parse(WKConfig *c, char *text) {
    enum { WK_SEC_NONE, WK_SEC_HOTKEYS, WK_SEC_OPTIONS, WK_SEC_FEATURES } section = WK_SEC_NONE;
    bool saw_hotkeys = false;
    WKConfig draft;
    wk_config_defaults(&draft);

    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char *p = next;
            while (*p == '\r' || *p == '\n') *p++ = '\0';
            next = p;
        }
        char *s = wk_trim(line);
        if (*s == '#' || *s == ';' || !*s) {
            line = next;
            continue;
        }
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = '\0';
                char *name = wk_trim(s + 1);
                char lowered[24];
                wk_lower_into(lowered, sizeof(lowered), name);
                if (!strcmp(lowered, "hotkeys")) section = WK_SEC_HOTKEYS;
                else if (!strcmp(lowered, "options")) section = WK_SEC_OPTIONS;
                else if (!strcmp(lowered, "features")) section = WK_SEC_FEATURES;
                else section = WK_SEC_NONE;
            }
            line = next;
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            ++draft.errors;
            line = next;
            continue;
        }
        *eq = '\0';
        char *key = wk_trim(s);
        char *value = wk_trim(eq + 1);
        char keylow[24];
        wk_lower_into(keylow, sizeof(keylow), key);
        if (section == WK_SEC_OPTIONS) {
            if (!strcmp(keylow, "require_no_modifier")) draft.require_no_modifier = wk_value_is_true(value);
            else if (!strcmp(keylow, "verbose_log")) draft.verbose_log = wk_value_is_true(value);
            else if (!strcmp(keylow, "force_enable")) draft.force_enable = wk_value_is_true(value);
            else if (!strcmp(keylow, "mod_id")) { if (!wk_config_set_mod_id(&draft, value)) ++draft.errors; }
            else if (!strcmp(keylow, "mod_id_approach")) {
                if (!wk_config_set_id(draft.mod_id_approach, &draft.mod_id_approach_chars, value)) {
                    wk_config_default_mod_id_approach(&draft);
                    ++draft.errors;
                }
            }
            else if (!strcmp(keylow, "ap_path_range")) {
                int v = atoi(value);
                if (v >= 1 && v <= 99) draft.ap_path_range = v;
                else ++draft.errors;
            }
            else ++draft.errors;
        } else if (section == WK_SEC_FEATURES) {
            /* 分功能总控开关：只认 1/true/yes/on，其余按 0 处理；
             * 未列出的键走 defaults 的全开值（解析前 draft 已初始化）。 */
            if (!strcmp(keylow, "wait")) draft.feat_wait = wk_value_is_true(value);
            else if (!strcmp(keylow, "approach")) draft.feat_approach = wk_value_is_true(value);
            else if (!strcmp(keylow, "speed")) draft.feat_speed = wk_value_is_true(value);
            else if (!strcmp(keylow, "pinattr")) draft.feat_pinattr = wk_value_is_true(value);
            /* goldhand：金之手卫星自己读本 ini；伞形只认这个键、不消费，
             * 否则 v2.5.0 起 ini 里的 goldhand=1 每次都计一条 parseErrors。 */
            else if (!strcmp(keylow, "goldhand")) { /* accepted, consumed by the satellite */ }
            else ++draft.errors;
        } else if (section == WK_SEC_HOTKEYS) {
            if (wk_key_code(key) < 0 || wk_action_code(value) < 0) ++draft.errors;
            else {
                if (!saw_hotkeys) {
                    draft.count = 0; /* first explicit binding replaces the default */
                    saw_hotkeys = true;
                }
                wk_binding_add(&draft, key, value);
            }
        } else {
            ++draft.errors;
        }
        line = next;
    }

    *c = draft;
    return c->count > 0;
}

/* Read the file as raw bytes (UTF-8, optional BOM) and parse it. */
static bool wk_config_load(WKConfig *c, const WCHAR *path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > WK_CONFIG_MAX_BYTES) {
        CloseHandle(f);
        return false;
    }
    char *buffer = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)size + 1);
    if (!buffer) {
        CloseHandle(f);
        return false;
    }
    DWORD got = 0;
    bool ok = ReadFile(f, buffer, size, &got, NULL) && got > 0;
    CloseHandle(f);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return false;
    }
    char *text = buffer;
    if (got >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text += 3;
    bool parsed = wk_config_parse(c, text);
    HeapFree(GetProcessHeap(), 0, buffer);
    return parsed;
}

/* Write the documented default file with a UTF-8 BOM so Notepad renders the
 * Chinese comments correctly. Only called when no file exists yet, so a
 * player's own bindings are never overwritten. */
static bool wk_config_write_default(const WCHAR *path) {
    static const char body[] =
        "# 幻世录重置版 · 待机快捷键 + 自动移动 配置文件\n"
        "#\n"
        "# 改完保存即生效，不用重启游戏，也不用重新安装。\n"
        "#\n"
        "# 键名照抄下面括号里的英文即可：\n"
        "#   空格(space)  回车(enter)  上(up) 下(down) 左(left) 右(right)\n"
        "#   字母直接写 f / j / k，数字键盘写 numpad0 / numpad1，功能键写 f1\n"
        "#\n"
        "# 动作名：\n"
        "#   move   移动        attack 攻击        skill  特殊技\n"
        "#   magic  魔法        item   道具        status 状态\n"
        "#   wait   待机        approach 自动接近开关\n"
        "#\n"
        "# 可以绑任意多个键。多个键同时按下时，按下表顺序优先触发：\n"
        "#   移动 → 攻击 → 特殊技 → 魔法 → 道具 → 状态 → 待机\n"
        "# 这些键只在角色行动菜单真的可以操作时才生效，\n"
        "# 其他时候完全保持游戏原本的功能，不会抢键。\n"
        "\n"
        "[hotkeys]\n"
        "space = wait\n"
        "\n"
        "# 【自动接近】战斗中按一次 G 开启：轮到我方单位时自动向最近的敌人\n"
        "# 移动，移动完操作权立刻交还（打不打、待机由你决定）；再按一次 G\n"
        "# 取消并立即拿回操作权。不需要就把下一行删掉。\n"
        "g = approach\n"
        "\n"
        "# 想加更多键时，把下面的 # 去掉即可，例如：\n"
        "# f = attack\n"
        "# numpad0 = wait\n"
        "# k = 待机\n"
        "\n"
        "[options]\n"
        "# 按住 Shift / Ctrl / Alt / Win 时是否不触发（1=不触发）。\n"
        "# 保留它是为了避免和游戏原生的组合键冲突。\n"
        "require_no_modifier = 1\n"
        "\n"
        "# 每次触发都写详细日志（排查问题用，平时保持 0）。\n"
        "# 自动移动排障时也请临时改成 1。\n"
        "verbose_log = 0\n"
        "\n"
        "# 本地预览用：不依赖游戏内 Mod 管理里的开关，强制生效。\n"
        "# 正式订阅安装后请保持 0，改用游戏内的开关控制。\n"
        "force_enable = 0\n"
        "\n"
        "# 待机快捷键在游戏 Mod 管理里那条记录的 ID（MODS\\<名字>\\mod.json 里的 modId）。\n"
        "mod_id = 待机快捷键\n"
        "\n"
        "# 【自动接近】对应的 Mod 条目开关 ID。游戏内开关关闭时自动移动不生效。\n"
        "mod_id_approach = hslr-auto-move\n"
        "\n"
        "# 接近计算用的移动范围兜底值（1-99，游戏范围数据为空时才用到）。\n"
        "ap_path_range = 6\n"
        "\n"
        "# ── 运行时重定位层（v2.9.0，一般不用动）──\n"
        "# 官方更新后 RVA 会漂移。默认 auto：构建哈希命中白名单就走恒等映射\n"
        "# （零开销），不认识才在锚点附近做掩码搜索找回地址。\n"
        "#   auto     = 默认（构建已知→恒等；构建未知→窗口搜索）\n"
        "#   native   = 强制用原值（调试基准用）\n"
        "#   adaptive = 强制窗口搜索\n"
        "#   selftest = 强制窗口搜索，并把锚点人为偏移 rva_shot 字节（自测用）\n"
        "# rva_window 窗口半宽，十六进制，默认 0x40000（256 KB）。仅排查用。\n"
        "# rva_shot   selftest 模式下锚点的人为偏移量（十六进制，可为负）。默认 0。\n"
        "# rva_mode   = auto\n"
        "# rva_window = 0x40000\n"
        "# rva_shot   = 0x2A00\n"
        "\n"
        "[features]\n"
        "# 【分功能总控开关】1=开 0=关，保存即生效，不用重启游戏。\n"
        "# 这里是各功能的总闸：关掉后该功能整体停用（游戏内 Mod 开关、\n"
        "# 卫星自己的 ini 都不再单独影响它）。\n"
        "wait = 1\n"
        "approach = 1\n"
        "speed = 1\n"
        "pinattr = 1\n"
        "goldhand = 1\n";

    static const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, bom, 3, &written, NULL) && written == 3;
    if (ok) ok = WriteFile(f, body, (DWORD)(sizeof(body) - 1), &written, NULL) &&
                 written == (DWORD)(sizeof(body) - 1);
    CloseHandle(f);
    return ok;
}

#endif
