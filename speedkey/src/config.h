/* Speed-hotkey configuration: a small, dependency-free INI reader.
 *
 * The file lives next to the mod DLL (the game root, not the Workshop folder,
 * so a Workshop update can never overwrite the player's bindings). It is read
 * at start-up and re-read whenever its last-write time changes, so edits apply
 * without restarting the game and without rebuilding the mod.
 *
 * Java 类比：本文件 = application.properties 的加载与解析
 * （@ConfigurationProperties，配合 speedkey.c 的热重载）。
 * mod_id 字段是开关轮询要在游戏 Mod 管理器里匹配的主键；
 * speed_steps 是档位序列（默认 1,2,5,10，上限=最后一项，玩家可自行降档）。
 */
#ifndef HSLR_SPEEDKEY_CONFIG_H
#define HSLR_SPEEDKEY_CONFIG_H

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keymap.h"

#define SK_MAX_BINDINGS 16
#define SK_CONFIG_MAX_BYTES (64 * 1024)
#define SK_MAX_STEPS 8

/* Longest accepted Mod id, in UTF-16 code units. The id lives in the game's
 * mod.json; the Mod 创建 page derives it from whatever the author typed, so
 * Chinese ids are normal (7 units covers 「战斗倍速快捷键」). */
#define SK_MOD_ID_MAX 64
/* Built-in fallback in UTF-8 (converted at load time; avoids relying on the
 * compiler's wide-literal charset handling for CJK). */
#define SK_DEFAULT_MOD_ID_UTF8 "待机快捷键"

typedef struct {
    int key;
    int action;
    char key_name[24];
    char action_name[24];
} SKBinding;

typedef struct {
    SKBinding bindings[SK_MAX_BINDINGS];
    int count;
    bool require_no_modifier;
    bool verbose_log;
    bool force_enable;
    /* v2.0.0 整合版新增：ini 子开关。0 = 倍速功能整体停用（按键无效并把
     * 当前档位收回 1x），默认 1 保持原行为不变。 */
    bool enable;
    bool dialogue_force_1x;   /* 对话/演出期间主动把速度拉回 1x 的兜底保险 */
    bool guard_reassert;      /* 新场景实例/对话结束后自动恢复设定档位 */
    /* v2.2.0 定版：GSET 写入者追踪钩开关。钩 Global.SetGameSpeed 记录每次
     * 写入的值与调用方（排查大地图调速链路用的诊断设施）。被动转发、
     * 无行为干预，但属于排查工具——默认关，排查时改 1。 */
    bool trace_gset;
    float steps[SK_MAX_STEPS]; /* 档位序列，升序；上限 = 最后一项 */
    int step_count;
    int errors;
    /* Which game-side Mod entry's on/off switch this build obeys. Matched
     * against the manifest's modId, so it must equal mod.json's modId. */
    WCHAR mod_id[SK_MOD_ID_MAX];
    int mod_id_chars;
} SKConfig;

typedef struct {
    const char *name;
    int action;
} SKActionName;

/* Action names accepted in [hotkeys]. 0 = one step faster, 1 = one step slower. */
static const SKActionName sk_action_names[] = {
    {"speed_up",0},   {"加速",0},   {"up",0},   {"+",0},  {"加",0},
    {"speed_down",1}, {"减速",1},   {"down",1}, {"-",1},  {"减",1},
};

static int sk_action_code(const char *value) {
    char lowered[24];
    if (!value || !*value) return -1;
    wk_lower_into(lowered, sizeof(lowered), value);
    for (size_t i = 0; i < sizeof(sk_action_names) / sizeof(sk_action_names[0]); ++i)
        if (!strcmp(sk_action_names[i].name, lowered)) return sk_action_names[i].action;
    return -1;
}

/* The .ini is UTF-8, and so is any id typed into the game's Mod page; fall back
 * to the system code page so an .ini re-saved as ANSI still resolves. A bad or
 * oversized value keeps the built-in default rather than disabling the check. */
static bool sk_config_set_mod_id_utf8(SKConfig *c, const char *value) {
    size_t len = strlen(value);
    if (len >= 2 && (value[0] == '"' || value[0] == '\'') && value[len - 1] == value[0]) {
        ++value;
        len -= 2;
    }
    if (!len) return false;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, (int)len,
                               c->mod_id, SK_MOD_ID_MAX - 1);
    if (n <= 0)
        n = MultiByteToWideChar(CP_ACP, 0, value, (int)len, c->mod_id, SK_MOD_ID_MAX - 1);
    if (n <= 0) return false;
    c->mod_id[n] = L'\0';
    c->mod_id_chars = n;
    return true;
}

static void sk_config_default_mod_id(SKConfig *c) {
    /* Convert the UTF-8 built-in at runtime (same path an .ini value takes).
     * The ASCII fallback cannot fail, so mod_id is always initialised. */
    if (!sk_config_set_mod_id_utf8(c, SK_DEFAULT_MOD_ID_UTF8))
        sk_config_set_mod_id_utf8(c, "hslr-speed-hotkey");
}

static void sk_config_defaults(SKConfig *c) {
    memset(c, 0, sizeof(*c));
    c->require_no_modifier = true;
    c->verbose_log = false;
    c->force_enable = false;
    c->enable = true;   /* v2.0.0 子开关默认开，行为与独立版一致 */
    c->dialogue_force_1x = true;
    c->guard_reassert = true;
    c->trace_gset = false;   /* v2.2.0 追踪钩默认关（排查设施） */
    /* 需求拍板：档位固定 1 / 2 / 5 / 10；上限 = 最后一项（ini 可降）。 */
    c->steps[0] = 1.0f; c->steps[1] = 2.0f; c->steps[2] = 5.0f; c->steps[3] = 10.0f;
    c->step_count = 4;
    c->bindings[0].key = 14; /* equals = 主键盘 + */
    c->bindings[0].action = 0;
    strcpy(c->bindings[0].key_name, "equals");
    strcpy(c->bindings[0].action_name, "speed_up");
    c->bindings[1].key = 13; /* minus = 主键盘 - */
    c->bindings[1].action = 1;
    strcpy(c->bindings[1].key_name, "minus");
    strcpy(c->bindings[1].action_name, "speed_down");
    c->bindings[2].key = 80; /* numpadplus = 小键盘 + */
    c->bindings[2].action = 0;
    strcpy(c->bindings[2].key_name, "numpadplus");
    strcpy(c->bindings[2].action_name, "speed_up");
    c->bindings[3].key = 81; /* numpadminus = 小键盘 - */
    c->bindings[3].action = 1;
    strcpy(c->bindings[3].key_name, "numpadminus");
    strcpy(c->bindings[3].action_name, "speed_down");
    c->count = 4;
    sk_config_default_mod_id(c);
}

static char *sk_trim(char *s) {
    char *end;
    while (*s == ' ' || *s == '\t') ++s;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = '\0';
    return s;
}

static bool sk_value_is_true(const char *v) {
    return !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes") ||
           !strcmp(v, "on");
}

/* 档位序列：逗号分隔的浮点数。规则（与需求基线一致）：
 *   - 至少 1 项、至多 8 项；每项 0.01 ~ 1000；重复值去重；结果升序排列。
 *   - 上限 = 最后一项（玩家想降到 8 / 5 就把最后一项改掉）。
 *   - 解析失败/为空 → 保留内置默认 1,2,5,10 并计一次错误。 */
static void sk_parse_steps(SKConfig *c, const char *value) {
    float vals[SK_MAX_STEPS];
    int n = 0;
    const char *p = value;
    while (*p) {
        char *end = NULL;
        float v = strtof(p, &end);
        if (end == p) break;                       /* 非数字：停止解析 */
        if (v > 0.01f && v < 1000.0f && n < SK_MAX_STEPS) {
            bool dup = false;
            for (int i = 0; i < n; ++i)
                if (vals[i] > v - 0.001f && vals[i] < v + 0.001f) { dup = true; break; }
            if (!dup) vals[n++] = v;
        } else if (n >= SK_MAX_STEPS) {
            break;
        }
        p = end;
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
    }
    if (n < 1) {
        ++c->errors;                               /* 无有效档位：保留默认 */
        return;
    }
    /* 简单插入排序（n <= 8）。 */
    for (int i = 1; i < n; ++i) {
        float v = vals[i];
        int j = i - 1;
        while (j >= 0 && vals[j] > v) { vals[j + 1] = vals[j]; --j; }
        vals[j + 1] = v;
    }
    memcpy(c->steps, vals, sizeof(float) * (size_t)n);
    c->step_count = n;
}

static void sk_binding_add(SKConfig *c, const char *keyname, const char *actionname) {
    int code = wk_key_code(keyname);
    int action = sk_action_code(actionname);
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
    if (c->count >= SK_MAX_BINDINGS) {
        ++c->errors;
        return;
    }
    SKBinding *b = &c->bindings[c->count++];
    b->key = code;
    b->action = action;
    wk_lower_into(b->key_name, sizeof(b->key_name), keyname);
    wk_lower_into(b->action_name, sizeof(b->action_name), actionname);
}

/* Parse INI text. Unknown keys and unparsable lines are counted in c->errors
 * and skipped; a malformed file can never remove the built-in fallback. */
static bool sk_config_parse(SKConfig *c, char *text) {
    enum { SK_SEC_NONE, SK_SEC_HOTKEYS, SK_SEC_OPTIONS } section = SK_SEC_NONE;
    bool saw_hotkeys = false;
    SKConfig draft;
    sk_config_defaults(&draft);

    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char *p = next;
            while (*p == '\r' || *p == '\n') *p++ = '\0';
            next = p;
        }
        char *s = sk_trim(line);
        if (*s == '#' || *s == ';' || !*s) {
            line = next;
            continue;
        }
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = '\0';
                char *name = sk_trim(s + 1);
                char lowered[24];
                wk_lower_into(lowered, sizeof(lowered), name);
                if (!strcmp(lowered, "hotkeys")) section = SK_SEC_HOTKEYS;
                else if (!strcmp(lowered, "options")) section = SK_SEC_OPTIONS;
                else section = SK_SEC_NONE;
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
        char *key = sk_trim(s);
        char *value = sk_trim(eq + 1);
        char keylow[24];
        wk_lower_into(keylow, sizeof(keylow), key);
        if (section == SK_SEC_OPTIONS) {
            if (!strcmp(keylow, "require_no_modifier")) draft.require_no_modifier = sk_value_is_true(value);
            else if (!strcmp(keylow, "verbose_log")) draft.verbose_log = sk_value_is_true(value);
            else if (!strcmp(keylow, "force_enable")) draft.force_enable = sk_value_is_true(value);
            else if (!strcmp(keylow, "enable")) draft.enable = sk_value_is_true(value);
            else if (!strcmp(keylow, "dialogue_force_1x")) draft.dialogue_force_1x = sk_value_is_true(value);
            else if (!strcmp(keylow, "guard_reassert")) draft.guard_reassert = sk_value_is_true(value);
            else if (!strcmp(keylow, "trace_gset")) draft.trace_gset = sk_value_is_true(value);
            else if (!strcmp(keylow, "speed_steps")) sk_parse_steps(&draft, value);
            else if (!strcmp(keylow, "mod_id")) { if (!sk_config_set_mod_id_utf8(&draft, value)) ++draft.errors; }
            else ++draft.errors;
        } else if (section == SK_SEC_HOTKEYS) {
            if (wk_key_code(key) < 0 || sk_action_code(value) < 0) ++draft.errors;
            else {
                if (!saw_hotkeys) {
                    draft.count = 0; /* first explicit binding replaces the default */
                    saw_hotkeys = true;
                }
                sk_binding_add(&draft, key, value);
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
static bool sk_config_load(SKConfig *c, const WCHAR *path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > SK_CONFIG_MAX_BYTES) {
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
    bool parsed = sk_config_parse(c, text);
    HeapFree(GetProcessHeap(), 0, buffer);
    return parsed;
}

/* Write the documented default file with a UTF-8 BOM so Notepad renders the
 * Chinese comments correctly. Only called when no file exists yet, so a
 * player's own bindings are never overwritten. */
static bool sk_config_write_default(const WCHAR *path) {
    static const char body[] =
        "# 幻世录重置版 · 战斗倍速快捷键 配置文件\n"
        "#\n"
        "# 改完保存即生效，不用重启游戏，也不用重新安装。\n"
        "#\n"
        "# 按主键盘或小键盘的 +（加速）/ -（减速）切换档位：\n"
        "#   1x → 2x → 5x → 10x（再按 + 停在 10x）\n"
        "#   10x → 5x → 2x → 1x（再按 - 停在 1x，即原速）\n"
        "# 档位序列可以在下面 [options] 的 speed_steps 里改。\n"
        "\n"
        "[hotkeys]\n"
        "# 左边是键名，右边是动作：\n"
        "#   speed_up   加一档（加速）\n"
        "#   speed_down 减一档（减速）\n"
        "# 想换键就改左边，例如：\n"
        "#   f = speed_up\n"
        "#   g = speed_down\n"
        "# 想关掉某组键就删掉或注释掉那一行。\n"
        "equals = speed_up\n"
        "minus = speed_down\n"
        "numpadplus = speed_up\n"
        "numpadminus = speed_down\n"
        "\n"
        "[options]\n"
        "# 档位序列（逗号分隔，升序；上限 = 最后一项）。\n"
        "# 想限制最高 5 倍就改成：speed_steps = 1,2,5\n"
        "speed_steps = 1,2,5,10\n"
        "\n"
        "# 按住 Ctrl / Alt / Win 时是否不触发（1=不触发）。\n"
        "# 保留它是为了避免和游戏原生的组合键冲突；Shift 放行 ——\n"
        "# 主键盘的「+」本身就要 Shift 配合，不能因此拒掉加速键。\n"
        "require_no_modifier = 1\n"
        "\n"
        "# 对话/演出期间把游戏速度压回 1x 的兜底保险（1=开）。\n"
        "# 游戏自带的对话仲裁一般会把速度重置回 1，这一项是双保险；\n"
        "# 如果实测对话节奏被加速影响，保持 1 即可。\n"
        "dialogue_force_1x = 1\n"
        "\n"
        "# 进战斗/读档/切场景后自动恢复当前档位（1=开）。\n"
        "guard_reassert = 1\n"
        "\n"
        "# 每次切档/恢复都写详细日志（排查问题用，平时保持 0）\n"
        "verbose_log = 0\n"
        "\n"
        "# GSET 写入者追踪钩（排查用，默认 0=关）。打开后钩住游戏官方写速\n"
        "# 总闸 Global.SetGameSpeed，在日志里记录每次写入的值与调用方地址。\n"
        "# 只记录不干预；排查大地图调速问题时配合 verbose_log=1 使用。\n"
        "trace_gset = 0\n"
        "\n"
        "# 本地预览用：不依赖游戏内 Mod 管理里的开关，强制生效。\n"
        "# 正式订阅安装后请保持 0，改用游戏内的开关控制。\n"
        "force_enable = 0\n"
        "\n"
        "# 本功能总开关（1=生效 0=停用）。整合进「待机快捷键」后，游戏内\n"
        "# 的条目开关管的是全部功能；只想单独停用倍速，把这里改成 0——\n"
        "# 停用瞬间当前档位自动收回 1x 原速。\n"
        "enable = 1\n"
        "\n"
        "# 本 Mod 在游戏 Mod 管理里那条记录的 ID（就是 MODS\\<名字>\\mod.json 里的 modId）。\n"
        "# 由安装工具自动写入并保持同步，一般不用手动改。\n"
        "mod_id = 待机快捷键\n";

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
