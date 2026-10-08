/* PinAttr configuration: itemId -> fixed value map + a dependency-free INI reader.
 *
 * Java 类比：本文件 = @ConfigurationProperties 的加载与解析。
 * 与倍速 Mod 的 config.h 同一套路：文件放在游戏根目录（DLL 旁边），
 * 启动时读一次、mtime 变化时热重载——改完保存即生效，不用重启游戏。
 *
 * 本 Mod 的配置核心是 pin_items：道具 ID -> 使用时固定的属性增量。
 *   224:5,225:5,226:5,227:5,280:15,281:15,282:15,283:15,290:10
 * 只列在这里的道具会被干预；没列的道具（回血药、装备词条、五抗之源…）
 * 一律零影响——这就是「不允许误伤」的第一道闸。
 */
#ifndef HSLR_PINATTR_CONFIG_H
#define HSLR_PINATTR_CONFIG_H

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PA_MAX_PINS 64
#define PA_CONFIG_MAX_BYTES (64 * 1024)
#define PA_MOD_ID_MAX 64
/* Built-in fallback in UTF-8 (converted at load time; avoids relying on the
 * compiler's wide-literal charset handling for CJK). */
#define PA_DEFAULT_MOD_ID_UTF8 "待机快捷键"

typedef struct {
    int item_id;   /* DesItem.Id */
    int value;     /* 使用该道具时属性增加多少（覆盖游戏随机值） */
} PAPin;

typedef struct {
    PAPin pins[PA_MAX_PINS];
    int pin_count;
    /* attrType 白名单：即使 itemId 奇偶匹配错，不在白名单的属性也零干预。
     * 默认 = 物攻14 / 防御9 / 魔攻15 / 速度12（之源与之魂四维同一集合）。 */
    int attr_types[16];
    int attr_type_count;
    bool require_no_modifier;
    bool verbose_log;
    bool force_enable;
    /* v2.0.0 整合版新增：ini 子开关。0 = 本功能整体停用（游戏内 Mod 开关
     * 之外的第三层闸），默认 1 保持原行为不变。 */
    bool enable;
    int errors;
    WCHAR mod_id[PA_MOD_ID_MAX];
    int mod_id_chars;
} PAConfig;

/* 内置默认映射（用户拍板 2026-09-20：钳到各自范围上限）。
 *
 * 之源 224~227：随机 1~5 → 钳 5（5 即上限，无争议）
 * 四维之魂 280~283：随机 3~15（期望 9）→ 钳 15（上限，玩家收益最大化）
 * 霸之魂 290：四选一 2~10 → 钳 10（上限）
 *
 * 不列入：228~232 五抗之源（本就恒定 +1）、284~288 五抗之魂（恒 3）、
 * 289 聖之魂（五选一但数值恒 2，随机只在挑选属性，无 SL 痛点）、
 * 回血药（治癒之水等，随机回血是设计意图，且走同一落档函数）。 */
typedef struct {
    int item_id;
    int value;
    const char *label;
} PADefaultPin;

static const PADefaultPin pa_default_pins[] = {
    {224, 5,  "力之源"},
    {225, 5,  "禦之源"},
    {226, 5,  "魔之源"},
    {227, 5,  "速之源"},
    {280, 15, "力之魂"},
    {281, 15, "禦之魂"},
    {282, 15, "魔之魂"},
    {283, 15, "速之魂"},
    {290, 10, "霸之魂"},
};

/* 默认 attrType 白名单：EFightAttrType 序号（= DesMagic.MagicFunc 数值）。
 * 14=PhysicalAttack 9=Defense 15=MagicAttack 12=Speed（数据表实证）。
 * 刻意不含 5(Hp)——回血药走同一落档函数，这是第三道闸。 */
static const int pa_default_attr_types[] = {9, 12, 14, 15};

static bool pa_attr_allowed(const PAConfig *c, int attr_type) {
    for (int i = 0; i < c->attr_type_count; ++i)
        if (c->attr_types[i] == attr_type) return true;
    return false;
}

/* 命中返回映射目标值；未命中返回 -1（= 本 Mod 不干预）。 */
static int pa_pin_value(const PAConfig *c, int item_id) {
    for (int i = 0; i < c->pin_count; ++i)
        if (c->pins[i].item_id == item_id) return c->pins[i].value;
    return -1;
}

static bool pa_config_set_mod_id_utf8(PAConfig *c, const char *value) {
    size_t len = strlen(value);
    if (len >= 2 && (value[0] == '"' || value[0] == '\'') && value[len - 1] == value[0]) {
        ++value;
        len -= 2;
    }
    if (!len) return false;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, (int)len,
                               c->mod_id, PA_MOD_ID_MAX - 1);
    if (n <= 0)
        n = MultiByteToWideChar(CP_ACP, 0, value, (int)len, c->mod_id, PA_MOD_ID_MAX - 1);
    if (n <= 0) return false;
    c->mod_id[n] = L'\0';
    c->mod_id_chars = n;
    return true;
}

static void pa_config_default_mod_id(PAConfig *c) {
    if (!pa_config_set_mod_id_utf8(c, PA_DEFAULT_MOD_ID_UTF8))
        pa_config_set_mod_id_utf8(c, "hslr-pinattr");
}

static void pa_config_defaults(PAConfig *c) {
    memset(c, 0, sizeof(*c));
    c->require_no_modifier = true;
    c->verbose_log = false;
    c->force_enable = false;
    c->enable = true;   /* v2.0.0 子开关默认开，行为与独立版一致 */
    for (size_t i = 0; i < sizeof(pa_default_pins) / sizeof(pa_default_pins[0]); ++i) {
        c->pins[c->pin_count].item_id = pa_default_pins[i].item_id;
        c->pins[c->pin_count].value = pa_default_pins[i].value;
        ++c->pin_count;
    }
    for (size_t i = 0; i < sizeof(pa_default_attr_types) / sizeof(pa_default_attr_types[0]); ++i)
        c->attr_types[c->attr_type_count++] = pa_default_attr_types[i];
    pa_config_default_mod_id(c);
}

static char *pa_trim(char *s) {
    char *end;
    while (*s == ' ' || *s == '\t') ++s;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = '\0';
    return s;
}

static bool pa_value_is_true(const char *v) {
    return !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes") ||
           !strcmp(v, "on");
}

/* pin_items 解析：逗号分隔的 "itemId:value" 对。
 * 规则：至少 1 对、至多 PA_MAX_PINS 对；itemId 1~99999、value 0~9999；
 * 后者覆盖前者的同 itemId 项；解析失败的对计一次错误并跳过。
 * 语义：文件里出现 pin_items = 即以文件内容整体替换内置默认（便于彻底关闭）。 */
static void pa_parse_pins(PAConfig *c, const char *value) {
    PAPin tmp[PA_MAX_PINS];
    int n = 0;
    const char *p = value;
    bool any = false;
    while (*p) {
        char *end = NULL;
        long id = strtol(p, &end, 10);
        if (end == p) break;
        if (*end != ':') { ++c->errors; break; }
        char *vend = NULL;
        long val = strtol(end + 1, &vend, 10);
        if (vend == end + 1) { ++c->errors; break; }
        if (id >= 1 && id <= 99999 && val >= 0 && val <= 9999) {
            bool replaced = false;
            for (int i = 0; i < n; ++i)
                if (tmp[i].item_id == (int)id) { tmp[i].value = (int)val; replaced = true; break; }
            if (!replaced && n < PA_MAX_PINS) {
                tmp[n].item_id = (int)id;
                tmp[n].value = (int)val;
                ++n;
            }
            any = true;
        } else {
            ++c->errors;
        }
        p = vend;
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
    }
    if (!any) {
        ++c->errors;   /* 无有效项：保留内置默认 */
        return;
    }
    memcpy(c->pins, tmp, sizeof(PAPin) * (size_t)n);
    c->pin_count = n;
}

/* attr_types 解析：逗号分隔的整数（EFightAttrType 序号）。 */
static void pa_parse_attr_types(PAConfig *c, const char *value) {
    int tmp[16];
    int n = 0;
    const char *p = value;
    while (*p) {
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        if (v >= 0 && v <= 255 && n < 16) tmp[n++] = (int)v;
        p = end;
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
    }
    if (n < 1) {
        ++c->errors;
        return;
    }
    memcpy(c->attr_types, tmp, sizeof(int) * (size_t)n);
    c->attr_type_count = n;
}

static bool pa_config_parse(PAConfig *c, char *text) {
    enum { PA_SEC_NONE, PA_SEC_OPTIONS } section = PA_SEC_NONE;
    PAConfig draft;
    pa_config_defaults(&draft);

    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char *p = next;
            while (*p == '\r' || *p == '\n') *p++ = '\0';
            next = p;
        }
        char *s = pa_trim(line);
        if (*s == '#' || *s == ';' || !*s) {
            line = next;
            continue;
        }
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = '\0';
                char *name = pa_trim(s + 1);
                char lowered[24];
                for (int i = 0; name[i] && i < 23; ++i) {
                    char ch = name[i];
                    lowered[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
                    lowered[i + 1] = '\0';
                }
                section = !strcmp(lowered, "options") ? PA_SEC_OPTIONS : PA_SEC_NONE;
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
        char *key = pa_trim(s);
        char *value = pa_trim(eq + 1);
        char keylow[32];
        for (int i = 0; key[i] && i < 31; ++i) {
            char ch = key[i];
            keylow[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
            keylow[i + 1] = '\0';
        }
        if (section == PA_SEC_OPTIONS) {
            if (!strcmp(keylow, "pin_items")) pa_parse_pins(&draft, value);
            else if (!strcmp(keylow, "attr_types")) pa_parse_attr_types(&draft, value);
            else if (!strcmp(keylow, "verbose_log")) draft.verbose_log = pa_value_is_true(value);
            else if (!strcmp(keylow, "force_enable")) draft.force_enable = pa_value_is_true(value);
            else if (!strcmp(keylow, "enable")) draft.enable = pa_value_is_true(value);
            else if (!strcmp(keylow, "mod_id")) { if (!pa_config_set_mod_id_utf8(&draft, value)) ++draft.errors; }
            else ++draft.errors;
        } else {
            ++draft.errors;
        }
        line = next;
    }

    *c = draft;
    return c->pin_count > 0;
}

/* Read the file as raw bytes (UTF-8, optional BOM) and parse it. */
static bool pa_config_load(PAConfig *c, const WCHAR *path) {
    /* 【v2.5.4】先铺默认值再读文件（对齐 goldhand 的 gh_config_load）：工坊
     * 订阅玩家可能身处只读目录（Steam 下载文件常态），ini 写不出时 config
     * 不能停留在全零——enable=false 会把整个功能静默杀死。此前本函数失败
     * 裸 return，config 全零进运行时，是「工坊玩家力之源失效」的根因。 */
    pa_config_defaults(c);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > PA_CONFIG_MAX_BYTES) {
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
    bool parsed = pa_config_parse(c, text);
    HeapFree(GetProcessHeap(), 0, buffer);
    return parsed;
}

/* Write the documented default file with a UTF-8 BOM so Notepad renders the
 * Chinese comments correctly. Only called when no file exists yet. */
static bool pa_config_write_default(const WCHAR *path) {
    static const char body[] =
        "# 幻世录重置版 · 属性道具稳定加值 配置文件\n"
        "#\n"
        "# 作用：使用「之源」「之魂」这类属性提升道具时，\n"
        "#       把游戏的随机加值固定为指定数值，避免反复存档/读档刷随机。\n"
        "#\n"
        "# 改完保存即生效，不用重启游戏，也不用重新安装。\n"
        "\n"
        "[options]\n"
        "# 道具 → 固定加值（逗号分隔的 itemId:数值 对）。\n"
        "# 只有列在这里的道具会被干预，其他道具完全不受影响。\n"
        "#\n"
        "# 默认（各道具随机范围的上限）：\n"
        "#   224 力之源 → 5      225 禦之源 → 5\n"
        "#   226 魔之源 → 5      227 速之源 → 5\n"
        "#   280 力之魂 → 15     281 禦之魂 → 15\n"
        "#   282 魔之魂 → 15     283 速之魂 → 15\n"
        "#   290 霸之魂 → 10\n"
        "#\n"
        "# 想改成固定 3 点就把 224:5 改成 224:3；\n"
        "# 想彻底不干预某个道具，把它从这一行删掉即可。\n"
        "# 例：只想管力之源，其他不管 →  pin_items = 224:5\n"
        "pin_items = 224:5,225:5,226:5,227:5,280:15,281:15,282:15,283:15,290:10\n"
        "\n"
        "# 允许干预的属性类型（EFightAttrType 序号），最后一道防误伤闸门。\n"
        "#   14=物理攻击  9=防御  15=魔法攻击  12=速度\n"
        "# 刻意不含 5(HP)：回血药走同一段代码，列在这里会让回血也固定。\n"
        "attr_types = 9,12,14,15\n"
        "\n"
        "# 每次命中都写详细日志（排查问题用，平时保持 0）\n"
        "verbose_log = 0\n"
        "\n"
        "# 本地预览用：不依赖游戏内 Mod 管理里的开关，强制生效。\n"
        "# 正式订阅安装后请保持 0，改用游戏内的开关控制。\n"
        "force_enable = 0\n"
        "\n"
        "# 本功能总开关（1=生效 0=停用）。整合进「待机快捷键」后，游戏内\n"
        "# 的条目开关管的是全部功能；只想单独停用加值干预，把这里改成 0。\n"
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
