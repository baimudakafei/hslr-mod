/* GoldHand - 100% hit rate & 1-stamina cost for Hanks' "金之手" skill family
 * in The Legend of Fancy Realm Remake (HSLR 2.2.4 / build 25767968).
 *
 * 需求（2026-09-22，用户拍板）：
 *   汉克斯的「金之手」（偷窃技能）命中率 90% → 100%，气力消耗 3 格 → 1 格；
 *   高级金之手一并处理（气力 2 → 1）。
 *
 * === 实现（全部结论来自 hotkey/docs/金之手调研.md，反汇编+数据表实证）===
 *
 * 方案 = 「运行时数据补丁」（方案 A，零钩子）：
 *   设计表是明文 JSON（resources.assets），加载后反序列化成托管对象。
 *   本 Mod 在游戏主线程 tick 里轮询，把目标行改写为 ini 指定值：
 *     DesSkill  Id 36（金之手）     Expend 3→1  HitRatio 90→100
 *     DesSkill  Id 38（高级金之手） Expend 2→1  HitRatio 90→100
 *     DesMagic  Id 3601/3801        HitRatio 90→200（见下「命中率内部放大」）
 *     DesItem   全表                GetRatio → 100（见下「偷窃判定必过」）
 *   游戏自己读改后的值 → 技能说明/消耗 UI 显示同步生效；命中公式
 *   (_calcHitRatio) 与消耗检查 (CheckSkillCost) 读同一批对象，天然一致。
 *
 * === v1.0.3：为什么「命中率改 100」还会 miss（实机反馈 + 反汇编定案）===
 *   _castSkill (0x8EBEF0) 的命中判定是：
 *       hitRatio = CombatUtils._calcHitRatio(...)      // 见下
 *       if (CombatUtils._rand(100) < hitRatio) → 命中
 *   而 _rand100/_rand 的实现在 NormalRandPolicy (0xC7DDC0)：
 *       return UnityEngine.Random.Range(0, 100) < prob     // 半开区间 → 0..99
 *   ⇒ prob/hitRatio **≥ 100 时恒真**。问题出在 _calcHitRatio 的公式本身
 *     (0x8DC840 反汇编)：
 *       if (HitRatio <= 100)  result = clamp(HitRatio+修正,20,100) + 物命 - 闪避
 *       if (HitRatio >  100)  result = (HitRatio - 100) + clamp(...) + 物命 - 闪避
 *   即 HitRatio=100 时仍会被对方的 DodgeRatio 扣下去（下限 10）→ 实测就 miss。
 *   而 HitRatio>100 时走「溢出补偿」分支：超出 100 的部分原样保留，
 *   于是把 DesMagic.HitRatio 写成 200 → 结果 ≥ 100 → _rand(100) 恒真 → 必定命中。
 *   界面不会显示 200%：CombatUtils.GetShowHitRatio (0x8E6730) 第一步就是
 *   `if (ratio >= 100) return 100;`，所以技能说明里看到的仍是 100%。
 *   ⇒ ini 的 hit_ratio 语义保持「期望命中率」：写 100 即必定命中（内部写 200），
 *     写 <100 则按原值写（可回原版 90）。
 *
 * === v1.0.3：金之手是偷窃技能，还有第二道判定 ===
 *   DesMagic 3601/3801 的 MagicFunc=50（偷窃系）且无任何伤害字段 → 金之手
 *   不造成伤害，它做的事在 CombatUtils.DoMagicFunc_StealItem (0x8D7C40)：
 *       for (每个道具) { prob = atkFightAttr.StealRatio + desItem[+0x78] + 10;
 *                        if (CombatUtils._rand100(prob)) { 偷到！break; } }
 *   其中 desItem[+0x78] 按 DesItem 的字段布局就是 GetRatio（第 20 个实例字段，
 *   偏移 0x78）。所以「命中与否」在这里由 StealRatio + GetRatio 决定，
 *   与 HitRatio 完全无关 —— 这就是「气力 1 格生效、攻击也命中，却还是 miss」的原因。
 *   ⇒ steal_always=1 时把全表 DesItem.GetRatio 钳到 GH_STEAL_GAIN(100)：
 *     prob = StealRatio + 100 + 10 ≥ 100 → _rand100 恒真 → 必定偷到。
 *     副作用：任何走 DoMagicFunc_StealItem 的偷窃（含敌方）都会成功，
 *     可以用 ini 的 steal_always=0 关掉。
 *
 * 为什么两个字典都要扫（round5/round6 反汇编定案）：
 *   TbSkill/TbMagic 上挂着两个 Dictionary<ValueDif,T>：
 *     +0x10  GetResolved 的命中缓存（merge 产物对象，独立于原始行，长期存活）
 *     +0x20  Get 的原始表查询（原始行）
 *   GetResolved 命中缓存时直接返回 merge 产物，只改其中一个都会漏。
 *   对策（round6 拿到硬常量）：按字典的精确 Entry 步长扫描两个字典，
 *   命中白名单 Id 就把值钳上。任何新 merge 对象在下一轮（≤500ms）被钳上。
 *
 * Dictionary<ValueDif,T> 布局（0x26DBFB0 TryGetValue 反汇编实证）：
 *   dict:  buckets@+0x10  entries@+0x18(Il2CppArray)  count@+0x20
 *   Entry: hash@+0x00  next@+0x04  key(ValueDif)@+0x08  value(T*)@+0x10
 *   步长 0x18（TryGetValue 里 (index*3+6)*8 展开 = index*24 + 0x30，
 *   而数组对象 +0x20 才是 data 首址 → 第 i 个 Entry 的 value 槽在
 *   data + 0x10 + 0x18*i）。按此步长读，全部落在数组内，零越界风险。
 *
 * 表对象入口（v1.0.2 修正；反汇编实证见 hotkey/build/meta/gh-tablemgr.txt）：
 *   v1.0.1 假设「TableMgr 里存在一个类型为 TableMgr 的静态字段」，实机日志
 *   直接打脸（resolve: TableMgr static singleton field not found），于是补丁
 *   一个值都没钳上。真实入口是游戏自己的访问器（都是「取个字段就返回」）：
 *     Game$$get_TableMgr()        0xC76C40  静态、无参    → TableMgr
 *     TableMgr$$get_Tables()      0x842180  实例(TableMgr) → cfg.Tables
 *     cfg.Tables$$get_TbSkill()   0x63F2B0  实例(Tables)   → TbSkill (= [tables+0x40])
 *     cfg.Tables$$get_TbMagic()   0x601AF0  实例(Tables)   → TbMagic (= [tables+0x38])
 *   四个 RVA 都有指纹锚点 + GameAssembly SHA256 白名单做 fail-closed；
 *   字段偏移依旧优先按名从 il2cpp 元数据取，取不到才用反汇编常量兜底。
 *   另有 0x8405B0/0x840080（TableMgr::GetSkill/GetMagic）用来「端到端自证」：
 *   用游戏自己的取值入口回读一遍，日志里直接看到 Expend=1 / HitRatio=100。
 *
 * 不依赖字段名/偏移猜容器：拿到表对象后直接遍历它的所有实例引用字段，对每个字段
 * 做「像不像一个 Dictionary」的结构自检（entries 必须是合法 Il2CppArray、
 * count ∈ [0, length]），自检通过才扫。防御式只读，猜错也只是空转。
 *
 * 补丁为什么长期有效（GetResolved 反汇编实证）：TbSkill::GetResolved 第一步就是
 * [_resolvedCache.TryGetValue(key)]，命中即原样返回同一实例 → 我们钳的缓存产物
 * 就是游戏此后每次读到的那一个；未命中的原始行在 _dataList 里，两个字典都扫。
 *
 * 生命周期与安全：
 *   - 零 MinHook：本 Mod 不 hook 任何游戏函数，只按名解析 IL2CPP 类与字段、
 *     读写已存在对象的 int 字段（无分配、无 GC 交互、无 write barrier）。
 *   - 全部工作在游戏主线程（伞形 DLL 每帧驱动的 gh_frame_tick）内完成，
 *     worker 线程只做文件 IO，不碰任何 IL2CPP API。
 *   - 关闭沿（enable=0 / [features] goldhand=0 / 游戏内开关关）：把
 *     _dataList 行恢复为首次见到时的原值（缓存对象不还原——它们由游戏
 *     重建，下次重建自然带上原值行）。
 *   - 解析 fail-closed：任一按名解析步骤失败 → 只写日志、零干预。
 *
 * 地址策略：无 RVA hook，仅复用 2 个既有已验证指纹（Mod 开关根 / 帧计数）
 * 做 fail-closed 的构建校验，另有 GameAssembly.dll SHA256 白名单。
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

#define GH_LOG_FILE L"hslr-goldhand.log"
#define GH_CONFIG_FILE L"hslr-goldhand.ini"
#define GH_VERSION "1.0.12"

/* 表对象入口 RVA（【2.2.0】build 25470727 按 metadata rid-1 重解，
 * tools/analyze/resolve_gh_symbols.py；GetItem 另有游戏调用点双重印证：
 * 新 UseStorageItem 内 call 0x83FFB0。元数据里 static 标志也已核对：
 * Game::get_TableMgr static=True，其余三个是实例方法）。 */
#define GH_RVA_GET_TABLEMGR 0xC76C40u   /* Game$$get_TableMgr()        静态无参 */
#define GH_RVA_GET_TABLES   0x842180u   /* TableMgr$$get_Tables()      实例 */
#define GH_RVA_GET_TBSKILL  0x63F2B0u   /* cfg.Tables$$get_TbSkill()   实例 */
#define GH_RVA_GET_TBMAGIC  0x601AF0u   /* cfg.Tables$$get_TbMagic()   实例 */

/* 游戏自己的「按 id 取行」入口（自证用，也是 DoAtkSkillCost / CheckSkillCost
 * 真正走的路径：Game.get_TableMgr() → GetSkill(id) → TbSkill.GetResolved(group, id)，
 * group 取 TableMgr.DataGroup）。反汇编实证：
 *   0x8405B0: rax=[rcx+0x10](m_Tables) → [rax+0x40](TbSkill) → group=[rcx+0x38] → GetResolved
 * 回读到的值就是游戏扣气力 / 判定命中时读到的那个字段。 */
#define GH_RVA_GETSKILL     0x8405B0u   /* TableMgr$$GetSkill(id)     实例 */
#define GH_RVA_GETMAGIC     0x840080u   /* TableMgr$$GetMagic(id)     实例 */
#define GH_RVA_GETITEM      0x83FFB0u   /* TableMgr$$GetItem(id)      实例 */

/* —— v1.0.4 trace 探针目标（只读观测，ini trace=1 才挂钩）——
 * 【2.2.0】四个探针 RVA 已重解；_calcHitRatio 消歧：0x8DC840 由
 * CombatUtils::_castSkill（新 0x8F4D80）唯一调用，另一同名重载 0x8F38E0 零调用者。
 * _rand(int) 取 0x8E1E60（DoMagicFunc_StealItem 内实际调用的重载）、
 * _rand100=0x8E1C50、DoMagicFunc_StealItem=0x8D7C40（StealItem 内调用点自证）。
 * _castSkill (新 0x8F4D80) 的判定链：
 *   hitRatio = _calcHitRatio(DesSkill.Type, magicIdx, &desMagic, atkAttr, defender)
 *   hit      = _rand(100) < hitRatio                  // 0x8E1E60
 *   命中后再走 _castMagic → MagicFunc 分发：
 *     MagicFunc 48 → DoMagicFunc_MagicDamage（伤害）
 *     MagicFunc 50 → DoMagicFunc_StealItem（0x8D7C40，金之手 3601/3801）
 *   偷窃循环里每个道具做 prob = StealRatio + GetRatio + 10 → _rand100(prob)（0x8E1C50）
 *   全部失败 ⇒ 把 CombatResult[+0x39]（命中标志，_castMagic 里刚置 1）改回 0
 *            ⇒ 屏幕上就是 MISS。所以「改命中率仍 MISS」必须分清是哪一道失败。
 *
 * DoMagicFunc_StealItem 的参数面（反汇编实证，本函数共 10 个参数，static 方法）：
 *   arg4  = 施法者 FightAttr（取 StealRatio）        [r12]
 *   arg9  = [rsp+0x70] → 解引用 = 偷窃目标（+0x40 = 可偷道具 List，+0x18 = Count）
 *   arg10 = [rsp+0x78] → 解引用 = CombatResult（+0x39 = 命中标志）
 * 早退路径（**不清零**命中标志）：判定 == 3 / 道具列表 null / Count == 0；
 * 只有「循环内每个道具的 _rand100 全败」才会清零 = MISS。
 * ⚠ 转发时必须原样带齐 10 个参数：少声明会让 trampoline 读到垃圾 arg9/arg10。 */
#define GH_RVA_TR_CALCHIT   0x8DC840u
#define GH_RVA_TR_RANDTO     0x8E1E60u
#define GH_RVA_TR_RAND100    0x8E1C50u
#define GH_RVA_TR_STEAL     0x8D7C40u

/* —— v1.0.9 杀敌金币倍数（kill_gold_ratio，默认 10）——
 * 写点定位（2.2.4 / build 25767968，tools/analyze/kill_gold_*）：
 *  StageManager$$GetAtkShowDatas（0x7E40A0..0x7E5F40）内部逐单位计算本次击杀
 *  待入账金币：
 *    r14d = TbPlayer[+0x130]（基础金币） + 被击杀单位[+0xd4]
 *    r14d += r14d * CharEntity.GoldRatio / 100      ; 0x7E4F1B..0x7E4F30
 *    if (entity[+0x20] == 2 即 Camp)                ; 0x7E4F36 / 0x7E5405
 *        带 HardLevelSet.MoneyR 乘算 → add [r12+0x8c], r14d   ; 0x7E5460（8 字节）
 *    else
 *        直接                    → add [r13+0x8c], r14d   ; 0x7E592E（7 字节）
 *  这两个写点是**互斥分支**，同一个值只累加一次；[+0x8c] = 该单位本回合
 *  待入账金币（TurnAddGold）。真入账在 <DoAtkDone>d__490.MoveNext：
 *    mov rax,[rbx+0x48]; mov ecx,[rax+0x8c]; add [r14+0xb8], ecx  ← StageManager.Gold
 *  完全在战斗结算协程内，与商店(GPlayData.AddGold) / 事件(EventUtils.AddGold) /
 *  卡牌小游戏无关 ⇒ 只影响「杀敌金钱」。
 *  第三处 add [r13+0x8c], eax（0x7E4D55）走 CombatResult[+0x4c] 的另一条路径，
 *  来源未定性，**不 patch**（宁缺勿滥，避免误伤）。
 *  patch 手法：写点 7/8 字节整条替换为 `call stub` + nop 填充；stub 内
 *  `edx = g_kill_gold_ratio; imul r14d, edx; add [reg+0x8c], r14d; ret`，
 *  ratio 运行期热改无需重建 stub。ratio==1 时还原原字节（零侵入）。 */
#define GH_RVA_KILL_W1 0x7E5460u   /* add dword ptr [r12+0x8C], r14d  8 字节 */
#define GH_RVA_KILL_W2 0x7E592Eu   /* add dword ptr [r13+0x8C], r14d  7 字节 */

/* 收尾校验用的原始字节（fail-closed：不符则不装 patch） */
static const unsigned char GH_KILL_W1_ORIG[8] =
    {0x45, 0x01, 0xB4, 0x24, 0x8C, 0x00, 0x00, 0x00};
static const unsigned char GH_KILL_W2_ORIG[7] =
    {0x45, 0x01, 0xB5, 0x8C, 0x00, 0x00, 0x00};

/* cfg.Tables 里表字段的反汇编常量（getter 就是 mov rax,[rcx+off]; ret），
 * 只在 il2cpp 按名取偏移失败时兜底（cfg.Tables::ResolveRef 也印证了同一批偏移：
 * +0x38=TbMagic / +0x40=TbSkill / +0x58=TbConsts；字段声明顺序为
 * TbString,TbStage,TbStageGroupObs,TbPlayer,TbJob,TbMagic,TbSkill,TbStatus,TbItem,
 * TbConsts... → 0x10 起每项 8 字节，TbItem 落在第二个 8 字节 → +0x50）。 */
#define GH_OFF_TBSKILL 0x40
#define GH_OFF_TBMAGIC 0x38
#define GH_OFF_TBITEM  0x50

/* 命中率「内部放大」值：_calcHitRatio 在 HitRatio>100 时走溢出补偿分支，
 * 超出 100 的部分不会被压缩，因此 200 足以抵消任何常规 DodgeRatio（下限 10），
 * 使 _rand(100) < hitRatio 恒真。界面因为 GetShowHitRatio 的 >=100 → 100
 * 仍然显示 100%。 */
#define GH_HIT_OVERKILL 200

/* 偷窃判定必过：DoMagicFunc_StealItem 的 prob = StealRatio + GetRatio + 10，
 * 而 _rand100(prob) 是 Random.Range(0,100) < prob（0..99）→ prob≥100 恒真。
 * 钳 100 即可（加上 +10 的常量后必然 >100）。 */
#define GH_STEAL_GAIN 100

/* resolve 重试上限（每 500ms 一次 → 约 5 分钟）。表肯定在这之前就绪；留足冗余
 * 但不要永远空转。 */
#define GH_RESOLVE_TRIES 600

/* Accepted GameAssembly.dll builds. Same pair the other mods accept: the
 * stock build and the build patched by the unrelated "nightmare randomiser"
 * Mod. Anything else disables the Mod entirely (fail-closed). */
static const char *const gh_accepted_hashes[] = {
    "1fda3e3add16e97abfd8ced98c24de839f0b079db803ae6f5a7867800fe03f1e",
    "7333475e7176b1f7395c724470a80fffd08e706295ba58fbbf59f0105e6634f1",
    "6aea28ddb7951f5df240f2fecc8cb16a79a8ecd0eb1cdc2b1343798a02be198c",
    "d969e10b94b47d423c701c1dd5fa6986cc8c8c5774519a89b2605fe91285bc09",
    "b023545ec4bc3bc23bf6193e55bb61e8583173b9dc65b1dec2e452e580296b45"
};

static HMODULE self, ga;
static HANDLE logfile = INVALID_HANDLE_VALUE;

/* 本模块的 SHA256 白名单（known_build 用）。 */
#define HSLR_ACCEPTED_HASHES gh_accepted_hashes

/* 日志 / 长路径 / 构建校验 —— 四模块共用实现，见 shared/common.h。 */
#include "common.h"

static bool enabled, state_seen;
static GHConfig config;

/* 【伞形总控】读 hslr-wait-hotkey.ini 的 [features] goldhand 键（与伞形/
 * 倍速/加值同一处总控文件）。文件缺失或键不存在 = 视为开。mtime 变化热重载。 */
#define GH_UMBRELLA_INI L"hslr-wait-hotkey.ini"
static bool umbrella_enable = true;
static ULONGLONG umbrella_stamp;

/* 游戏内开关(enabled) && ini 子开关(config.enable) && 伞形总控 三重取与。 */
static bool feature_on(void) {
    return enabled && config.enable && umbrella_enable;
}

#define PTR(p, o) (*(void **)((BYTE *)(p) + (o)))
#define VAL(t, p, o) (*(t *)((BYTE *)(p) + (o)))
#define FN(rva, ret, ...) ((ret(*)(__VA_ARGS__))((BYTE *)ga + rva_of(rva)))

/* 【v2.9.0 免版本跟进】运行时重定位层（把"上一次重解时的 RVA"翻译成当前版本
 * 的 RVA）；锚点表 rva_anchors.h 由 tools/analyze/capture_fingerprints.py 生成。
 * 注意：本模块的 gh.get_* 函数指针直接由 rva_of() 赋值，因此**必须**先通过
 * check_fingerprints() 的模块级门（rva_all_ok）才会走到赋值处 —— 解析失败的
 * 锚点会让整个金之手卫星停用，而不是拿着 0 地址去调用。 */
#define RVA_LOG logmsg
#include "rva_reloc.h"

/* ------------------------------------------------------------------ */
/* Build identity                                                      */
/* ------------------------------------------------------------------ */

/* 复用的 fail-closed 指纹（与 pinattr 同一张表的子集，只做构建校验、不挂钩）
 * 定义见 fingerprints.h：gh_fingerprints[]。
 * 构建未变时 = 原逐点字节校验；构建未知时 = 重定位层的逐点结果。 */
static bool check_fingerprints(void) {
    if (rva_mode == RVA_MODE_NATIVE) {
        for (unsigned i = 0; i < sizeof(gh_fingerprints) / sizeof(gh_fingerprints[0]); ++i) {
            const GHFingerprint *f = &gh_fingerprints[i];
            if (memcmp((BYTE *)ga + f->rva, f->bytes, 16) != 0) {
                logmsg("Fingerprint mismatch %s RVA=%llx", f->name, (unsigned long long)f->rva);
                return false;
            }
        }
        logmsg("%u fingerprints verified", (unsigned)(sizeof(gh_fingerprints) / sizeof(gh_fingerprints[0])));
        return true;
    }
    if (!rva_all_ok()) {
        logmsg("reloc: %d of %u anchors could not be relocated; gold-hand disabled",
               rva_failed(), (unsigned)RVA_ANCHOR_COUNT);
        return false;
    }
    logmsg("reloc: all %u anchors relocated; gold-hand active on an unknown build",
           (unsigned)RVA_ANCHOR_COUNT);
    return true;
}

/* ------------------------------------------------------------------ */
/* Umbrella [features] master switch                                   */
/* ------------------------------------------------------------------ */

static void umbrella_features_reload(void) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, GH_UMBRELLA_INI);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ULONGLONG now = GetFileAttributesExW(p, GetFileExInfoStandard, &fad)
                        ? ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) |
                              fad.ftLastWriteTime.dwLowDateTime
                        : 0;
    if (now && now == umbrella_stamp) return;
    umbrella_stamp = now;
    /* 缺文件/缺键 = 视为开（GetPrivateProfileInt 默认 1），老配置行为不变 */
    DWORD v = GetPrivateProfileIntW(L"features", L"goldhand", 1, p);
    bool next = v != 0;
    static bool seen;
    if (!seen || next != umbrella_enable) {
        seen = true;
        umbrella_enable = next;
        logmsg("Umbrella [features] goldhand -> %d", next ? 1 : 0);
    }
}

/* ------------------------------------------------------------------ */
/* Mod gate (in-game switch) — same contract as pinattr/speedkey        */
/* ------------------------------------------------------------------ */

/* 回退白名单：ini mod_id 完全匹配不到条目时按这些已知 ID 自愈（v2.3.0 体系）。
 * 目前只有内置 UTF-8 一条（待机快捷键），启动时转 UTF-16。 */
static WCHAR gh_fallback_ids[2][GH_MOD_ID_MAX];
static int gh_fallback_count;
static bool gh_fallback_active;

static void gh_fallback_init(void) {
    gh_fallback_count = 0;
    const char *utf8 = GH_DEFAULT_MOD_ID_UTF8;
    WCHAR *dst = gh_fallback_ids[gh_fallback_count];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                                dst, GH_MOD_ID_MAX);
    if (n > 1) gh_fallback_count++;
}

static bool id_matches(void *s) {
    if (!s || config.mod_id_chars <= 0) return false;
    int slen = VAL(int, s, 0x10);
    if (slen == config.mod_id_chars)
        return !memcmp((BYTE *)s + 0x14, config.mod_id, (size_t)config.mod_id_chars * 2);
    /* 【v2.5.2 工坊条目】游戏把工坊订阅条目的主键写成 "<modId>@<文件ID>"
     * （实测：待机快捷键@3802488917）→ 全等匹配打不中，订阅玩家气力/必偷全关。
     * 放宽为：前缀相同且紧跟 '@'。 */
    if (slen > config.mod_id_chars) {
        const WCHAR *p = (const WCHAR *)((BYTE *)s + 0x14);
        return !memcmp(p, config.mod_id, (size_t)config.mod_id_chars * 2) &&
               p[config.mod_id_chars] == L'@';
    }
    return false;
}

static bool fallback_matches(void *s) {
    if (!s) return false;
    int slen = VAL(int, s, 0x10);
    if (slen <= 0 || slen >= GH_MOD_ID_MAX) return false;
    for (int i = 0; i < gh_fallback_count; ++i)
        if (gh_fallback_ids[i][slen] == 0 &&
            !memcmp((BYTE *)s + 0x14, gh_fallback_ids[i], (size_t)slen * 2))
            return true;
    return false;
}

/* Queried on Unity's main tick, never from the worker thread. */
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
    void *fb_mod = NULL;   /* 回退白名单命中的第一个条目 */
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
                    if (!fb_mod && fallback_matches(sid)) fb_mod = mod;
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
    /* 自愈：完整条目列表里主 mod_id 一个都没匹配上、但存在自家已知 ID 的
     * 条目 → 按那个条目的开关状态生效（残留旧 ini 的玩家免重装自愈）。 */
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
        enabled = next;
        last_reason = reason;
        last_modflag = modflag;
        last_compatible = compatible;
        last_errors = errors;
        char idbuf[GH_MOD_ID_MAX * 4];
        wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
        logmsg("Mod %s; id=%s; reason=%s; enabled=%d compatible=%d validationItems=%d",
               enabled ? "enabled" : "disabled", idbuf, reasons[reason], modflag, compatible, errors);
        if (used_fallback && !gh_fallback_active)
            logmsg("FALLBACK: ini mod_id \"%s\" matches no game entry; using the "
                   "built-in id instead (stale hslr-goldhand.ini? re-run the "
                   "installer to sync mod_id)", idbuf);
        gh_fallback_active = used_fallback;
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
    path_for(p, GH_CONFIG_FILE);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    config_stamp = GetFileAttributesExW(p, GetFileExInfoStandard, &fad)
                       ? ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) |
                             fad.ftLastWriteTime.dwLowDateTime
                       : 0;
}

static void config_log_summary(bool reload) {
    char idbuf[GH_MOD_ID_MAX * 4];
    wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
    logmsg("Config %s; enable=%d; hitRatio=%d%s; expend=%d; stealAlways=%d; "
           "verbose=%d; trace=%d; force=%d; errors=%d; modId=%s",
           reload ? "reloaded" : "loaded", config.enable, config.hit_ratio,
           config.hit_ratio >= 100 ? " (=guaranteed hit)" : "",
           config.expend, config.steal_always,
           config.verbose_log, config.trace, config.force_enable, config.errors, idbuf);
}

static void config_setup(void) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, GH_CONFIG_FILE);
    if (!gh_config_load(&config, p)) {
        if (gh_config_write_default(p))
            logmsg("Wrote default config %ls", GH_CONFIG_FILE);
        gh_config_load(&config, p);
    }
    config_remember_stamp();
    umbrella_features_reload();
    config_log_summary(false);
}

static void config_maybe_reload(void) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, GH_CONFIG_FILE);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ULONGLONG now = GetFileAttributesExW(p, GetFileExInfoStandard, &fad)
                        ? ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) |
                              fad.ftLastWriteTime.dwLowDateTime
                        : 0;
    if (now == config_stamp) return;
    config_stamp = now;
    GHConfig draft;
    if (gh_config_load(&draft, p)) {
        bool was = config.enable;
        config = draft;
        config_log_summary(true);
        if (was != config.enable)
            logmsg("Ini switch 'enable' -> %d; gold hand patching %s",
                   config.enable ? 1 : 0, config.enable ? "active" : "disabled");
    } else {
        logmsg("Config reload failed (kept previous)");
    }
}

/* ------------------------------------------------------------------ */
/* IL2CPP C API bindings (resolved by export name from GameAssembly)   */
/* ------------------------------------------------------------------ */

static void *(*i_domain_get)(void);
static void **(*i_domain_assemblies)(void *, size_t *);
static void *(*i_assembly_image)(void *);
static size_t (*i_image_class_count)(void *);
static void *(*i_image_class)(void *, size_t);
static const char *(*i_class_name)(void *);
static void *(*i_class_parent)(void *);
static void *(*i_class_field)(void *, const char *);
static void *(*i_class_fields)(void *, void **);
static const char *(*i_field_name)(void *);
static uint32_t (*i_field_flags)(void *);
static int32_t (*i_field_offset)(void *);
static void *(*i_field_type)(void *);
static const char *(*i_type_name)(void *);
static void (*i_field_static_get_value)(void *, void *);
static void *(*i_type_class)(void *);

#define IL2CPP_FIELD_ATTRIBUTE_STATIC 0x10

static bool il2cpp_bind(void) {
    struct { const char *name; void **target; bool required; } table[] = {
        {"il2cpp_domain_get",              (void **)&i_domain_get,           true},
        {"il2cpp_domain_get_assemblies",   (void **)&i_domain_assemblies,    true},
        {"il2cpp_assembly_get_image",      (void **)&i_assembly_image,       true},
        {"il2cpp_image_get_class_count",   (void **)&i_image_class_count,    true},
        {"il2cpp_image_get_class",         (void **)&i_image_class,          true},
        {"il2cpp_class_get_name",          (void **)&i_class_name,           true},
        {"il2cpp_class_get_parent",        (void **)&i_class_parent,         false},
        {"il2cpp_class_get_field_from_name", (void **)&i_class_field,        true},
        {"il2cpp_class_get_fields",        (void **)&i_class_fields,         true},
        {"il2cpp_field_get_name",          (void **)&i_field_name,           false},
        {"il2cpp_field_get_flags",         (void **)&i_field_flags,          true},
        {"il2cpp_field_get_offset",        (void **)&i_field_offset,         true},
        {"il2cpp_field_get_type",          (void **)&i_field_type,           true},
        {"il2cpp_type_get_name",           (void **)&i_type_name,            false},
        {"il2cpp_field_static_get_value",  (void **)&i_field_static_get_value, true},
        {"il2cpp_type_get_class_or_element_class", (void **)&i_type_class,   true},
    };
    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        *table[i].target = (void *)GetProcAddress(ga, table[i].name);
        if (!*table[i].target && table[i].required) {
            logmsg("Missing IL2CPP export %s; goldhand cannot resolve names", table[i].name);
            return false;
        }
    }
    return true;
}

static void for_each_class(void (*cb)(void *klass, void *ud), void *ud) {
    void *domain = i_domain_get();
    if (!domain) return;
    size_t n = 0;
    void **asms = i_domain_assemblies(domain, &n);
    if (!asms) return;
    for (size_t a = 0; a < n; ++a) {
        void *img = i_assembly_image(asms[a]);
        if (!img) continue;
        size_t cnt = i_image_class_count(img);
        for (size_t c = 0; c < cnt; ++c) {
            void *k = i_image_class(img, c);
            if (k) cb(k, ud);
        }
    }
}

/* 按简单名找类；validate 用来在同名的异命名空间类里挑正主。 */
struct gh_find_ctx { const char *name; bool (*validate)(void *); void *result; };
static void gh_find_cb(void *klass, void *ud) {
    struct gh_find_ctx *ctx = (struct gh_find_ctx *)ud;
    if (ctx->result) return;
    const char *nm = i_class_name(klass);
    if (!nm || strcmp(nm, ctx->name)) return;
    if (ctx->validate && !ctx->validate(klass)) return;
    ctx->result = klass;
}
static void *find_class(const char *name, bool (*validate)(void *)) {
    struct gh_find_ctx ctx = {name, validate, NULL};
    for_each_class(gh_find_cb, &ctx);
    return ctx.result;
}

/* 类里是否真有这个实例字段（validate 用，防同名类）。 */
static bool has_instance_field(void *klass, const char *name) {
    void *f = i_class_field(klass, name);
    if (!f) return false;
    return !(i_field_flags(f) & IL2CPP_FIELD_ATTRIBUTE_STATIC);
}

/* ------------------------------------------------------------------ */
/* Name resolution (game thread only)                                  */
/* ------------------------------------------------------------------ */

/* 目标行（数据面调研实锁）：
 *   DesSkill Id 36 = 金之手(Expend 3→1) / Id 38 = 高级金之手(Expend 2→1)
 *   DesMagic Id 3601/3801 HitRatio 90→100（Group 0/1 两行） */
static const int gh_skill_ids[] = {36, 38};
static const int gh_magic_ids[] = {3601, 3801};

static struct {
    void *tblmgr_klass, *tables_klass, *tbskill_klass, *tbmagic_klass, *tbitem_klass;
    void *desskill_klass, *desmagic_klass, *desitem_klass;
    /* —— 表对象入口：RVA getter（v1.0.2 起；见文件头注释）—— */
    void *(*get_tblmgr)(void);         /* Game$$get_TableMgr()      静态无参 */
    void *(*get_tables)(void *mgr);    /* TableMgr$$get_Tables()    实例 */
    void *(*get_tbskill)(void *tables);/* cfg.Tables$$get_TbSkill() 实例 */
    void *(*get_tbmagic)(void *tables);/* cfg.Tables$$get_TbMagic() 实例 */
    /* —— 游戏自己的按 id 取行入口（自证回读；游戏内部同款调用）—— */
    void *(*get_skill)(void *mgr, int id);   /* TableMgr$$GetSkill(id) */
    void *(*get_magic)(void *mgr, int id);   /* TableMgr$$GetMagic(id) */
    void *(*get_item)(void *mgr, int id);    /* TableMgr$$GetItem(id) */
    /* —— 字段偏移（按名从 il2cpp 取；失败时用上面的反汇编常量兜底）—— */
    int32_t m_tables_off;                  /* TableMgr.m_Tables */
    int32_t tbskill_off, tbmagic_off, tbitem_off;  /* cfg.Tables 的表字段 */
    /* 表类里的两个 Dictionary<ValueDif,T> 不再按名取偏移——v1.0.1 起直接
     * 遍历表类的实例引用字段做结构自检（字段名/位置随版本变，结构才是硬约束）。 */
    int32_t ds_group_off, ds_id_off, ds_expend_off, ds_hit_off;
    int32_t dm_group_off, dm_id_off, dm_hit_off;
    int32_t di_id_off, di_ratio_off;       /* DesItem 的行号 / GetRatio(+0x78) */
    bool ready;
} gh;

/* 表行类型（决定用哪个 row patcher）。 */
enum { GH_TABLE_SKILL = 0, GH_TABLE_MAGIC = 1, GH_TABLE_ITEM = 2 };

/* --- 类校验器（防同名类）：要求持有本 Mod 需要的字段 --- */
static bool val_tables(void *k) {
    return has_instance_field(k, "<TbSkill>k__BackingField") &&
           has_instance_field(k, "<TbMagic>k__BackingField");
}
/* 类里是否存在某个「类型名含 needle」的实例字段（结构校验，不依赖字段名）。 */
static bool has_field_of_type(void *klass, const char *needle) {
    if (!i_field_type || !i_type_name) return false;
    void *iter = NULL, *field;
    while ((field = i_class_fields(klass, &iter))) {
        if (i_field_flags(field) & IL2CPP_FIELD_ATTRIBUTE_STATIC) continue;
        void *t = i_field_type(field);
        const char *tn = t ? i_type_name(t) : NULL;
        if (tn && strstr(tn, needle)) return true;
    }
    return false;
}

static bool val_tbskill(void *k) {
    /* 名字命中 或 结构命中（表类必然挂 Dictionary<ValueDif,T>） */
    return has_instance_field(k, "_dataList") || has_instance_field(k, "_resolvedCache") ||
           has_field_of_type(k, "Dictionary");
}
static bool val_tblmgr(void *k) {
    return has_instance_field(k, "m_Tables") && has_instance_field(k, "m_TableJsonCache");
}
static bool val_desskill(void *k) {
    return has_instance_field(k, "Expend") && has_instance_field(k, "MagicIDs") &&
           has_instance_field(k, "HitRatio");
}
static bool val_desmagic(void *k) {
    return has_instance_field(k, "MagicFunc") && has_instance_field(k, "HitRatio") &&
           has_instance_field(k, "AttrType");
}
static bool val_desitem(void *k) {
    /* GetRatio 是偷窃判定读的那个字段（DoMagicFunc_StealItem 的 desItem[+0x78]）。 */
    return has_instance_field(k, "GetRatio") && has_instance_field(k, "BuyPrice") &&
           has_instance_field(k, "Icon");
}

/* 取实例字段偏移；失败返回 -1。 */
static int32_t field_off(void *klass, const char *name) {
    void *f = i_class_field(klass, name);
    if (!f) return -1;
    int32_t off = i_field_offset(f);
    return off > 0 ? off : -1;
}

/* resolve 失败原因去重（每 500ms 重试一次，同一原因只打一行；原因变了再打）。
 * v1.0.1 的日志被 60 行同样的 "static singleton field not found" 刷屏，
 * 反而看不清「一个值都没钳上」这个关键事实。 */
static char g_last_reason[256];
static void resolve_fail(const char *fmt, ...) {
    char b[256];
    va_list v;
    va_start(v, fmt);
    vsnprintf(b, sizeof(b), fmt, v);
    va_end(v);
    if (!strcmp(b, g_last_reason)) return;
    strncpy(g_last_reason, b, sizeof(g_last_reason) - 1);
    g_last_reason[sizeof(g_last_reason) - 1] = '\0';
    logmsg("%s", b);
}

static bool resolve_all(void) {
    if (!ga) return false;
    if (!i_domain_get && !il2cpp_bind()) return false;

    gh.tables_klass = find_class("Tables", val_tables);
    gh.tbskill_klass = find_class("TbSkill", val_tbskill);
    gh.tbmagic_klass = find_class("TbMagic", val_tbskill);
    gh.tbitem_klass = find_class("TbItem", val_tbskill);
    gh.tblmgr_klass = find_class("TableMgr", val_tblmgr);
    gh.desskill_klass = find_class("DesSkill", val_desskill);
    gh.desmagic_klass = find_class("DesMagic", val_desmagic);
    gh.desitem_klass = find_class("DesItem", val_desitem);
    if (!gh.tables_klass || !gh.tbskill_klass || !gh.tbmagic_klass ||
        !gh.tblmgr_klass || !gh.desskill_klass || !gh.desmagic_klass) {
        resolve_fail("resolve: classes missing (tables=%p tbskill=%p tbmagic=%p tblmgr=%p "
                     "desskill=%p desmagic=%p)", gh.tables_klass, gh.tbskill_klass,
                     gh.tbmagic_klass, gh.tblmgr_klass, gh.desskill_klass, gh.desmagic_klass);
        return false;
    }
    /* 道具表/行类只服务「偷窃必中」。取不到时不算失败：本 Mod 的命中率与气力
     * 两项主功能照常工作，只记一行日志说明偷窃必中不可用。 */
    if (!gh.tbitem_klass || !gh.desitem_klass)
        resolve_fail("resolve: item table classes missing (tbitem=%p desitem=%p) — "
                     "steal-always unavailable", gh.tbitem_klass, gh.desitem_klass);
    /* v1.0.2 入口：不再假设「TableMgr 里有静态自引用字段」（那是错的，实机日志
     * 直接打脸），改用游戏自己的访问器 —— 每个都是「取个字段就返回」的 getter，
     * RVA 由反汇编实证、指纹锚点 + SHA256 白名单做 fail-closed。 */
    /* 【v2.9.0】全部经 rva_of()：构建未变时与原值完全相同，构建更新后由重定位
     * 层找回。解析失败的锚点在这里会得到 0 —— 但那种情况下 worker 早已在
     * check_fingerprints() 处整体停用本卫星，不会走到这里。 */
    gh.get_tblmgr = (void *(*)(void))((BYTE *)ga + rva_of(GH_RVA_GET_TABLEMGR));
    gh.get_tables = (void *(*)(void *))((BYTE *)ga + rva_of(GH_RVA_GET_TABLES));
    gh.get_tbskill = (void *(*)(void *))((BYTE *)ga + rva_of(GH_RVA_GET_TBSKILL));
    gh.get_tbmagic = (void *(*)(void *))((BYTE *)ga + rva_of(GH_RVA_GET_TBMAGIC));
    gh.get_skill = (void *(*)(void *, int))((BYTE *)ga + rva_of(GH_RVA_GETSKILL));
    gh.get_magic = (void *(*)(void *, int))((BYTE *)ga + rva_of(GH_RVA_GETMAGIC));
    gh.get_item = (void *(*)(void *, int))((BYTE *)ga + rva_of(GH_RVA_GETITEM));
    /* 字段偏移仍优先按名从元数据取（表对象内部布局）；三个表字段取不到时用
     * getter 反汇编常量兜底（两者等价，getter 就是 mov rax,[rcx+off]; ret）。 */
    gh.m_tables_off = field_off(gh.tblmgr_klass, "m_Tables");
    gh.tbskill_off = field_off(gh.tables_klass, "<TbSkill>k__BackingField");
    if (gh.tbskill_off <= 0) gh.tbskill_off = GH_OFF_TBSKILL;
    gh.tbmagic_off = field_off(gh.tables_klass, "<TbMagic>k__BackingField");
    if (gh.tbmagic_off <= 0) gh.tbmagic_off = GH_OFF_TBMAGIC;
    gh.tbitem_off = field_off(gh.tables_klass, "<TbItem>k__BackingField");
    if (gh.tbitem_off <= 0) gh.tbitem_off = GH_OFF_TBITEM;
    gh.ds_group_off = field_off(gh.desskill_klass, "Group");
    gh.ds_id_off = field_off(gh.desskill_klass, "Id");
    gh.ds_expend_off = field_off(gh.desskill_klass, "Expend");
    gh.ds_hit_off = field_off(gh.desskill_klass, "HitRatio");
    gh.dm_group_off = field_off(gh.desmagic_klass, "Group");
    gh.dm_id_off = field_off(gh.desmagic_klass, "Id");
    gh.dm_hit_off = field_off(gh.desmagic_klass, "HitRatio");
    if (gh.desitem_klass) {
        gh.di_id_off = field_off(gh.desitem_klass, "Id");
        gh.di_ratio_off = field_off(gh.desitem_klass, "GetRatio");
    }
    if (gh.ds_group_off < 0 || gh.ds_id_off < 0 ||
        gh.ds_expend_off < 0 || gh.ds_hit_off < 0 ||
        gh.dm_group_off < 0 || gh.dm_id_off < 0 || gh.dm_hit_off < 0) {
        resolve_fail("resolve: field offsets missing (ds_g=%d ds_id=%d ds_exp=%d ds_hit=%d "
                     "dm_g=%d dm_id=%d dm_hit=%d)",
                     gh.ds_group_off, gh.ds_id_off, gh.ds_expend_off, gh.ds_hit_off,
                     gh.dm_group_off, gh.dm_id_off, gh.dm_hit_off);
        return false;
    }
    gh.ready = true;
    logmsg("resolve ok (entry=getters): mTables=+%d tbSkill=+%d tbMagic=+%d tbItem=+%d "
           "ds(group=%d id=%d expend=%d hit=%d) dm(group=%d id=%d hit=%d) di(id=%d ratio=%d)",
           gh.m_tables_off, gh.tbskill_off, gh.tbmagic_off, gh.tbitem_off,
           gh.ds_group_off, gh.ds_id_off, gh.ds_expend_off, gh.ds_hit_off,
           gh.dm_group_off, gh.dm_id_off, gh.dm_hit_off,
           gh.di_id_off, gh.di_ratio_off);
    return true;
}

/* ------------------------------------------------------------------ */
/* Patch engine                                                        */
/* ------------------------------------------------------------------ */

/* 只读安全网：确认 [p, p+n) 已提交且可读。
 * 字段/容器猜错时指针可能是垃圾值——先过这一关再解引用，宁可不干活也不崩。 */
static bool readable(const void *p, size_t n) {
    if (!p || !n) return false;
    const BYTE *q = (const BYTE *)p;
    size_t done = 0;
    while (done < n) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)(q + done), &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_WRITECOPY |
                             PAGE_EXECUTE_READWRITE)))
            return false;
        size_t region = (size_t)((const BYTE *)mbi.BaseAddress + mbi.RegionSize - (q + done));
        if (!region) return false;
        done += region;
    }
    return true;
}

/* 原值台账：首次钳某对象某字段时记录，关闭沿按需还原。
 * v1.0.1 起不再区分「原始行/缓存产物」——还原前做三重校验，任一项不过就跳过
 * （宁可留新值，也不写坏可能已被回收的内存）：
 *   ① 地址当前仍可读  ② 对象头 klass 与记录时一致  ③ 当前值仍是「我们写下去的值」
 * v1.0.3：道具表整表钳 GetRatio（每行一个字段），台账上限随之放大。 */
#define GH_ORIG_MAX 4096
typedef struct {
    void *obj;
    void *klass;      /* 记录时的对象头（复核用，防对象被回收后内存被复用） */
    int32_t off;
    int orig;         /* 原值 */
    int patched;      /* 我们写下去的值 */
} GHOrig;
static GHOrig g_orig[GH_ORIG_MAX];
static int g_orig_count;
static int g_writes;

static GHOrig *orig_find(void *obj, int32_t off) {
    for (int i = 0; i < g_orig_count; ++i)
        if (g_orig[i].obj == obj && g_orig[i].off == off) return &g_orig[i];
    return NULL;
}

static void orig_remember(void *obj, int32_t off, int cur, int patched) {
    GHOrig *e = orig_find(obj, off);
    if (e) { e->patched = patched; return; }
    if (g_orig_count >= GH_ORIG_MAX) return;
    g_orig[g_orig_count].obj = obj;
    g_orig[g_orig_count].klass = *(void **)obj;
    g_orig[g_orig_count].off = off;
    g_orig[g_orig_count].orig = cur;
    g_orig[g_orig_count].patched = patched;
    ++g_orig_count;
}

static void restore_all(void) {
    int done = 0;
    for (int i = 0; i < g_orig_count; ++i) {
        GHOrig *o = &g_orig[i];
        if (!o->obj || !readable(o->obj, 8)) continue;
        if (*(void **)o->obj != o->klass) continue;
        if (VAL(int, o->obj, o->off) != o->patched) continue;
        VAL(int, o->obj, o->off) = o->orig;
        ++done;
        if (config.verbose_log)
            logmsg("restored obj=%016llx +%d -> %d",
                   (unsigned long long)(uintptr_t)o->obj, o->off, o->orig);
    }
    /* 台账清空：重新打开后按新目标值重记原值（原值不变，幂等） */
    g_orig_count = 0;
    logmsg("feature off: %d value(s) restored to the original", done);
}

/* 行对象身份：优先对象头 klass 指针精确匹配；merge 产物若派生自 DesSkill/
 * DesMagic，用「类名包含」兜底（IL2CPP 派生类布局 = 基类字段在前，同名偏移
 * 依然成立）。读类名前先确认指针可读，猜错也只是判否。 */
static bool is_kind(void *obj, void *klass, const char *name) {
    if (!obj) return false;
    void *k = *(void **)obj;
    if (k == klass) return true;
    if (!name || !k || !readable(k, 8)) return false;
    const char *nm = i_class_name ? i_class_name(k) : NULL;
    return nm && strstr(nm, name) != NULL;
}

/* DesMagic.HitRatio 的「实际写入值」——这是命中公式 (_calcHitRatio) 的输入。
 * ini 的 hit_ratio 是「期望命中率」（也是界面显示值）：>=100 时写 GH_HIT_OVERKILL，
 * 触发游戏的溢出补偿分支从而无视对方闪避；<100（例如回原版 90）则原样写。 */
static int gh_hit_write_value(void) {
    return config.hit_ratio >= 100 ? GH_HIT_OVERKILL : config.hit_ratio;
}

/* 钳一个 DesSkill 行：返回是否真的写了值。 */
static bool patch_skill_row(void *row, const char *where) {    if (!is_kind(row, gh.desskill_klass, "DesSkill")) return false;
    int id = VAL(int, row, gh.ds_id_off);
    int group = VAL(int, row, gh.ds_group_off);
    bool hit_row = false;
    for (unsigned i = 0; i < sizeof(gh_skill_ids) / sizeof(gh_skill_ids[0]); ++i)
        if (gh_skill_ids[i] == id) { hit_row = true; break; }
    if (!hit_row) return false;

    bool wrote = false;
    int *expend = (int *)((BYTE *)row + gh.ds_expend_off);
    if (*expend != config.expend && config.expend > 0) {
        if (config.verbose_log)
            logmsg("DesSkill Id=%d Group=%d Expend %d -> %d (%s)", id, group,
                   *expend, config.expend, where);
        orig_remember(row, gh.ds_expend_off, *expend, config.expend);
        *expend = config.expend;
        wrote = true;
        ++g_writes;
    }
    /* DesSkill.HitRatio 只用于技能说明的「命中率 N%」显示，写用户期望值即可
     * （100）。真正参与公式的是 DesMagic.HitRatio —— 见 patch_magic_row。 */
    int *hit = (int *)((BYTE *)row + gh.ds_hit_off);
    if (*hit != config.hit_ratio) {
        if (config.verbose_log)
            logmsg("DesSkill Id=%d Group=%d HitRatio %d -> %d (%s)", id, group,
                   *hit, config.hit_ratio, where);
        orig_remember(row, gh.ds_hit_off, *hit, config.hit_ratio);
        *hit = config.hit_ratio;
        wrote = true;
        ++g_writes;
    }
    return wrote;
}

/* 钳一个 DesMagic 行。 */
static bool patch_magic_row(void *row, const char *where) {
    if (!is_kind(row, gh.desmagic_klass, "DesMagic")) return false;
    int id = VAL(int, row, gh.dm_id_off);
    int group = VAL(int, row, gh.dm_group_off);
    bool hit_row = false;
    for (unsigned i = 0; i < sizeof(gh_magic_ids) / sizeof(gh_magic_ids[0]); ++i)
        if (gh_magic_ids[i] == id) { hit_row = true; break; }
    if (!hit_row) return false;

    int want = gh_hit_write_value();
    int *hit = (int *)((BYTE *)row + gh.dm_hit_off);
    if (*hit != want) {
        if (config.verbose_log)
            logmsg("DesMagic Id=%d Group=%d HitRatio %d -> %d (ini %d%s) (%s)", id, group,
                   *hit, want, config.hit_ratio,
                   want != config.hit_ratio ? ", overkill for guaranteed hit" : "", where);
        orig_remember(row, gh.dm_hit_off, *hit, want);
        *hit = want;
        ++g_writes;
        return true;
    }
    return false;
}

/* 钳一个 DesItem 行：把 GetRatio（DoMagicFunc_StealItem 的 desItem[+0x78]）
 * 钳到 GH_STEAL_GAIN，使偷窃判定 prob = StealRatio + GetRatio + 10 ≥ 100
 * （_rand100 是 Random.Range(0,100) < prob，prob≥100 恒真）→ 金之手必定偷到。
 * 白名单＝整表（金之手的偷窃目标在开战前不可知，只能全表覆盖）；用 ini 的
 * steal_always=0 可以整体关掉。 */
static bool patch_item_row(void *row, const char *where) {
    if (!gh.desitem_klass || gh.di_ratio_off <= 0) return false;
    if (!is_kind(row, gh.desitem_klass, "DesItem")) return false;
    int *ratio = (int *)((BYTE *)row + gh.di_ratio_off);
    if (*ratio >= GH_STEAL_GAIN) return false;   /* 已经是「必偷」值：幂等跳过 */
    int id = gh.di_id_off > 0 ? VAL(int, row, gh.di_id_off) : -1;
    if (config.verbose_log)
        logmsg("DesItem Id=%d GetRatio %d -> %d (%s)", id, *ratio, GH_STEAL_GAIN, where);
    orig_remember(row, gh.di_ratio_off, *ratio, GH_STEAL_GAIN);
    *ratio = GH_STEAL_GAIN;
    ++g_writes;
    return true;
}

/* 扫一个 Dictionary<ValueDif,T>：按 0x18 的精确 Entry 步长读 value 槽。
 * 结构自检（entries 是合法 Il2CppArray、count ∈ [0, length]）不过就当「不是字典」
 * 静默跳过——表类里的非字典字段会被顺手试到，自检就是它们的守门人。 */
static void patch_dict(void *dict, int kind, const char *where) {
    if (!dict) return;
    if (!readable(dict, 0x28)) return;                /* buckets/entries/count */
    void *entries = PTR(dict, 0x18);
    if (!entries || !readable(entries, 0x20)) return; /* Il2CppArray 头 */
    int len = VAL(int, entries, 0x18);                /* 数组 max_length */
    int count = VAL(int, dict, 0x20);                 /* 字典 _count */
    if (len <= 0 || len > 32768) return;
    if (count < 0 || count > len) return;
    BYTE *data = (BYTE *)entries + 0x20;
    if (!readable(data, (size_t)len * 0x18)) return;
    for (int i = 0; i < len; ++i) {
        void *row = *(void **)(data + 0x10 + (size_t)i * 0x18);
        if (kind == GH_TABLE_SKILL) patch_skill_row(row, where);
        else if (kind == GH_TABLE_MAGIC) patch_magic_row(row, where);
        else patch_item_row(row, where);
    }
}

/* 遍历表类的实例引用字段，把每个「像字典」的字段都扫一遍。
 * 不依赖字段名：v1.0.0 按名取 _dataList/_resolvedCache 的偏移，既漏掉了第二个
 * 字典（其实是 Dictionary 却被当成 List 遍历 → 静默空转），也扛不住版本改名。
 * 同时沿继承链上溯（表类常把容器声明在泛型基类里），重复扫到只是空转。 */
static void patch_table(void *tb, int kind) {
    if (!tb || !readable(tb, 8)) return;
    void *klass = *(void **)tb;
    int idx = 0;
    for (int depth = 0; klass && depth < 8; ++depth) {
        void *iter = NULL, *field;
        while ((field = i_class_fields(klass, &iter))) {
            if (i_field_flags(field) & IL2CPP_FIELD_ATTRIBUTE_STATIC) continue;
            int32_t off = i_field_offset(field);
            if (off <= 0) continue;
            char where[32];
            snprintf(where, sizeof(where), "field#%d", idx++);
            patch_dict(PTR(tb, off), kind, where);
        }
        klass = i_class_parent ? i_class_parent(klass) : NULL;
    }
}

/* 表对象链路：Game.get_Tables → cfg.Tables → TbSkill/TbMagic。
 * getter 优先（反汇编实证的 RVA），字段偏移兜底；每一步都过 readable() 只读
 * 安全网，链路任何一环拿不到就返回 false（= 表还没初始化好，下一轮再来）。 */
static bool table_chain(void **mgr_out, void **tables_out, void **skill_out,
                        void **magic_out, void **item_out) {
    if (!gh.get_tblmgr) return false;
    void *mgr = gh.get_tblmgr();                     /* Game$$get_TableMgr() */
    if (!mgr || !readable(mgr, 0x30)) return false;
    void *tables = gh.get_tables ? gh.get_tables(mgr) : NULL;
    if (!tables && gh.m_tables_off > 0) tables = PTR(mgr, gh.m_tables_off);
    if (!tables || !readable(tables, (size_t)gh.tbskill_off + 8)) return false;
    void *tb_skill = gh.get_tbskill ? gh.get_tbskill(tables) : NULL;
    if (!tb_skill) tb_skill = PTR(tables, gh.tbskill_off);
    void *tb_magic = gh.get_tbmagic ? gh.get_tbmagic(tables) : NULL;
    if (!tb_magic) tb_magic = PTR(tables, gh.tbmagic_off);
    /* 道具表没有专门的 getter（cfg.Tables 里只有一个 backing field），按字段取。 */
    void *tb_item = gh.tbitem_off > 0 ? PTR(tables, gh.tbitem_off) : NULL;
    *mgr_out = mgr;
    *tables_out = tables;
    *skill_out = tb_skill;
    *magic_out = tb_magic;
    if (item_out) *item_out = tb_item;
    return true;
}

/* 端到端自证：用游戏自己的 TableMgr::GetSkill/GetMagic/GetItem 按 id 回读行，
 * 打印它们现在看到的 Expend / HitRatio / GetRatio。这几条就是 DoAtkSkillCost /
 * CheckSkillCost / _calcHitRatio / DoMagicFunc_StealItem 底层吃到的那一批对象
 * （GetResolved 命中缓存 = 同一实例），所以日志里回读到目标值 = 游戏必然按新值工作。
 * 只在首轮、或回读值与目标值不符时打印（不刷屏）。 */
static void verify_chain(void *mgr) {
    if (!gh.get_skill || !gh.get_magic) return;
    static bool verified;
    bool mismatch = false;
    int want_hit = gh_hit_write_value();

    for (unsigned i = 0; i < sizeof(gh_skill_ids) / sizeof(gh_skill_ids[0]); ++i) {
        void *row = gh.get_skill(mgr, gh_skill_ids[i]);
        if (!row || !readable(row, (size_t)gh.ds_hit_off + 4)) continue;
        int ex = VAL(int, row, gh.ds_expend_off);
        int hr = VAL(int, row, gh.ds_hit_off);
        if (ex != config.expend || hr != config.hit_ratio) mismatch = true;
        if (!verified || mismatch)
            logmsg("verify: DesSkill Id=%d via TableMgr::GetSkill -> Expend=%d HitRatio=%d",
                   gh_skill_ids[i], ex, hr);
    }
    for (unsigned i = 0; i < sizeof(gh_magic_ids) / sizeof(gh_magic_ids[0]); ++i) {
        void *row = gh.get_magic(mgr, gh_magic_ids[i]);
        if (!row || !readable(row, (size_t)gh.dm_hit_off + 4)) continue;
        int hr = VAL(int, row, gh.dm_hit_off);
        if (hr != want_hit) mismatch = true;
        if (!verified || hr != want_hit)
            logmsg("verify: DesMagic Id=%d via TableMgr::GetMagic -> HitRatio=%d%s",
                   gh_magic_ids[i], hr,
                   hr == GH_HIT_OVERKILL ? " (overkill, guaranteed hit)" : "");
    }
    /* 道具表整表钳 GetRatio：抽头几个 id 回读即可证明（id 不存在就跳过）。 */
    if (config.steal_always && gh.get_item && gh.di_ratio_off > 0 && gh.desitem_klass) {
        int shown = 0;
        for (int id = 1; id <= 8 && shown < 3; ++id) {
            void *row = gh.get_item(mgr, id);
            if (!row || !readable(row, (size_t)gh.di_ratio_off + 4)) continue;
            if (!is_kind(row, gh.desitem_klass, "DesItem")) continue;
            int r = VAL(int, row, gh.di_ratio_off);
            if (r < GH_STEAL_GAIN) mismatch = true;
            if (!verified || r < GH_STEAL_GAIN) {
                logmsg("verify: DesItem Id=%d via TableMgr::GetItem -> GetRatio=%d", id, r);
                ++shown;
            }
        }
    }
    if (!verified) {
        verified = true;
        logmsg(mismatch ? "verify WARN: the game's own accessors still read old values"
                        : "verify ok: the game's own accessors now read the patched values");
    }
}

/* 一轮完整钳值（幂等：值已相等不写）。返回本轮写入的值个数。 */
static int patch_pass(void) {
    void *mgr = NULL, *tables = NULL, *tb_skill = NULL, *tb_magic = NULL, *tb_item = NULL;
    if (!table_chain(&mgr, &tables, &tb_skill, &tb_magic, &tb_item)) return 0;
    /* 链路首通留一行硬证据：日志里没有这行 = 根本没摸到表对象 */
    static bool chain_logged;
    if (!chain_logged) {
        chain_logged = true;
        logmsg("table chain ok: tableMgr=%p tables=%p tbSkill=%p tbMagic=%p tbItem=%p",
               mgr, tables, tb_skill, tb_magic, tb_item);
    }
    g_writes = 0;
    if (tb_skill) patch_table(tb_skill, GH_TABLE_SKILL);
    if (tb_magic) patch_table(tb_magic, GH_TABLE_MAGIC);
    if (config.steal_always && tb_item) patch_table(tb_item, GH_TABLE_ITEM);
    verify_chain(mgr);
    return g_writes;
}

/* ------------------------------------------------------------------ */
/* Runtime probe (ini trace=1; read-only, off by default)              */
/* ------------------------------------------------------------------ */
/* 「改 100 还 miss」的两种可能必须被分开：
 *   A) 命中 roll 就失败了（_rand(100) >= hitRatio）
 *   B) 命中成功但偷窃判定全败（DoMagicFunc_StealItem 把命中标志改回 0）
 * 这两条路各自打印真值：_calcHitRatio 的返回值、_rand(100) 的抽签结果、
 * 每次 _rand100(prob) 的 prob 与结果、以及 StealItem 进出的命中标志。
 * 全部只读（不发任何游戏调用），关掉 ini 里的 trace 就完全不挂钩。 */

#define GH_TRACE_CAP 600      /* 每类探针各 600 行上限，防止刷爆日志 */

static int (*tr_orig_calchit)(int, int, void **, void *, void *);
static int (*tr_orig_randto)(int);
static bool (*tr_orig_rand100)(int);
/* ⚠ DoMagicFunc_StealItem 是 static 方法、共 10 个参数（反汇编实证：
 * arg9=[rsp+0x70] 指向目标、arg10=[rsp+0x78] 指向 CombatResult）。
 * v1.0.4 只声明了 8 个 → 读错对象，且转发时原函数的 arg9/arg10 home 被垃圾覆盖。
 * 必须原样带齐 10 个，少一个都会破坏游戏本次调用。 */
typedef void (*gh_steal_fn)(void *, void *, void *, void *, void *, void *,
                            void *, void *, void *, void *);
static gh_steal_fn tr_orig_steal;
static int tr_lines[4];
static long volatile g_rand100_calls;   /* 偷窃循环跑了多少次判定（探针自证用） */

static bool tr_budget(int slot, const char *tag) {
    if (tr_lines[slot] >= GH_TRACE_CAP) return false;
    if (++tr_lines[slot] == GH_TRACE_CAP)
        logmsg("trace[%s]: reached %d lines, muting this probe", tag, GH_TRACE_CAP);
    return tr_lines[slot] <= GH_TRACE_CAP;
}

/* int _calcHitRatio(int mode = DesSkill.Type, int magicIdx,
 *                   void **desMagicRef, void *atkCastAttr, void *defender) */
static int tr_hk_calchit(int mode, int midx, void **magic_ref, void *atk, void *def) {
    int base = -1;
    if (magic_ref && readable(magic_ref, sizeof(void *)) && *magic_ref &&
        readable(*magic_ref, 0x28))
        base = VAL(int, *magic_ref, 0x24);        /* DesMagic.HitRatio */
    int r = tr_orig_calchit(mode, midx, magic_ref, atk, def);
    if (tr_budget(0, "calcHitRatio"))
        logmsg("trace _calcHitRatio(Type=%d idx=%d DesMagic.HitRatio=%d) -> %d",
               mode, midx, base, r);
    return r;
}

/* int _rand(int to) —— _castSkill 用 _rand(100) < hitRatio 判命中。 */
static int tr_hk_randto(int to) {
    int r = tr_orig_randto(to);
    if (to == 100 && tr_budget(1, "rand(100)"))
        logmsg("trace _rand(100) -> %d (hit needs %d < hitRatio)", r, r);
    return r;
}

/* bool _rand100(int prob) —— 偷窃/命中率类判定统一走这里。 */
static bool tr_hk_rand100(int prob) {
    ++g_rand100_calls;
    bool r = tr_orig_rand100(prob);
    if (tr_budget(2, "rand100"))
        logmsg("trace _rand100(prob=%d) -> %d%s", prob, r ? 1 : 0,
               (prob >= 100 && !r) ? "   <== prob>=100 却判假：随机策略异常" : "");
    return r;
}

/* 取对象的类名（只读；任何一步取不到就返回 "?"）。 */
static const char *obj_class_name(void *obj) {
    if (!obj || !readable(obj, sizeof(void *)) || !i_class_name) return "?";
    void *k = *(void **)obj;
    if (!k || !readable(k, sizeof(void *))) return "?";
    const char *nm = i_class_name(k);
    return nm ? nm : "?";
}

/* DoMagicFunc_StealItem 探针（10 参数，原样转发）。
 * 判定链：早退（判定==3 / 列表 null / Count==0，都不清零命中标志）
 *         → 循环 prob = 攻方StealRatio + 道具GetRatio + 10，_rand100(prob)
 *         → 全败 ⇒ CombatResult[+0x39] = 0 ⇒ 屏幕 MISS。 */
static void tr_hk_steal(void *a1, void *a2, void *a3, void *a4, void *a5,
                        void *a6, void *a7, void *a8, void *a9, void *a10) {
    void *tgt = NULL, *cr = NULL, *lst = NULL;
    const char *tname = "?";
    int count = -1, alen = -1, flag_before = -1;
    int ids[8];
    int nids = 0;
    long rolls_before = g_rand100_calls;
    if (a9 && readable(a9, sizeof(void *))) {
        tgt = *(void **)a9;
        if (tgt && readable(tgt, 0x48)) {
            tname = obj_class_name(tgt);
            lst = PTR(tgt, 0x40);
            if (lst && readable(lst, 0x1c)) {
                int c = VAL(int, lst, 0x18);
                if (c >= 0 && c <= 4096) count = c;
                /* 只读底层数组取 id 抽样（List<T> 的 _items 在 +0x10，
                 * Il2CppArray 的 data 在 +0x20）。不调用任何游戏函数。 */
                void *arr = PTR(lst, 0x10);
                if (arr && readable(arr, 0x24)) {
                    alen = VAL(int, arr, 0x18);
                    if (alen > 0 && alen < 4096 && readable((BYTE *)arr + 0x20, 4)) {
                        const int *d = (const int *)((const BYTE *)arr + 0x20);
                        for (int i = 0; i < alen && i < 8; ++i) {
                            if (!readable(d + i, 4)) break;
                            ids[nids++] = d[i];
                        }
                    }
                }
            }
        }
    }
    if (a10 && readable(a10, sizeof(void *))) {
        cr = *(void **)a10;
        if (cr && readable(cr, 0x40)) flag_before = VAL(unsigned char, cr, 0x39);
    }
    tr_orig_steal(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    if (tr_budget(3, "StealItem")) {
        int flag_after = (cr && readable(cr, 0x40)) ? VAL(unsigned char, cr, 0x39) : -1;
        char idbuf[120];
        idbuf[0] = '\0';
        for (int i = 0; i < nids; ++i) {
            char one[16];
            snprintf(one, sizeof(one), i ? ",%d" : "%d", ids[i]);
            strncat(idbuf, one, sizeof(idbuf) - strlen(idbuf) - 1);
        }
        logmsg("trace StealItem: target=%p(%s) items=%d(alloc=%d) ids[%s] "
               "stealRolls=%ld->%ld hitFlag %d -> %d%s",
               tgt, tname, count, alen, idbuf, rolls_before, g_rand100_calls,
               flag_before, flag_after,
               (flag_before == 1 && flag_after == 0)
                   ? "  <== 每个道具的偷窃判定全败 = 这就是 MISS" : "");
    }
}

/* ------------------------------------------------------------------ */
/* Kill-gold multiplier (kill_gold_ratio) — inline patch of the two    */
/* TurnAddGold write sites inside StageManager.GetAtkShowDatas.        */
/* ------------------------------------------------------------------ */

static int g_kill_state;                /* 0 = 未装, 1 = 已装 */
static void *g_kill_mem;                /* stub 内存（RWX）+ 同块 ratio 槽 */
static int *g_kill_ratio_slot;          /* 指向 g_kill_mem 内的 int（stub rip 相对寻址） */

/* stub 布局（两个写点各一份，仅第 3 条 add 的寄存器/长度不同）：
 *   8B 15 xx xx xx xx      mov edx, dword ptr [rip+disp]   ; edx = *ratio_slot
 *   44 0F AF F2            imul r14d, edx                  ; r14d *= ratio
 *   45 01 B4 24 8C 00 00 00  add dword ptr [r12+0x8c], r14d ; W1（8 字节）
 *   ——— 或 ———
 *   45 01 B5 8C 00 00 00   add dword ptr [r13+0x8c], r14d  ; W2（7 字节）
 *   C3                     ret
 * ratio_slot 与 stub 在同一块 VirtualAlloc 内存里 ⇒ rip 相对距离必在 ±2GB 内。
 */
#define GH_KILL_RATIO_OFFSET 0x00   /* 同块内存首部放 ratio */
#define GH_KILL_STUB1_OFFSET 0x40
#define GH_KILL_STUB2_OFFSET 0x80

static void kill_build_stub(BYTE *p, int *ratio_slot, bool w1) {
    BYTE *b = p;
    /* mov edx, dword ptr [rip+disp32] */
    *b++ = 0x8B;
    *b++ = 0x15;
    {
        intptr_t end = (intptr_t)(b + 4);
        intptr_t tgt = (intptr_t)ratio_slot;
        *(int32_t *)b = (int32_t)(tgt - end);
        b += 4;
    }
    /* imul r14d, edx  (REX.R=1 → r14d 目标，reg=edx 源) */
    *b++ = 0x44; *b++ = 0x0F; *b++ = 0xAF; *b++ = 0xF2;
    /* add dword ptr [reg+0x8c], r14d */
    if (w1) {
        *b++ = 0x45; *b++ = 0x01; *b++ = 0xB4; *b++ = 0x24;
        *b++ = 0x8C; *b++ = 0x00; *b++ = 0x00; *b++ = 0x00;
    } else {
        *b++ = 0x45; *b++ = 0x01; *b++ = 0xB5;
        *b++ = 0x8C; *b++ = 0x00; *b++ = 0x00; *b++ = 0x00;
    }
    *b++ = 0xC3;    /* ret */
}

/* 给写点整条指令换成 `E8 rel32` + nop 填充（保持总长度与原指令一致）。 */
static bool kill_write_patch(BYTE *site, size_t len, const BYTE *orig, BYTE *stub) {
    DWORD old;
    if (memcmp(site, orig, len) != 0) return false;
    intptr_t dist = (intptr_t)stub - ((intptr_t)site + 5);
    if (dist < INT32_MIN || dist > INT32_MAX) return false;   /* rel32 不可达 */
    if (!VirtualProtect(site, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    int32_t rel = (int32_t)dist;
    site[0] = 0xE8;
    memcpy(site + 1, &rel, 4);
    for (size_t i = 5; i < len; ++i) site[i] = 0x90;
    VirtualProtect(site, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, len);
    return true;
}

static bool kill_write_restore(BYTE *site, size_t len, const BYTE *orig) {
    /* 只在仍是我们的 call 桩时才还原（避免覆盖别人/游戏的改动） */
    if (site[0] != 0xE8) return false;
    DWORD old;
    if (!VirtualProtect(site, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(site, orig, len);
    VirtualProtect(site, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, len);
    return true;
}

/* 在 target 附近 ±1.5GB 内分配一块 RWX（供 rel32 call 可达）。Windows 下
 * 用带 hint 的 VirtualAlloc 反复试探，步进 16MB。返回 NULL 表示放弃。 */
static void *kill_alloc_near(void *target) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    uintptr_t t = (uintptr_t)target;
    /* 先试常规分配，若已在范围内则直接可用 */
    void *p = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (p) {
        intptr_t d = (intptr_t)p - (intptr_t)t;
        if (d > -0x70000000 && d < 0x70000000) return p;
        VirtualFree(p, 0, MEM_RELEASE);
    }
    for (int i = 1; i <= 96; ++i) {
        uintptr_t step = (uintptr_t)i * 0x1000000;   /* 16MB */
        for (int s = 0; s < 2; ++s) {
            uintptr_t hint = s ? (t + step) : (t - step);
            hint &= ~(gran - 1);
            void *q = VirtualAlloc((void *)hint, 0x1000,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (q) {
                intptr_t d = (intptr_t)q - (intptr_t)t;
                if (d > -0x70000000 && d < 0x70000000) return q;
                VirtualFree(q, 0, MEM_RELEASE);
            }
        }
    }
    return NULL;
}

static void kill_apply(int ratio) {
    if (ratio < 1) ratio = 1;

    /* 【v2.9.0】写点同样经重定位层；任一点找不回就整体不动（fail-safe）。 */
    uintptr_t w1r = rva_of(GH_RVA_KILL_W1), w2r = rva_of(GH_RVA_KILL_W2);
    if (ratio > 1 && (!w1r || !w2r)) {
        logmsg("kill-gold: write-site anchor could not be relocated; kill-gold unavailable");
        return;
    }
    BYTE *w1 = (BYTE *)ga + w1r;
    BYTE *w2 = (BYTE *)ga + w2r;
    bool want = ratio > 1;

    if (want && !g_kill_state) {
        if (!g_kill_mem) {
            /* RWX：ratio 槽 + 两个 stub 同块；必须落在两个写点 ±2GB 内
             * （rel32 call 可达），否则 patch 会被放弃。 */
            g_kill_mem = kill_alloc_near(w1);
            if (!g_kill_mem) { logmsg("kill-gold: no executable memory within range; skipped"); return; }
            g_kill_ratio_slot = (int *)((BYTE *)g_kill_mem + GH_KILL_RATIO_OFFSET);
            *g_kill_ratio_slot = 1;
        }
        *g_kill_ratio_slot = ratio;     /* stub 运行时读，热改即时生效 */
        BYTE *s1 = (BYTE *)g_kill_mem + GH_KILL_STUB1_OFFSET;
        BYTE *s2 = (BYTE *)g_kill_mem + GH_KILL_STUB2_OFFSET;
        kill_build_stub(s1, g_kill_ratio_slot, true);
        kill_build_stub(s2, g_kill_ratio_slot, false);
        FlushInstructionCache(GetCurrentProcess(), g_kill_mem, 0x200);

        bool ok1 = kill_write_patch(w1, sizeof(GH_KILL_W1_ORIG), GH_KILL_W1_ORIG, s1);
        bool ok2 = kill_write_patch(w2, sizeof(GH_KILL_W2_ORIG), GH_KILL_W2_ORIG, s2);
        if (ok1 && ok2) {
            g_kill_state = 1;
            logmsg("kill-gold ON: ratio=%d (write sites 0x%X, 0x%X patched; stub=%p)",
                   ratio, (unsigned)w1r, (unsigned)w2r, g_kill_mem);
        } else {
            /* 一半装上就回滚，保持原子性 */
            kill_write_restore(w1, sizeof(GH_KILL_W1_ORIG), GH_KILL_W1_ORIG);
            kill_write_restore(w2, sizeof(GH_KILL_W2_ORIG), GH_KILL_W2_ORIG);
            logmsg("kill-gold: patch failed (w1=%d w2=%d) bytes=%02X %02X %02X / %02X %02X %02X; "
                   "left unpatched (fail-safe)", ok1, ok2,
                   w1[0], w1[1], w1[2], w2[0], w2[1], w2[2]);
        }
    } else if (want && g_kill_state) {
        *g_kill_ratio_slot = ratio;     /* 已装：只改槽，热生效 */
    } else if (!want && g_kill_state) {
        kill_write_restore(w1, sizeof(GH_KILL_W1_ORIG), GH_KILL_W1_ORIG);
        kill_write_restore(w2, sizeof(GH_KILL_W2_ORIG), GH_KILL_W2_ORIG);
        g_kill_state = 0;
        logmsg("kill-gold OFF: original code restored");
    }
}

static void trace_install(void) {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        logmsg("trace: MH_Initialize failed status=%d; probe disabled", s);
        return;
    }
    struct { uintptr_t rva; void *fn; void **orig; const char *name; } list[] = {
        {GH_RVA_TR_CALCHIT, (void *)tr_hk_calchit, (void **)&tr_orig_calchit, "calcHitRatio"},
        {GH_RVA_TR_RANDTO,  (void *)tr_hk_randto,  (void **)&tr_orig_randto,  "rand(to)"},
        {GH_RVA_TR_RAND100, (void *)tr_hk_rand100, (void **)&tr_orig_rand100, "rand100"},
        {GH_RVA_TR_STEAL,   (void *)tr_hk_steal,   (void **)&tr_orig_steal,   "StealItem"},
    };
    int ok = 0;
    for (unsigned i = 0; i < sizeof(list) / sizeof(list[0]); ++i) {
        /* 【v2.9.0】探针点同样过重定位层；解析失败的点直接跳过（debug 路径）。 */
        uintptr_t at = rva_of(list[i].rva);
        if (!at) {
            logmsg("trace: hook %s skipped (anchor not relocated)", list[i].name);
            continue;
        }
        MH_STATUS cs = MH_CreateHook((BYTE *)ga + at, list[i].fn, list[i].orig);
        if (cs != MH_OK) {
            logmsg("trace: hook %s failed status=%d", list[i].name, cs);
            continue;
        }
        if (MH_EnableHook((BYTE *)ga + at) != MH_OK) {
            logmsg("trace: enable %s failed", list[i].name);
            continue;
        }
        ++ok;
    }
    logmsg(ok == 4 ? "trace: 4/4 probes armed (logs the hit roll and every steal roll)"
                   : "trace: only %d/4 probes armed", ok);
}

/* ------------------------------------------------------------------ */
/* Frame tick (driven by the umbrella DLL)                             */
/* ------------------------------------------------------------------ */

static bool process_focused = true;
/* 【v2.9.0 免版本跟进】模块级闸门：check_fingerprints() 通过（NATIVE 哈希命中，
 * 或 ADAPTIVE 下本模块全部锚点唯一找回）后由 worker 置真。未通过前所有 tick
 * 直接返回 —— 尤其是 read_mods() 里对 Game$$get_ModMgr 的直接调用，此时那个
 * 地址在 ADAPTIVE 下尚未解析，绝不能碰。 */
static bool gh_reloc_ok = false;

__declspec(dllexport) void gh_input_focus(int focus) {
    process_focused = focus ? true : false;
}

__declspec(dllexport) void gh_frame_tick(void) {
    /* 【v2.9.0】本卫星的锚点没全部找回时：整个卫星停用（含 read_mods 里对
     * Game$$get_ModMgr 的直接调用——那个地址此时是未解析的）。 */
    if (!gh_reloc_ok) return;
    if (!process_focused) return;
    static ULONGLONG last_work;
    ULONGLONG now = GetTickCount64();
    if (now == last_work) return;             /* 同帧去重 + 天然限频（≈每 ms 一次） */
    last_work = now;

    config_maybe_reload();
    umbrella_features_reload();
    read_mods();

    /* 解析阶段：表晚于 DLL 加载，每 500ms 重试一次，最多 GH_RESOLVE_TRIES 次
     * （≈5 分钟）。失败原因由 resolve_fail 去重，不会像 v1.0.1 那样刷屏。 */
    static int resolve_tries;
    static bool last_was_on = true;
    bool on = feature_on();
    if (!gh.ready) {
        static ULONGLONG last_try;
        if (!last_try || now - last_try >= 500) {
            last_try = now;
            if (++resolve_tries <= GH_RESOLVE_TRIES) {
                if (resolve_all()) resolve_tries = 0;   /* 成功即复位（表重建后能重试） */
            } else if (resolve_tries == GH_RESOLVE_TRIES + 1) {
                logmsg("resolve gave up after %d tries; gold hand inactive "
                       "(see the deduped 'resolve:' line above)", GH_RESOLVE_TRIES);
            }
        }
        return;
    }

    /* 功能翻转沿：关闭 → 还原原值；开启 → 台账清空、下一轮重新钳。 */
    static bool patched_seen;
    if (last_was_on && !on) {
        restore_all();
        kill_apply(1);          /* 关闭时还原写点，严格零侵入 */
        patched_seen = false;
        last_was_on = false;
        return;
    }
    last_was_on = on;
    if (!on) return;

    /* 杀敌金币倍数：每次 tick 对齐（ratio 热改即时生效；ratio>1 装桩，==1 还原）。 */
    kill_apply(config.kill_gold_ratio);

    /* 钳值轮：每 500ms 一次足够（新缓存对象最迟半秒被钳上）。 */
    static ULONGLONG last_patch;
    if (!last_patch || now - last_patch >= 500) {
        last_patch = now;
        int n = patch_pass();
        /* 首次真写进去时留一行硬证据：日志里没有这行 = 一个值都没钳上 */
        if (!patched_seen && n > 0) {
            patched_seen = true;
            logmsg("first patch pass: %d value(s) written", n);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Bootstrap                                                           */
/* ------------------------------------------------------------------ */

static DWORD WINAPI worker(void *unused) {
    (void)unused;
    hslr_log_open(GH_LOG_FILE);
    logmsg("HSLR GoldHand %s; data-patching + kill-gold xN satellite; game 2.2.4 / build 25767968",
           GH_VERSION);
    hslr_log_self_path();
    config_setup();
    gh_fallback_init();
    int build_ok = known_build();
    if (build_ok < 0) {
        logmsg("Unsupported GameAssembly.dll; Mod inactive and nothing is patched");
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
    /* 【v2.9.0】重定位层必须在任何 rva_of() 之前就绪。配置读伞形 ini（四模块共用）。 */
    {
        WCHAR up[HSLR_PATH_MAX];
        path_for(up, GH_UMBRELLA_INI);
        rva_reloc_configure(up);
    }
    rva_reloc_init(build_ok == 1);
    gh_reloc_ok = check_fingerprints();
    if (!gh_reloc_ok) {
        logmsg("Unsupported game build; Mod inactive and nothing is patched");
        return 0;
    }
    char idbuf[GH_MOD_ID_MAX * 4];
    wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
    logmsg("Ready (data patching + kill-gold x%d via umbrella tick; hooks: %s); "
           "waiting for modId=\"%s\" to be switched on in the game's Mod manager",
           config.kill_gold_ratio, config.trace ? "trace probe only" : "none", idbuf);
    if (config.trace) trace_install();
    return 0;
}

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
