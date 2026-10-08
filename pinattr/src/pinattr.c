/* PinAttr - Fixed attribute gain for "之源 / 之魂" consumables in
 * The Legend of Fancy Realm Remake (HSLR 2.2.4 / build 25767968).
 *
 * 问题：游戏中「力之源」这类道具使用时随机加 1~5 点属性；因为游戏允许随时
 * 存/读档，玩家可以反复 SL 刷出最大值——随机在这里只增加操作负担，没有乐趣。
 * 本 Mod 把这类道具的加值固定为指定值（默认取各自随机范围上限）。
 *
 * === 实现（全部结论来自 hotkey/docs/属性道具稳定加值调研.md，反汇编实证）===
 *
 * 落档链路（【2.2.0】RVA 为 build 25470727 重解值，括号内为 2.1.5 旧值）：
 *   背包：StorageViewModel::UsePlayerBagItem(0xA5D420，旧 0xA4B3A0)
 *           → ItemUtils::UseItem 只负责消耗道具
 *           → CombatUtils::CastOnByItem(0x8D45F0，旧 0x8E2930) 演出/施放
 *           → game.FightAttr::AddByType(0xC60C50，旧 0xC17790) 永久属性落档 ★
 *   仓库：ItemUtils::UseStorageItem(0xB95590，旧 0xB78FC0)（静态，自带消耗+演出+落档）
 *
 * 三个 hook（方案 A''：双入口包裹 + itemId 映射钳值）：
 *   hook① UsePlayerBagItem  detour: 读第 3 参 itemId → 命中映射则置标志并记值
 *   hook② UseStorageItem    detour: 读第 1 参 itemId → 同上
 *   hook③ AddByType         detour: 标志有效 且 attrType ∈ 白名单
 *                                   → 把 value 换成映射值，再调原函数
 *
 * ③ 道防误伤闸（用户明确要求「不允许误伤，要判断」）：
 *   1. 包裹标志：只在 hook①/② 的原函数执行期间有效——该窗口内 AddByType 的
 *      调用者必然是这次道具使用（消耗→演出→落档同步完成）。窗口外一切放行。
 *   2. itemId 映射：只有 pin_items 里列出的道具才置标志。没列的道具
 *      （回血药、装备词条、五抗之源…）连标志都不会置。
 *   3. attrType 白名单：即使前两道因配置失误放行，属性类型不在
 *      {9,12,14,15} 就零干预——回血药是 5(Hp)，被这一道挡住。
 *
 * 不碰的东西：战斗临时属性、升级成长、装备、掉落、事件系统、随机系统。
 * 战斗内使用道具（StageManager::DoAtkSkillCost → UseItem）不经过两个入口，
 * 且 UseItem 只消耗不落档，故与本 Mod 天然无交集。
 *
 * 地址策略：3 个新增 hook 点走硬编码 RVA + 16 字节指纹（fail-closed，
 * 见 fingerprints.h）；输入帧钩子与 Mod 开关根复用既有已验证指纹。
 * 本 Mod 不需要 IL2CPP 按名解析——不查任何类型/方法/字段。
 *
 * 布局与代理 ABI 主干沿用 MIT 许可的参考实现 by Doomsdayer（见 LICENSE.txt）。
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

#define PA_LOG_FILE L"hslr-pinattr.log"
#define PA_CONFIG_FILE L"hslr-pinattr.ini"
#define PA_VERSION "1.0.14"

/* Accepted GameAssembly.dll builds. Same pair the wait-hotkey / speed mods
 * accept: the stock build and the build patched by the unrelated "nightmare
 * randomiser" Mod. Anything else disables the Mod entirely. */
static const char *const pa_accepted_hashes[] = {
    "1fda3e3add16e97abfd8ced98c24de839f0b079db803ae6f5a7867800fe03f1e",
    "7333475e7176b1f7395c724470a80fffd08e706295ba58fbbf59f0105e6634f1",
    "6aea28ddb7951f5df240f2fecc8cb16a79a8ecd0eb1cdc2b1343798a02be198c",
    "d969e10b94b47d423c701c1dd5fa6986cc8c8c5774519a89b2605fe91285bc09",
    "b023545ec4bc3bc23bf6193e55bb61e8583173b9dc65b1dec2e452e580296b45"
};

static HMODULE self, ga;
static HANDLE logfile = INVALID_HANDLE_VALUE;

/* 本模块的 SHA256 白名单（known_build 用）。 */
#define HSLR_ACCEPTED_HASHES pa_accepted_hashes

/* 日志 / 长路径 / 构建校验 —— 四模块共用实现，见 shared/common.h。 */
#include "common.h"

static bool enabled, state_seen;
static PAConfig config;

/* 【v2.3.0 伞形总控】读 hslr-wait-hotkey.ini 的 [features] pinattr 键
 * （与伞形/倍速同一处总控文件）。文件缺失或键不存在 = 视为开，
 * 老配置文件与独立部署环境行为不变。mtime 变化时热重载。 */
#define PA_UMBRELLA_INI L"hslr-wait-hotkey.ini"
static bool umbrella_enable = true;
static bool umbrella_seen;
static ULONGLONG umbrella_stamp;

/* v2.3.0：游戏内开关(enabled) && 自身 ini 子开关(config.enable) &&
 * 伞形 [features] 总控(umbrella_enable) 三重取与。 */
static bool feature_on(void) {
    return enabled && config.enable && umbrella_enable;
}

#define PTR(p, o) (*(void **)((BYTE *)(p) + (o)))
#define VAL(t, p, o) (*(t *)((BYTE *)(p) + (o)))
/* 【v2.9.0 免版本跟进】所有经 FN 的直接调用都先过运行时重定位层 rva_of()：
 * 构建未变时它是恒等映射（与原值逐字节相同），构建更新后由锚点窗口搜索
 * 找回真实地址；解析失败的点由模块级闸门 pa_reloc_ok 拦下，永不误调。 */
#define FN(rva, ret, ...) ((ret(*)(__VA_ARGS__))((BYTE *)ga + rva_of(rva)))

/* 【v2.9.0 免版本跟进】运行时重定位层（本模块独立实例）。
 * RVA_LOG 必须在 include 之前注入：头文件里的扫描器用它写日志。 */
#define RVA_LOG logmsg
#include "rva_reloc.h"

/* ------------------------------------------------------------------ */
/* Build identity                                                      */
/* ------------------------------------------------------------------ */

/* 字节指纹校验（fail-closed）：本次 7 行 = 3 个新增 hook 点 + 4 行复用项。
 * 【v2.9.0 免版本跟进】拆成两条路径：
 *   NATIVE   构建哈希命中白名单 → 原逐点字节校验（行为与 v2.8.0 100% 相同）；
 *   ADAPTIVE 构建未知 → 交给重定位层：锚点全部唯一找回才放行（方案 C，不再
 *            因为哈希不认识就整体停机）；有任何一点没找回 → 本卫星停用。 */
static bool check_fingerprints(void) {
    if (rva_mode == RVA_MODE_NATIVE) {
        for (unsigned i = 0; i < sizeof(pa_fingerprints) / sizeof(pa_fingerprints[0]); i++) {
            const PAFingerprint *f = &pa_fingerprints[i];
            if (memcmp((BYTE *)ga + f->rva, f->bytes, 16)) {
                logmsg("Fingerprint mismatch %s RVA=%llx", f->name, (unsigned long long)f->rva);
                return false;
            }
        }
        logmsg("%u fingerprints verified", (unsigned)(sizeof(pa_fingerprints) / sizeof(pa_fingerprints[0])));

        /* 落档调用点锚：5 字节 call 指令必须原样在位，且返回地址闸门才有意义。
         * 任一不符 → fail-closed（与指纹同级别）。 */
        for (unsigned i = 0; i < sizeof(pa_call_sites) / sizeof(pa_call_sites[0]); i++) {
            const PACallSite *c = &pa_call_sites[i];
            if (memcmp((BYTE *)ga + c->call_rva, c->bytes, 5)) {
                logmsg("Call-site mismatch %s RVA=%llx", c->name, (unsigned long long)c->call_rva);
                return false;
            }
        }
        logmsg("%u call sites verified", (unsigned)(sizeof(pa_call_sites) / sizeof(pa_call_sites[0])));
        return true;
    }
    /* ADAPTIVE：锚点的字节级一致性已由扫描器（唯一命中）保证，等价于逐点校验，
     * 且额外覆盖了「函数被整体挪位」的情形。调用点锚随其所属函数一同重定位。 */
    if (!rva_all_ok()) {
        logmsg("reloc: %d of %u anchors could not be relocated; pin-attr disabled",
               rva_failed(), (unsigned)RVA_ANCHOR_COUNT);
        return false;
    }
    logmsg("reloc: all %u anchors relocated; pin-attr active on an unknown build",
           (unsigned)RVA_ANCHOR_COUNT);
    return true;
}

/* ------------------------------------------------------------------ */
/* Hook state: the itemId context captured by hook①/②                  */
/* ------------------------------------------------------------------ */

/* 包裹标志。hook①/② 在原函数执行前置位、返回后清除；hook③ 只在这个窗口内
 * 才可能改写 value。用 volatile 是因为 hook③ 可能从不同线程进入（Unity 主线程
 * 之外还有演出协程线程），虽然实际都在主线程，但语义上必须声明。
 *
 * Java 类比：相当于一个 ThreadLocal<ItemContext>，只不过这里作用域是
 * 「一次同步调用栈」，比线程范围更窄也更准确。 */
static volatile LONG pa_depth;        /* 包裹深度（防嵌套重入） */
static volatile LONG pa_item_id;      /* 当前道具 ID；0 = 无上下文 */
static volatile LONG pa_cur_value;    /* 当前道具的固定值；-1 = 不干预 */

/* 一次命中的统计（写日志用） */
static volatile LONG pa_hit_count;

/* 进入/离开包裹窗口。返回是否命中映射（仅用于日志）。 */
static bool pa_enter(int item_id) {
    int v = pa_pin_value(&config, item_id);
    InterlockedIncrement(&pa_depth);
    if (v >= 0) {
        InterlockedExchange(&pa_item_id, item_id);
        InterlockedExchange(&pa_cur_value, v);
        return true;
    }
    /* 未命中：清掉残留上下文（防御性——理论上不该有） */
    InterlockedExchange(&pa_item_id, 0);
    InterlockedExchange(&pa_cur_value, -1);
    return false;
}

static void pa_leave(void) {
    InterlockedDecrement(&pa_depth);
    if (pa_depth <= 0) {
        InterlockedExchange(&pa_depth, 0);
        InterlockedExchange(&pa_item_id, 0);
        InterlockedExchange(&pa_cur_value, -1);
    }
}

/* ------------------------------------------------------------------ */
/* The three hooks                                                     */
/* ------------------------------------------------------------------ */

/* 背包入口：StorageViewModel::UsePlayerBagItem(this, slotIdx, itemId, resultList)
 * 参数位实证（item_args.txt A4B3A0 反汇编）：
 *   rcx=this  edx=slotIdx  r8d=itemId  r9=resultList */
typedef bool (*pa_use_bag_fn)(void *self, int slot_idx, int item_id, void *result_list);
static pa_use_bag_fn orig_use_bag;

static bool hk_use_bag(void *self, int slot_idx, int item_id, void *result_list) {
    bool hit = false;
    if (feature_on()) hit = pa_enter(item_id);
    bool r = orig_use_bag(self, slot_idx, item_id, result_list);
    if (feature_on()) pa_leave();
    if (hit && config.verbose_log)
        logmsg("hit bag itemId=%d slot=%d -> pin value available", item_id, slot_idx);
    return r;
}

/* 仓库入口：ItemUtils::UseStorageItem(itemId, charId, resultList)（静态方法）
 * 参数位实证（item_args.txt B78FC0 反汇编）：
 *   ecx=itemId  edx=charId  r8=resultList
 * 该函数自带「CheckItemUseCondition → RemoveStorageItem → CastOnByItem →
 * AddByType 落档」，所以整个包裹窗口正好覆盖落档。 */
typedef bool (*pa_use_storage_fn)(int item_id, int char_id, void *result_list);
static pa_use_storage_fn orig_use_storage;

/* ---- v1.0.2 诊断：charData 属性组件容器快照 ----
 * v1.0.1 实机：钳值写进了 [charData+0x58]（AddByType self），但玩家面板只涨
 * 原始随机值。静态分析（pinattr-trace2.txt）显示 UpdateFightAttrs(0xB23600)
 * 会 Clear(+0x60) 后把 +0x50(custom)/+0x58(本组件)/+0x48(equip)/基础成长
 * 逐个累进 +0x60——理论上 +5 应当生效，实测没有。本诊断在命中窗口里对
 * charData 的四个 FightAttr* 容器做「原函数前/后」快照，一次使用即可看出
 * +5 落在哪、+2 从哪冒出来。
 * 调用形态照抄游戏本体：wrapper = Game$$get_GPlayData(0)（新 0xC76230，
 * 游戏调用时 ecx=0），charData = GPlayData::GetCharData(wrapper, charId, 0)（新 0xB0D160）。*/
typedef void *(*pa_get_wrapper_fn)(int);
typedef void *(*pa_get_char_data_fn)(void *, int, unsigned);

static void dump_fa(const char *tag, const char *label, void *obj) {
    if (!obj) { logmsg("  %s %s: (null)", tag, label); return; }
    void *arr = PTR(obj, 0x10);
    if (!arr) { logmsg("  %s %s: arr=null", tag, label); return; }
    int total = VAL(int, arr, 0x18);
    int n = total < 32 ? total : 32;
    char line[560];
    int off = sprintf(line, "%s %s obj=%016llx n=%d:", tag, label,
                      (unsigned long long)(uintptr_t)obj, total);
    for (int i = 0; i < n && off < 500; i++) {
        int v = VAL(int, arr, 0x20 + i * 4);
        if (v) off += sprintf(line + off, " t%d=%d", i, v);
    }
    logmsg("%s", line);
}

static bool hk_use_storage(int item_id, int char_id, void *result_list) {
    bool hit = false;
    if (feature_on()) hit = pa_enter(item_id);
    void *cd = NULL;
    if (hit && config.verbose_log) {
        /* 【2.2.0】get_GPlayData 0xC76230 / GPlayData::GetCharData 0xB0D160（重解） */
        void *wrapper = ((pa_get_wrapper_fn)((BYTE *)ga + rva_of(0xC76230)))(0);
        if (wrapper)
            cd = ((pa_get_char_data_fn)((BYTE *)ga + rva_of(0xB0D160)))(wrapper, char_id, 0);
        if (cd) {
            logmsg("---- use %d begin charData=%016llx ----",
                   item_id, (unsigned long long)(uintptr_t)cd);
            dump_fa("before", "c+0x48", PTR(cd, 0x48));
            dump_fa("before", "c+0x50", PTR(cd, 0x50));
            dump_fa("before", "c+0x58", PTR(cd, 0x58));
            dump_fa("before", "c+0x60", PTR(cd, 0x60));
        } else {
            logmsg("---- use %d begin (charData unavailable) ----", item_id);
        }
    }
    bool r = orig_use_storage(item_id, char_id, result_list);
    if (hit && config.verbose_log && cd) {
        dump_fa("after ", "c+0x48", PTR(cd, 0x48));
        dump_fa("after ", "c+0x50", PTR(cd, 0x50));
        dump_fa("after ", "c+0x58", PTR(cd, 0x58));
        dump_fa("after ", "c+0x60", PTR(cd, 0x60));
        logmsg("---- use %d end ----", item_id);
    }
    if (feature_on()) pa_leave();
    if (hit && config.verbose_log)
        logmsg("hit storage itemId=%d charId=%d -> pin value available", item_id, char_id);
    return r;
}

/* ---- v1.0.3：效果值源头钳制（提示文字与落档同源）----
 * v1.0.2 实机：面板数值已正确（+5 落档成功），但使用道具的文字提示仍显示
 * 随机值。反汇编（pinattr-trace2/4.txt）证明两边读的是同一份数据：
 * CastOnByItem 产出的结果列表，每个元素 [elem+0x60] 是一个
 * Dictionary<int,int>（attrType → 随机后的加值），随机值在字典里就已定死：
 *   字典 entries 数组在 [dict+0x18]、count 在 [dict+0x20]，
 *   每条 16 字节：hashCode@+0  next@+4  key(attrType)@+8  value@+0xc。
 * 落档循环经 MoveNext(0x12CE3F0) 从字典打包 (attrType,value) → AddByType；
 * 提示文字也读同一字典。
 * 修法：hook CastOnByItem，原函数返回后立刻把字典里白名单属性的 value
 * 原地改成 pin 值——落档与提示从源头一致。AddByType 的返回地址锚钳制
 * 保留作最后一道保险（此时 value 已是 pin，钳制为空操作）。 */
typedef void *(*pa_cast_on_by_item_fn)(int skill_ref, void *list_holder, void *tag);
static pa_cast_on_by_item_fn orig_cast_on_item;

static void pa_patch_effect_dict(void *dict, int pin) {
    if (!dict) return;
    void *entries = PTR(dict, 0x18);
    int count = VAL(int, dict, 0x20);
    if (!entries || count <= 0 || count > 64) {
        if (config.verbose_log)
            logmsg("  dict=%016llx entries=%016llx count=%d (skipped)",
                   (unsigned long long)(uintptr_t)dict,
                   (unsigned long long)(uintptr_t)entries, count);
        return;
    }
    for (int j = 0; j < count; j++) {
        unsigned char *entry = (unsigned char *)entries + 0x20 + (size_t)j * 16;
        int hc = VAL(int, entry, 0);
        if (hc < 0) continue;               /* Dictionary 惯例：hashCode<0 = 空槽 */
        int key = VAL(int, entry, 8);       /* attrType */
        int val = VAL(int, entry, 0xc);     /* 随机后的加值 */
        if (pa_attr_allowed(&config, key)) {
            VAL(int, entry, 0xc) = pin;
            if (config.verbose_log)
                logmsg("  dict entry[%d] attrType=%d value %d -> %d", j, key, val, pin);
        } else if (config.verbose_log) {
            logmsg("  dict entry[%d] attrType=%d value=%d (not whitelisted, untouched)",
                   j, key, val);
        }
    }
}

static void pa_patch_result_list(void *list_holder, int pin) {
    if (!list_holder) return;
    void *list = PTR(list_holder, 0);
    if (!list) return;
    int count = VAL(int, list, 0x18);       /* List<T>._size */
    void *items = PTR(list, 0x10);          /* List<T>._items */
    if (!items || count <= 0 || count > 64) {
        if (config.verbose_log)
            logmsg("  resultList=%016llx items=%016llx count=%d (skipped)",
                   (unsigned long long)(uintptr_t)list,
                   (unsigned long long)(uintptr_t)items, count);
        return;
    }
    for (int i = 0; i < count; i++) {
        void *elem = PTR(items, 0x20 + (size_t)i * 8);
        if (!elem) continue;
        void *dict = PTR(elem, 0x60);       /* 效果字典（trace2: B791F2 [rax+0x60]） */
        if (config.verbose_log)
            logmsg("  elem[%d]=%016llx dict=%016llx", i,
                   (unsigned long long)(uintptr_t)elem,
                   (unsigned long long)(uintptr_t)dict);
        pa_patch_effect_dict(dict, pin);
    }
}

static void *hk_cast_on_item(int skill_ref, void *list_holder, void *tag) {
    void *r = orig_cast_on_item(skill_ref, list_holder, tag);
    int pin = pa_cur_value;
    if (feature_on() && pa_depth > 0 && pa_item_id != 0 && pin >= 0) {
        if (config.verbose_log)
            logmsg("CastOnByItem returned; patching result list (itemId=%d pin=%d)",
                   pa_item_id, pin);
        pa_patch_result_list(list_holder, pin);
        InterlockedIncrement(&pa_hit_count);
    }
    return r;
}

/* 落档点：FightAttr::AddByType(this, attrType, value, packed) -> int（返回实际增量）
 * 参数位实证（item_args.txt C17790 反汇编）：
 *   rcx=this(FightAttr)  edx=attrType  r8d=value  r9=packed(低字节=cap开关/高32=上限)
 * 函数体：单次 `add [this+0x10 数组 + attrType*4 + 0x20], value`，无循环。
 * 道具路径的调用方一律传 r9=0（不设上限），故 detour 只改 value 不动 packed。 */
typedef int (*pa_add_by_type_fn)(void *self, int attr_type, int value, uint64_t packed);
static pa_add_by_type_fn orig_add_by_type;

static int hk_add_by_type(void *self, int attr_type, int value, uint64_t packed) {
    /* 四道闸：①包裹窗口内 ②有 itemId 上下文与固定值 ③attrType 在白名单
     * ④返回地址必须是两个道具效果循环调用点之一（v1.0.1 新增）。
     *
     * ④ 的由来：v1.0.0 实机证明窗口内还会进来别的 AddByType（角色记录重算，
     * attrType 同样在白名单里），靠 ①②③ 拦不住——它们看起来和真效果一模一样。
     * 返回地址是唯一不可伪造的「谁发起的调用」证据：
     *   [rsp] = 原调用方 call 的下一条指令地址（MinHook 跳板不压栈，原样保留）。
     * Java 类比：相当于拿 StackWalker 取直接调用者的 class#method 再比对，
     * 只不过这里精确到一条 call 指令。 */
    if (feature_on() && pa_depth > 0 && pa_item_id != 0) {
        uintptr_t ret = (uintptr_t)__builtin_return_address(0);
        uintptr_t off = ret - (uintptr_t)ga;
        const PACallSite *hit_cs = NULL;
        for (unsigned i = 0; i < sizeof(pa_call_sites) / sizeof(pa_call_sites[0]); i++) {
            if (off == pa_call_sites[i].ret_rva) { hit_cs = &pa_call_sites[i]; break; }
        }
        int pin = pa_cur_value;
        if (hit_cs && pin >= 0 && pa_attr_allowed(&config, attr_type)) {
            if (config.verbose_log)
                logmsg("pin AddByType self=%016llx itemId=%d attrType=%d value %d -> %d via %s",
                       (unsigned long long)(uintptr_t)self, pa_item_id, attr_type,
                       value, pin, hit_cs->name);
            InterlockedIncrement(&pa_hit_count);
            value = pin;
        } else if (config.verbose_log) {
            /* 窗口内的非效果调用：放行原值，只记录。这条日志同时是取证——
             * 下一轮实测能直接看到重算调用来自哪个 this / 哪个返回地址。 */
            logmsg("skip AddByType self=%016llx attrType=%d value=%d ret=+0x%llx%s",
                   (unsigned long long)(uintptr_t)self, attr_type, value,
                   (unsigned long long)off, pin < 0 ? " (no pin)" : "");
        }
    }
    return orig_add_by_type(self, attr_type, value, packed);
}

/* ------------------------------------------------------------------ */
/* In-game Mod switch (same audited chain the other mods use)          */
/* ------------------------------------------------------------------ */

static bool id_matches(void *s) {
    if (!s || config.mod_id_chars <= 0) return false;
    int slen = VAL(int, s, 0x10);
    if (slen == config.mod_id_chars)
        return !memcmp((BYTE *)s + 0x14, config.mod_id, (size_t)config.mod_id_chars * 2);
    /* 【v2.5.2 工坊条目】游戏把工坊订阅条目的主键写成 "<modId>@<文件ID>"
     * （实测：待机快捷键@3802488917）→ 全等匹配打不中，订阅玩家 +5 全关。
     * 放宽为：前缀相同且紧跟 '@'。 */
    if (slen > config.mod_id_chars) {
        const WCHAR *p = (const WCHAR *)((BYTE *)s + 0x14);
        return !memcmp(p, config.mod_id, (size_t)config.mod_id_chars * 2) &&
               p[config.mod_id_chars] == L'@';
    }
    return false;
}

/* 【v2.3.0 modId 自愈白名单】ini 里的 mod_id 在游戏条目列表里一个都对不上
 * 时，按这份内置列表回退——全是自家历史/现用 ID，不含第三方：
 *   0 = 整合条目「待机快捷键」（v2.0.0 起的正式 ID）
 *   1 = 老独立版条目「属性道具稳定加值」（工坊更新不会动游戏根目录的
 *       hslr-pinattr.ini，老用户残留 ini 仍指向它 → 逐字符比较永不命中 →
 *       +5 静默失效；2026-09-20 三名用户反馈的根因）
 * 主 ID 哪怕匹配到 disabled 条目也不回退，保持原语义。 */
#define PA_FALLBACK_IDS 2
static WCHAR pa_fallback_ids[PA_FALLBACK_IDS][PA_MOD_ID_MAX];
static int pa_fallback_chars[PA_FALLBACK_IDS];
static bool pa_fallback_ready;
static bool pa_fallback_active;   /* 当前是否正用回退 ID（供日志只提示一次） */

static void pa_fallback_init(void) {
    static const char *const ids[PA_FALLBACK_IDS] = {
        PA_DEFAULT_MOD_ID_UTF8,   /* 待机快捷键 */
        "属性道具稳定加值",
    };
    for (int i = 0; i < PA_FALLBACK_IDS; ++i) {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ids[i], -1,
                                    pa_fallback_ids[i], PA_MOD_ID_MAX - 1);
        if (n <= 0)
            n = MultiByteToWideChar(CP_ACP, 0, ids[i], -1,
                                    pa_fallback_ids[i], PA_MOD_ID_MAX - 1);
        pa_fallback_chars[i] = (n > 0) ? n - 1 : 0;   /* 去掉结尾 NUL 计入长度 */
    }
    pa_fallback_ready = true;
}

/* 与 id_matches 同一布局语义：manifest modId = UTF-16，len@0x10、chars@0x14。 */
static bool pa_fallback_matches(void *s) {
    if (!pa_fallback_ready || !s) return false;
    int slen = VAL(int, s, 0x10);
    if (slen <= 0 || slen > PA_MOD_ID_MAX - 1) return false;
    for (int i = 0; i < PA_FALLBACK_IDS; ++i)
        if (pa_fallback_chars[i] == slen &&
            !memcmp((BYTE *)s + 0x14, pa_fallback_ids[i], (size_t)slen * 2))
            return true;
    return false;
}

/* Queried on Unity's main input tick, never from the worker thread. */
static void read_mods(void) {
    static int last_reason = -1, last_modflag = -2, last_compatible = -2, last_errors = -2;
    static const char *reasons[] = {"enabled", "mod-manager-null", "mod-list-null",
        "invalid-mod-list", "mod-id-not-found", "mod-disabled", "mod-incompatible",
        "mod-validation-items"};
    if (config.force_enable) {
        if (!state_seen) logmsg("force_enable=1; ignoring the in-game Mod switch");
        state_seen = true;
        enabled = true;
        return;
    }
    bool next = false;
    int reason = 1, modflag = -1, compatible = -1, errors = -1;
    int seen_count = 0;
    char seen_ids[400] = "";
    void *fb_mod = NULL;   /* 【v2.3.0】回退白名单命中的第一个条目 */
    void *t = FN(0xC76810, void *, void *)(NULL);
    if (t) {
        void *l = PTR(t, 0x18);
        reason = 2;
        if (l) {
            int n = VAL(int, l, 0x18);
            void *a = PTR(l, 0x10);
            reason = 3;
            if (a && n >= 0 && n < 4096) {
                reason = 4;
                for (int i = 0; i < n; i++) {
                    void *mod = PTR(a, 0x20 + i * 8);
                    if (!mod) continue;
                    void *man = PTR(mod, 0x10);
                    if (!man) continue;
                    void *sid = PTR(man, 0x10);
                    int slen = sid ? VAL(int, sid, 0x10) : 0;
                    if (slen > 0 && slen <= 64 && seen_count < 8 &&
                        strlen(seen_ids) + slen * 3 + 4 < sizeof(seen_ids)) {
                        WCHAR wtmp[65];
                        memcpy(wtmp, (BYTE *)sid + 0x14, (size_t)slen * 2);
                        wtmp[slen] = 0;
                        char one[196];
                        wide_to_utf8(wtmp, one, (int)sizeof(one));
                        seen_count++;
                        if (seen_count > 1) strncat(seen_ids, ", ", sizeof(seen_ids) - strlen(seen_ids) - 1);
                        strncat(seen_ids, one, sizeof(seen_ids) - strlen(seen_ids) - 1);
                    }
                    if (!fb_mod && pa_fallback_matches(sid)) fb_mod = mod;
                    if (id_matches(sid)) {
                        void *err = PTR(mod, 0x28);
                        int mf = VAL(BYTE, mod, 0x20), cp = VAL(BYTE, mod, 0x34);
                        int er = (mf && cp) ? (err ? VAL(int, err, 0x18) : 0) : -1;
                        bool on = mf && cp && er == 0;
                        /* 【v2.5.2】本地 + 工坊两条目可能同时命中：任一启用即算开；
                         * 还没遇到启用条目时记住当前命中的状态继续扫。 */
                        if (on || !next) {
                            modflag = mf; compatible = cp; errors = er;
                            next = on;
                            reason = on ? 0 : !mf ? 5 : !cp ? 6 : 7;
                        }
                        if (next) break;
                    }
                }
            }
        }
    }
    /* 【v2.3.0 自愈】走了完整条目列表（reason==4）且主 mod_id 一个都没匹配上
     * （modflag 仍为初值 -1），而列表里存在自家已知 ID 的条目 → 直接按那个
     * 条目的开关状态生效，残留旧 ini 的玩家免重装自愈。 */
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
    if (!state_seen || next != enabled || reason != last_reason || modflag != last_modflag ||
        compatible != last_compatible || errors != last_errors) {
        state_seen = true;
        /* enabled 保持「游戏内开关」的纯语义；ini 子开关在 feature_on() 里取与。
         * 这样 ini 热重载翻转 enable 时不需要 read_mods 重新触发也会立即生效。 */
        enabled = next;
        last_reason = reason;
        last_modflag = modflag;
        last_compatible = compatible;
        last_errors = errors;
        char idbuf[PA_MOD_ID_MAX * 4];
        wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
        logmsg("Mod %s; id=%s; reason=%s; enabled=%d compatible=%d validationItems=%d",
               enabled ? "enabled" : "disabled", idbuf, reasons[reason], modflag, compatible, errors);
        if (used_fallback && !pa_fallback_active)
            /* 只在「开始使用回退」的第一次打：醒目、指路，避免日志刷屏。 */
            logmsg("FALLBACK: ini mod_id \"%s\" matches no game entry; using the "
                   "built-in id instead (stale hslr-pinattr.ini? re-run the "
                   "installer to sync mod_id)", idbuf);
        pa_fallback_active = used_fallback;
        if (reason == 4)
            logmsg("Game Mod entries: %d seen; ids: %s", seen_count,
                   seen_ids[0] ? seen_ids : "(none readable)");
        else if (seen_count > 0)
            /* 【v2.5.2 取证】闸门状态每次变化都转储实际见到的条目 ID。 */
            logmsg("Game Mod entries: %d seen; ids: %s", seen_count, seen_ids);
    }
}

/* ------------------------------------------------------------------ */
/* Config hot-reload                                                   */
/* ------------------------------------------------------------------ */

static ULONGLONG config_stamp;

static void config_remember_stamp(void) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, PA_CONFIG_FILE);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    config_stamp = GetFileAttributesExW(p, GetFileExInfoStandard, &fad)
                       ? ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) |
                             fad.ftLastWriteTime.dwLowDateTime
                       : 0;
}

/* 【v2.3.0 伞形总控】读 hslr-wait-hotkey.ini 的 [features] pinattr。
 * 只认该段的 pinattr 键；文件缺失/键缺失 = 缺省开（*out 不变，调用方预置 true）。 */
static void umbrella_read_pinattr(bool *out) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, PA_UMBRELLA_INI);
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > PA_CONFIG_MAX_BYTES) {
        CloseHandle(f);
        return;
    }
    char *buffer = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)size + 1);
    if (!buffer) {
        CloseHandle(f);
        return;
    }
    DWORD got = 0;
    bool ok = ReadFile(f, buffer, size, &got, NULL) && got > 0;
    CloseHandle(f);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return;
    }
    char *text = buffer;
    if (got >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text += 3;
    int section_features = 0;
    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char *q = next;
            while (*q == '\r' || *q == '\n') *q++ = '\0';
            next = q;
        }
        char *s = pa_trim(line);
        if (*s == '#' || *s == ';' || !*s) {
            line = next;
            continue;
        }
        if (*s == '[') {
            /* 段名只可能是 ASCII，逐字符小写化比较 */
            char *end = strchr(s, ']');
            section_features = 0;
            if (end) {
                *end = '\0';
                char low[24];
                int li = 0;
                for (char *n = pa_trim(s + 1); *n && li < 23; ++n) {
                    char ch = *n;
                    low[li++] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
                }
                low[li] = '\0';
                section_features = !strcmp(low, "features");
            }
            line = next;
            continue;
        }
        if (!section_features) {
            line = next;
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            line = next;
            continue;
        }
        *eq = '\0';
        char *key = pa_trim(s);
        char klow[24];
        int ki = 0;
        for (; key[ki] && ki < 23; ++ki) {
            char ch = key[ki];
            klow[ki] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
        }
        klow[ki] = '\0';
        if (!strcmp(klow, "pinattr")) {
            *out = pa_value_is_true(pa_trim(eq + 1));
            break;   /* 找到即止：ini 里同名键以后写的为准的语义不适用于总控 */
        }
        line = next;
    }
    HeapFree(GetProcessHeap(), 0, buffer);
}

/* mtime 变化时重读伞形总控；文件消失（含伞形 ini 被删）→ 回到缺省开。
 * 只在值真正翻转时打日志，避免每帧刷屏。 */
static void umbrella_features_reload(void) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, PA_UMBRELLA_INI);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ULONGLONG stamp = GetFileAttributesExW(p, GetFileExInfoStandard, &fad)
                          ? ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) |
                                fad.ftLastWriteTime.dwLowDateTime
                          : 0;
    bool file_there = stamp != 0;
    if (umbrella_seen && stamp == umbrella_stamp) return;
    umbrella_stamp = stamp;
    umbrella_seen = true;
    bool value = true;
    if (file_there) umbrella_read_pinattr(&value);
    if (value != umbrella_enable) {
        umbrella_enable = value;
        logmsg("Umbrella ini [features] pinattr -> %d; pinning %s",
               value ? 1 : 0, value ? "active" : "fully disabled");
    }
}

static void config_log_summary(bool reload) {
    char idbuf[PA_MOD_ID_MAX * 4];
    wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
    logmsg("Config %s; pins=%d; attrTypes=%d; verbose=%d; force=%d; enable=%d; errors=%d; modId=%s",
           reload ? "reloaded" : "loaded", config.pin_count, config.attr_type_count,
           config.verbose_log, config.force_enable, config.enable, config.errors, idbuf);
    /* 【v2.3.0】伞形总控状态随摘要一起打，远程排障时与伞形/倍速日志对账。 */
    logmsg("Umbrella gate: [features] pinattr = %d", umbrella_enable ? 1 : 0);
    if (config.verbose_log) {
        for (int i = 0; i < config.pin_count; ++i) {
            int id = config.pins[i].item_id;
            const char *label = "";
            for (unsigned k = 0; k < sizeof(pa_default_pins) / sizeof(pa_default_pins[0]); ++k)
                if (pa_default_pins[k].item_id == id) { label = pa_default_pins[k].label; break; }
            logmsg("  pin %d (%s) -> %d", id, label[0] ? label : "?", config.pins[i].value);
        }
    }
}

static void config_setup(void) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, PA_CONFIG_FILE);
    if (!pa_config_load(&config, p)) {
        if (pa_config_write_default(p))
            logmsg("Wrote default config %ls", PA_CONFIG_FILE);
        pa_config_load(&config, p);
    }
    config_remember_stamp();
    umbrella_features_reload();   /* 【v2.3.0】启动时读一次伞形总控 */
    config_log_summary(false);
}

static void config_maybe_reload(void) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, PA_CONFIG_FILE);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ULONGLONG now = GetFileAttributesExW(p, GetFileExInfoStandard, &fad)
                        ? ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) |
                              fad.ftLastWriteTime.dwLowDateTime
                        : 0;
    if (now == config_stamp) return;
    config_stamp = now;
    PAConfig draft = config;
    if (pa_config_load(&draft, p)) {
        bool was = config.enable;
        config = draft;
        config_log_summary(true);
        /* v2.0.0 整合版：子开关翻转时给一条明确日志，方便远程排障。 */
        if (was != config.enable)
            logmsg("Ini switch 'enable' -> %d; pinning %s", config.enable ? 1 : 0,
                   config.enable ? "active" : "fully disabled");
    } else {
        logmsg("Config reload failed (kept previous)");
    }
}

/* ------------------------------------------------------------------ */
/* Frame tick: the mod's main-thread entry (reuses the input hook)     */
/* ------------------------------------------------------------------ */

/* 帧去重时间戳：同帧内 Update 可能被调多次，用毫秒时间戳保证每帧最多跑一次
 * 配置重载（配置检查本身极便宜，精度要求很低）。
 *
 * 这里刻意用系统时间而不是游戏内的帧计数（UnityEngine.Time$$get_frameCount）：
 * 该锚点列在 rva_anchors.h 里、参与启动时的整体构建健康判定（rva_all_ok），
 * 下面这行 rva_of() 的唯一作用就是让锚点提取工具继续认得它。 */
static uintptr_t frame_count(void) {
    (void)rva_of(0x3FFA550);
    return (uintptr_t)GetTickCount64();
}

typedef void (*pa_input_fn)(void *self, void *method);
typedef void (*pa_focus_fn)(void *self, bool focus, void *method);
/* v2.0.3：orig_input_update/orig_input_focus 随输入钩一并移除（钩子收归伞形） */

static bool process_focused = true;
/* 【v2.9.0 免版本跟进】模块级闸门：check_fingerprints() 通过（NATIVE 哈希命中，
 * 或 ADAPTIVE 下本模块全部锚点唯一找回）后由 worker 置真。未通过前 tick 直接
 * 返回，绝不调用任何尚未解析的地址。 */
static bool pa_reloc_ok = false;

/* v2.0.3 整合：输入对（0x65A4E0/0x657a40）钩子收归伞形 DLL 独占——三个独立
 * MinHook 实例并行钩同一目标地址，目标上最终只有一个 JMP，最后写入者的
 * detour 存活、其余静默失效（v2.0.2 实机实锤）。本模块经导出函数
 * pa_input_focus / pa_frame_tick 接收事件，自身不再钩输入对。 */
__declspec(dllexport) void pa_input_focus(int focus) {
    process_focused = focus ? true : false;
}

/* Unity 主线程每帧一次（本 Mod 的全部周期性工作都在这里）。 */
/* 每帧一次；由伞形 DLL 的输入钩子代为调用（v2.0.3，见 pa_input_focus 注释）。 */
__declspec(dllexport) void pa_frame_tick(void) {
    if (!pa_reloc_ok) return;
    if (!process_focused) return;
    static uintptr_t last_frame;
    uintptr_t now = frame_count();
    if (now == last_frame) return;
    last_frame = now;

    config_maybe_reload();
    umbrella_features_reload();   /* 【v2.3.0】伞形总控热重载（mtime 变化才真读） */
    read_mods();
}

/* ------------------------------------------------------------------ */
/* Bootstrap                                                           */
/* ------------------------------------------------------------------ */

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

static DWORD WINAPI worker(void *unused) {
    (void)unused;
    hslr_log_open(PA_LOG_FILE);
    logmsg("HSLR PinAttr %s; independent d3d11.dll; game 2.2.4 / build 25767968", PA_VERSION);
    hslr_log_self_path();
    config_setup();
    pa_fallback_init();           /* 【v2.3.0】modId 自愈白名单（静态 UTF-8 → UTF-16） */
    int build_ok = known_build();
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
    /* 【v2.9.0】重定位层必须在任何 FN/addhook 之前就绪。配置读伞形 ini（四模块共用）。 */
    {
        WCHAR up[HSLR_PATH_MAX];
        path_for(up, PA_UMBRELLA_INI);
        rva_reloc_configure(up);
    }
    rva_reloc_init(build_ok == 1);
    pa_reloc_ok = check_fingerprints();
    if (!pa_reloc_ok) {
        logmsg("Unsupported game build; Mod inactive and nothing is hooked");
        return 0;
    }
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK) {
        logmsg("MinHook initialization failed status=%d; Mod inactive", init_status);
        return 0;
    }
    /* 四个钩子：①背包 ②仓库 ③落档 ④效果施放。③ 是钳值兜底；
     * ④ 在源头改效果字典（提示文字与落档同源，v1.0.3）。①② 捕获 itemId 上下文
     * ——即使它们失败也无法安全钳值，故一并算失败。
     * v2.0.3：输入帧/焦点事件不再自己钩（收归伞形 DLL 独占，见 pa_input_focus 注释）。
     * 【2.2.0】四个入口 RVA 按 build 25470727 metadata 重解（rid-1→methodPointers，
     * 与 fingerprints.h 的静态字节双重验证一致，tools/analyze/resolve_gh_symbols.py）。 */
    if (!addhook(0xA5D420, hk_use_bag, (void **)&orig_use_bag) ||        /* UsePlayerBagItem */
        !addhook(0xB95590, hk_use_storage, (void **)&orig_use_storage) || /* UseStorageItem */
        !addhook(0xC60C50, hk_add_by_type, (void **)&orig_add_by_type) || /* FightAttr$$AddByType */
        !addhook(0x8D45F0, hk_cast_on_item, (void **)&orig_cast_on_item) || /* CastOnByItem */
        MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        logmsg("Hook installation failed; Mod inactive");
        return 0;
    }
    char idbuf[PA_MOD_ID_MAX * 4];
    wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
    logmsg("Hooks ready (bag/storage/castOn/addByType; input frames via umbrella); "
           "waiting for modId=\"%s\" to be switched on in the game's Mod manager", idbuf);
    return 0;
}

/* 入口引导：DllMain 只拉起 worker 线程，全部初始化都在那边做
 * （加载器锁下严禁干重活）。 */
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
