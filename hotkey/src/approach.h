/* Auto-approach (自动接近) module for HSLR — v0.1.
 *
 * Include AFTER menu.h (needs menu_safe_context / menu_eligible_button /
 * menu_game_fields / menu_class_is) and after the host defines ga, PTR,
 * VAL, FN, logmsg, config. Unity main thread only (input tick hook).
 *
 * Semantics (v1 requirement baseline, settled 2026-09-20):
 *   A hotkey toggles the approach mode. While it is ON, whenever a player
 *   unit's action menu (AMenu) becomes ready:
 *     - if an enemy is already inside the unit's attack scope (engaged),
 *       do nothing: the menu is simply handed back to the player;
 *     - otherwise path toward the nearest enemy (GetMoveToPath), pick the
 *       furthest reachable chunk from the unit's move scope, click the
 *       native "move" button (InvokeClick) and, once the FSM lands in
 *       FsmState_Move, drive handleMoveAsync — the exact pipeline a player
 *       click would use. When the move finishes the FSM returns to AMenu
 *       and the mod never touches attack/skill/wait: the player decides.
 *   Each unit is auto-processed at most once per turn; pressing the hotkey
 *   again cancels the mode and hands everything back instantly.
 *
 * Fail-closed: every direct call goes through fingerprints_auto.h; if any
 * il2cpp class/field lookup fails the whole module disables itself and the
 * wait-hotkey keeps working untouched.
 *
 * Java 类比：本文件 = 一个带状态机的小型调度器（Spring StateMachine），
 * 外层闸门（menu_safe_context）相当于 @PreAuthorize，指纹校验相当于
 * 启动时的契约测试，字段按名解析相当于 JNDI lookup——找不到就拒绝启动。
 */

#ifndef HSLR_APPROACH_H
#define HSLR_APPROACH_H

#include "fingerprints_auto.h"

/* ------------------------------------------------------------------ */
/* Direct-call RVAs (all fingerprint-gated at startup)                  */
/* ------------------------------------------------------------------ */

#define AP_RVA_GET_CUR_ENTITY_ID  0x7FE260 /* StageManager$$get_CurEntityId (returns 64-bit id as stored) */
#define AP_RVA_GET_CUR_CHAR_ENT   0x7FE0D0 /* StageManager$$get_CurCharEntity（主指纹表锚点，approach 直接引用） */
#define AP_RVA_GET_TURN_ID        0x7FE810 /* StageManager$$get_TurnId */
#define AP_RVA_GET_CUR_CHUNK      0x7FE190 /* StageManager$$get_CurEntityChunkId */
#define AP_RVA_CUR_MOVE_TO        0x7DF280 /* StageManager$$CurEntityMoveTo(toChunkId, waitWalk, follow) */
/* 【2026-09-21 实测复盘】官方点击移动的前置三连 + 移动力（Player.log 实锤
 * "計算出來的路徑爲空, fromChunkId:45, toChunkId:230, moveRange:8"——
 * dest 超出移动力则 CalcMovePath 返回空、移动被取消）。 */
#define AP_RVA_GET_MOVE           0x7717C0 /* CharEntity$$get_Move —— 本回合移动力 */
#define AP_RVA_FSM_SET_MOVE_TO    0x9BB640 /* Fsm_StageData$$SetEntityMoveToChunkId(chunkId) */
#define AP_RVA_FSM_PUSH_MOVE      0x9BAE30 /* Fsm_StageData$$PushMoveState(frmChunkId, toChunkId) */
/* FsmState_Move$$handleMoveAsync 已弃用：async 方法直接调用不执行（见 pending_tick 注释）。 */
#define AP_RVA_GET_MOVE_TO_PATH   0x7E90F0 /* StageManager$$GetMoveToPath(entityId, toChunkId, moveRange) */
#define AP_RVA_MOVE_RANGE_CHUNKS  0x7C31F0 /* AIUtils$$GetMoveRangeChunkId(entityId, moveRange, canfly, camp) */
#define AP_RVA_MOVE_RANGE_CHUNKS2 0x7C2940 /* AIUtils$$GetMoveRangeChunkId2(entityId, moveRange, canfly, camp) */
#define AP_RVA_ENT_BY_CHUNK       0x7E6430 /* StageManager$$GetCharEntityByChunkId(chunkId, filtFlags) */
#define AP_RVA_GET_CHUNK_ID       0x771570 /* CharEntity$$get_ChunkId */
#define AP_RVA_GET_CAN_FLY        0x771560 /* CharEntity$$get_CanFly */
/* AIUtils$$FindTarget(0x7C2680) / AIUtils$$GetTarEntityListbyFilt(0x7C4700) /
 * CharEntity$$IsPlayerCamp(0x76B550) 早期版本曾在此直接调用，现行流程改用
 * 游戏自己的目标选择；这三个地址仍由 fingerprints_auto.h 逐点校验（构建
 * 一致性），所以不在这里重复定义。 */

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */

#define AP_STATE_IDLE        0   /* waiting for an AMenu to decide on   */
#define AP_STATE_PENDING_CLICK 1  /* move button clicked, FSM not yet in Move */

#define AP_PENDING_TIMEOUT_MS 6000
#define AP_PATH_RANGE_FALLBACK 99 /* GetMoveToPath moveRange upper bound  */

/* ap_fp_ok  = approach fingerprint table verified (set by the host's
 *             check_fingerprints on the worker thread).
 * ap_runtime_ok = fingerprints AND il2cpp metadata lookups all succeeded;
 *             raised only when ap_il2cpp_resolve() completes (on the game
 *             thread, via ap_resolve_tick), so no thread can see "ready" with
 *             half-initialised state. */
static bool ap_fp_ok;
static bool ap_runtime_ok;
static bool ap_mode_on;            /* player hotkey toggle                */
static bool ap_key_last;           /* toggle key level on the last frame  */

static int ap_state;
static int ap_pending_dest;
static uint64_t ap_pending_at;

static void *ap_last_actor;        /* unit currently being handled        */
static int ap_actor_turn;          /* turn id the actor was seen in       */
static bool ap_actor_done;         /* unit already processed this turn    */
static uint64_t ap_handled_gen;    /* last AMenu generation decided on    */
static bool ap_gen_seen;           /* guard: generation 0 vs 0 initial collision */

/* il2cpp official C API (resolved by name; no offsets to guess). */
static struct {
    void *(*domain_get)(void);
    void *(*domain_get_assemblies)(void *, size_t *);
    void *(*assembly_get_image)(void *);
    const char *(*image_get_name)(void *);
    void *(*class_from_name)(void *, const char *, const char *);
    void *(*class_get_field_from_name)(void *, const char *);
    void (*field_get_value)(void *, void *, void *);
    void (*field_static_get_value)(void *, void *);
    unsigned (*field_get_flags)(void *);
    /* Field self-introspection (diagnostics only; probe skips if missing). */
    void *(*class_get_fields)(void *, void **);
    const char *(*field_get_name)(void *);
    void *(*field_get_type)(void *);
    char *(*type_get_name)(void *);
} ap_il;

/* Named fields / constants resolved once at startup. */
static void *ap_class_stage;    /* StageManager Il2CppClass* (probe use) */
static void *ap_fld_char_map;   /* StageManager.charEntitiesMap : Dictionary<String,CharEntity> —— 实体容器权威来源 */
static void *ap_fld_moveScope;  /* StageManager.moveScope : Dictionary<Int32,Int32>（chunkId→步耗；实测 2026-09-21 probe 确认，不是 List） */
static void *ap_fld_atk_chunks; /* StageManager.atkEntitiyChunkIds : List<Int32>（攻击范围内 chunk；可选） */
static int ap_ai_nearest = 0;   /* EAIFindFlag.AI_NEAREST (runtime probed, fallback 0) */

/* ------------------------------------------------------------------ */
/* Startup: resolve the il2cpp C API and named metadata                 */
/* ------------------------------------------------------------------ */

static void *ap_il_fn(const char *name) {
    void *p = (void *)GetProcAddress(ga, name);
    if (!p) logmsg("Approach: il2cpp export missing: %s", name);
    return p;
}

/* il2cpp_type_get_name 返回的字符串用 il2cpp_free 释放；导出缺失时静默跳过。 */
static void il2cpp_free_name(char *p) {
    static void (*fn)(void *);
    if (!fn) fn = (void (*)(void *))GetProcAddress(ga, "il2cpp_free");
    if (fn && p) fn(p);
}

/* 【v2.0.2 重构：解析只允许发生在游戏线程】
 *
 * 两次实机崩溃（2026-09-20，"Fatal error in GC: Collecting from unknown thread"）
 * 证明：自建 worker 线程在 il2cpp 启动中途碰任何会分配的 IL2CPP API 都会炸——
 *   - v2.0.0 崩在 il2cpp_domain_get_assemblies（每次分配托管数组）；
 *   - v2.0.1 崩在 il2cpp_thread_attach（注册后立刻分配托管 Thread 对象）。
 * 共同点：domain 一出现就动手，而此刻运行时半就绪、主线程的启动分配正在密集
 * 触发 GC 收集；未注册线程发起/参与收集 = 直接 Fatal。
 *
 * 治本：worker 线程从此**零 IL2CPP 调用**，元数据解析挪到游戏线程的输入帧里
 * 懒执行（与 speedkey 的 speed_resolve_tick 同一模式，长期实测安全）。游戏线程
 * 本来就是 il2cpp 已 attach 的运行时线程，跟着主线程的启动节奏走，不存在
 * 「抢在运行时就绪前」的问题。失败按 15 次 × 2 秒有界重试，放弃只影响
 * approach，不影响待机热键。 */
static int ap_attempts;
static uint64_t ap_next_try;

static bool ap_il2cpp_resolve(void) {
    void *(*dget)(void) = (void *(*)(void))ap_il_fn("il2cpp_domain_get");
    void *(*dasm)(void *, size_t *) = (void *(*)(void *, size_t *))ap_il_fn("il2cpp_domain_get_assemblies");
    void *(*aimg)(void *) = (void *(*)(void *))ap_il_fn("il2cpp_assembly_get_image");
    const char *(*iname)(void *) = (const char *(*)(void *))ap_il_fn("il2cpp_image_get_name");
    /* field_get_value reads StageManager.moveScope/atkscope every decision —
     * required. field_static_get_value only feeds the AI_NEAREST probe. */
    void (*fget)(void *, void *, void *) = (void (*)(void *, void *, void *))ap_il_fn("il2cpp_field_get_value");
    if (!dget || !dasm || !aimg || !iname || !fget) return false;

    void *domain = dget();
    if (!domain) return false;   /* il2cpp boots late — retry on a later frame */

    size_t n = 0;
    void **asms = (void **)dasm(domain, &n);
    if (!asms || !n || n > 4096) { logmsg("Approach: assembly list unreadable; disabled"); return false; }
    void *acs = NULL;
    for (size_t i = 0; i < n && !acs; ++i) {
        void *img = aimg(asms[i]);
        if (img && iname(img) && !strcmp(iname(img), "Assembly-CSharp.dll")) acs = img;
    }
    if (!acs) { logmsg("Approach: Assembly-CSharp image not found; disabled"); return false; }

    void *(*cfn)(void *, const char *, const char *) = (void *(*)(void *, const char *, const char *))ap_il_fn("il2cpp_class_from_name");
    void *(*gfn)(void *, const char *) = (void *(*)(void *, const char *))ap_il_fn("il2cpp_class_get_field_from_name");
    if (!cfn || !gfn) return false;

    void *kStage = cfn(acs, "", "StageManager");
    if (!kStage) { logmsg("Approach: StageManager class not found; disabled"); return false; }
    ap_class_stage = kStage;
    /* 2026-09-21 实机 probe 实锤的容器模型（详见 memory）：
     * charEntitiesMap : Dictionary<String, CharEntity> —— 活体实体容器
     * moveScope       : Dictionary<Int32, Int32>       —— 移动范围（chunkId→步耗）
     * atkEntitiyChunkIds : List<Int32>                 —— 当前攻击范围内 chunk（拼写 Entitiy 是游戏源码原样） */
    ap_fld_char_map = gfn(kStage, "charEntitiesMap");
    if (!ap_fld_char_map) {
        logmsg("Approach: StageManager.charEntitiesMap field missing; disabled");
        return false;
    }
    ap_fld_moveScope = gfn(kStage, "moveScope");         /* optional: computed fallback exists */
    ap_fld_atk_chunks = gfn(kStage, "atkEntitiyChunkIds"); /* optional: engaged check skipped if absent */
    logmsg("Approach: fields resolved (charEntitiesMap ok; moveScope=%d atkEntitiyChunkIds=%d)",
           ap_fld_moveScope ? 1 : 0, ap_fld_atk_chunks ? 1 : 0);

    /* EAIFindFlag.AI_NEAREST: probe the static field; enum literals may not
     * carry runtime storage, in which case keep the safe default 0 and say
     * so — GetTarEntityListbyFilt(filtId=0) is AI_NORMAL, a sane default. */
    void *kFlag = cfn(acs, "cfg.def", "EAIFindFlag");
    void *fNear = kFlag ? gfn(kFlag, "AI_NEAREST") : NULL;
    void (*sgv)(void *, void *) = (void (*)(void *, void *))ap_il_fn("il2cpp_field_static_get_value");
    if (fNear && sgv) {
        int v = 0;
        sgv(fNear, &v);
        if (v >= 0 && v <= 15) ap_ai_nearest = v;
        logmsg("Approach: EAIFindFlag.AI_NEAREST=%d (probed)", ap_ai_nearest);
    } else {
        logmsg("Approach: EAIFindFlag.AI_NEAREST not readable; using filtId=0");
    }

    /* Publish everything to the shared vtable BEFORE claiming success —
     * ap_decide() dereferences ap_il.field_get_value on the main thread the
     * moment ap_runtime_ok turns true. (先装配依赖，再宣布就绪.) */
    ap_il.domain_get = dget;
    ap_il.domain_get_assemblies = dasm;
    ap_il.assembly_get_image = aimg;
    ap_il.image_get_name = iname;
    ap_il.class_from_name = cfn;
    ap_il.class_get_field_from_name = gfn;
    ap_il.field_get_value = fget;
    ap_il.field_static_get_value = sgv;
    /* Diagnostics-only exports: optional, the stage probe skips if absent. */
    ap_il.field_get_flags = (unsigned (*)(void *))ap_il_fn("il2cpp_field_get_flags");
    ap_il.class_get_fields = (void *(*)(void *, void **))ap_il_fn("il2cpp_class_get_fields");
    ap_il.field_get_name = (const char *(*)(void *))ap_il_fn("il2cpp_field_get_name");
    ap_il.field_get_type = (void *(*)(void *))ap_il_fn("il2cpp_field_get_type");
    ap_il.type_get_name = (char *(*)(void *))ap_il_fn("il2cpp_type_get_name");

    logmsg("Approach: il2cpp metadata ready (charEntitiesMap resolved)");
    return true;
}

/* 游戏线程每输入帧调一次；有界重试，成功即停，失败 15 次后放弃并记日志。 */
static void ap_resolve_tick(void) {
    if (ap_runtime_ok || ap_fp_ok == false) return;
    if (ap_attempts >= 15) return;
    uint64_t now = GetTickCount64();
    if (ap_attempts && now < ap_next_try) return;
    ap_next_try = now + 2000;
    ap_attempts++;
    if (ap_il2cpp_resolve()) {
        ap_runtime_ok = true;   /* 同一游戏线程上置位；此后 approach_tick 才放行 */
        return;
    }
    if (ap_attempts == 15)
        logmsg("Approach: metadata unresolved after %d tries; disabled for this session",
               ap_attempts);
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                        */
/* ------------------------------------------------------------------ */

/* IL2CPP List<int>: _items array at +0x10, _size at +0x18, data at +0x20. */
static bool ap_list_i32(void *list, int **items, int *count) {
    if (!list) return false;
    void *arr = PTR(list, 0x10);
    int n = VAL(int, list, 0x18);
    if (!arr || n < 0 || n > 65536) return false;
    *items = (int *)((BYTE *)arr + 0x20);
    *count = n;
    return true;
}

static void ap_dump_obj(const char *tag, void *obj) {
    if (!config.verbose_log || !obj) return;
    unsigned char *p = (unsigned char *)obj;
    char b[240];
    int n = snprintf(b, sizeof(b), "Approach dump %s @%p:", tag, obj);
    for (int i = 0; i < 32 && n > 0 && n < (int)sizeof(b) - 4; ++i)
        n += snprintf(b + n, sizeof(b) - (size_t)n, " %02x", p[i]);
    if (n > 0) logmsg("%s", b);
}

/* 【实机诊断】遍历 StageManager 的全部实例字段，打印「字段名 : 类型名」。
 * 只读元数据（il2cpp_field_get_type + il2cpp_type_get_name），不解引用任何
 * 运行时值——零崩溃风险。目的：找出真正的实体容器字段（Dictionary/List），
 * 替代对 AIUtils 返回值布局的猜测。每个 stage 指针只跑一次。 */
static void *ap_probed_stage;

static void ap_probe_stage_fields(void *stage) {
    if (!stage || stage == ap_probed_stage) return;
    if (!ap_il.class_get_fields || !ap_il.field_get_name ||
        !ap_il.field_get_type || !ap_il.type_get_name || !ap_il.field_get_flags)
        return;
    ap_probed_stage = stage;
    void *it = NULL;
    int logged = 0;
    while ((it = ap_il.class_get_fields(ap_class_stage, &it)) && logged < 64) {
        if (ap_il.field_get_flags(it) & 0x10) continue;   /* FIELD_ATTRIBUTE_STATIC */
        void *t = ap_il.field_get_type(it);
        if (!t) continue;
        char *tn = ap_il.type_get_name(t);
        if (tn) {
            logmsg("Approach probe: StageManager.%s : %s", ap_il.field_get_name(it), tn);
            il2cpp_free_name(tn);
            ++logged;
        }
    }
    logmsg("Approach probe: StageManager field dump done (%d instance fields shown)", logged);
}

/* 【2026-09-21 实机 probe 定案】实体身份是字符串 ID（_curEntityId : String），
 * GetTarEntityListbyFilt 返回 Dictionary<String,String>（id→分类），对找敌人没用；
 * 权威容器是 StageManager.charEntitiesMap : Dictionary<String, CharEntity>. */

/* 实体 id 字符串打日志（Il2CppString：klass/monitor/length@0x10/chars@0x14）。 */
static void ap_log_id(const char *tag, void *strObj) {
    if (!config.verbose_log || !strObj) return;
    const char *cls = class_name(PTR(strObj, 0));
    int len = (cls && !strcmp(cls, "String")) ? VAL(int, strObj, 0x10) : -1;
    if (len < 0 || len > 60) {
        logmsg("Approach: %s id=<unreadable @%p>", tag, strObj);
        return;
    }
    char b[80];
    int n = 0;
    const wchar_t *w = (const wchar_t *)((BYTE *)strObj + 0x14);
    for (int i = 0; i < len && n < (int)sizeof(b) - 2; ++i)
        b[n++] = (w[i] >= 32 && w[i] < 127) ? (char)w[i] : '?';
    b[n] = 0;
    logmsg("Approach: %s id=\"%s\"", tag, b);
}

/* Dictionary<Int32,?> 的 key 集合扫描。Entry = {int hashCode; int next; int key;
 * TValue}：4 字节 value 时 stride=0x10（value@0xC），8 字节引用 value 时
 * stride=0x18（key 后有对齐垫，value@0x10）——两种布局 key 都在 +0x08。
 * 已删槽 hashCode=-1 跳过；key 合法性兜底防垃圾。 */
static bool ap_dict_i32_keys(void *dict, int *out, int out_max, int *out_n, int stride) {
    *out_n = 0;
    if (!dict) return false;
    const char *cls = class_name(PTR(dict, 0));
    if (!cls || !strstr(cls, "Dictionary")) return false;
    void *entries = PTR(dict, 0x18);
    int n = VAL(int, dict, 0x20);
    if (!entries || n <= 0 || n > 4096) return false;
    BYTE *ed = (BYTE *)entries + 0x20;      /* Entry[] payload              */
    for (int i = 0; i < n && *out_n < out_max; ++i) {
        BYTE *e = ed + (size_t)i * stride;
        if (*(int *)e == -1) continue;      /* removed/free slot            */
        int key = *(int *)(e + 0x08);
        if (key >= 0 && key < 1000000) out[(*out_n)++] = key;
    }
    return true;
}

/* 扫 charEntitiesMap：第一个「类名 CharEntity 且非玩家阵营」的 value 即敌人。
 * 死亡实体在 diedCharEntitiesMap 单独存放，本容器应只含活体；verbose 打全量
 * 类名清单，语义再偏也能从日志看出来。 */
static void *ap_find_enemy(void *stage) {
    void *map = NULL;
    ap_il.field_get_value(stage, ap_fld_char_map, &map);
    if (!map) {
        if (config.verbose_log) logmsg("Approach: charEntitiesMap value is NULL");
        return NULL;
    }
    void *entries = PTR(map, 0x18);
    int n = VAL(int, map, 0x20);
    if (!entries || n <= 0 || n > 4096) {
        ap_dump_obj("charEntitiesMap", map);
        return NULL;
    }
    if (config.verbose_log) logmsg("Approach: charEntitiesMap count=%d; scanning values", n);
    BYTE *ed = (BYTE *)entries + 0x20;
    void *enemy = NULL;
    int seen = 0;
    for (int i = 0; i < n; ++i) {
        void *e = *(void **)(ed + (size_t)i * 0x18 + 0x10);
        if (!e) continue;
        if (config.verbose_log)
            logmsg("Approach: charMap[%d] raw=%p", i, e);
        /* 指针合法性粗筛：IL2CPP 对象 8 字节对齐且不可能在低地址——
         * 垃圾值直接停扫，避免带崩 class_name。 */
        if (((uintptr_t)e & 7) != 0 || (uintptr_t)e < 0x10000) {
            if (config.verbose_log)
                logmsg("Approach: charMap[%d] value not a plausible object; stop scan", i);
            break;
        }
        const char *ec = class_name(PTR(e, 0));
        if (config.verbose_log && seen < 12)
            logmsg("Approach: charMap[%d] class=%s", i, ec ? ec : "?");
        ++seen;
        /* 阵营判定内联：IsPlayerCamp 反汇编 = (Camp@0x20 == 2)。直接读字段
         * 消除扫描循环里唯一的游戏函数调用（上轮闪退点附近），chunk 同理。 */
        if (config.verbose_log)
            logmsg("Approach: charMap[%d] camp=%d chunk=%d", i, VAL(int, e, 0x20), VAL(int, e, 0x148));
        if (!enemy && ec && !strcmp(ec, "CharEntity") && VAL(int, e, 0x20) != 2)
            enemy = e;
    }
    if (!enemy && config.verbose_log)
        logmsg("Approach: charEntitiesMap holds no hostile CharEntity (seen=%d)", seen);
    return enemy;
}

static void ap_cleanup(void) {
    ap_state = AP_STATE_IDLE;
    ap_pending_dest = 0;
    ap_actor_done = false;
    ap_last_actor = NULL;
    /* Re-enabling mid-menu must re-take the current unit: forget the deduped
     * generation so the next tick decides again. */
    ap_gen_seen = false;
}

/* ------------------------------------------------------------------ */
/* Decision: where should the current unit go?                          */
/* Returns dest chunk id, or -1 for "stay / hand back".                  */
/* ------------------------------------------------------------------ */

static int ap_should_act(void *stage, void *actor) {
    ap_probe_stage_fields(stage);       /* one-shot metadata dump per battle */
    int64_t curEnt = FN(AP_RVA_GET_CUR_ENTITY_ID, int64_t, void *, void *)(stage, NULL);
    if (!curEnt) return -1;
    /* _curEntityId 实为 String（2026-09-21 probe 实锤）；指针透传调用不受影响。 */
    ap_log_id("current entity", (void *)curEnt);

    /* 敌人：直接扫 charEntitiesMap（Dictionary<String,CharEntity>）。 */
    void *enemy = ap_find_enemy(stage);
    if (!enemy) {
        if (config.verbose_log) logmsg("Approach: no enemy found; handing back");
        return -1;
    }
    if (config.verbose_log)
        logmsg("Approach: enemy entity @%p; reading chunk ids…", enemy);
    int enemyChunk = FN(AP_RVA_GET_CHUNK_ID, int, void *, void *)(enemy, NULL);
    int myChunk = FN(AP_RVA_GET_CUR_CHUNK, int, void *, void *)(stage, NULL);
    if (config.verbose_log)
        logmsg("Approach: enemy picked chunk=%d (my chunk=%d)", enemyChunk, myChunk);

    /* Already engaged? atkEntitiyChunkIds : List<Int32> = 当前攻击范围内的
     * chunk。语义待实测确认，verbose 先打出内容，偏了能从日志看出来。 */
    if (ap_fld_atk_chunks) {
        void *atk = NULL;
        ap_il.field_get_value(stage, ap_fld_atk_chunks, &atk);
        int *ai = NULL, an = 0;
        if (ap_list_i32(atk, &ai, &an) && an > 0) {
            if (config.verbose_log) {
                char b[200];
                int m = snprintf(b, sizeof(b), "Approach: atkEntitiyChunkIds n=%d:", an);
                for (int k = 0; k < an && k < 8 && m > 0 && m < (int)sizeof(b) - 8; ++k)
                    m += snprintf(b + m, sizeof(b) - (size_t)m, " %d", ai[k]);
                logmsg("%s", b);
            }
            for (int k = 0; k < an; ++k)
                if (ai[k] == enemyChunk) {
                    if (config.verbose_log)
                        logmsg("Approach: already engaged (enemyChunk=%d in atk list); handing back", enemyChunk);
                    return -1;
                }
        } else if (config.verbose_log) {
            logmsg("Approach: atkEntitiyChunkIds empty/unreadable (engaged check skipped)");
        }
    }

    /* 决策到此为止：Phase 1 只负责"要不要动 + 敌人在哪"。
     * 【2026-09-21 实测定案】AMenu 阶段 moveScope 是空字典、atkEntitiyChunkIds
     * 也是空的——游戏的范围计算发生在点"移动"进入 FsmState_Move 之后，
     * 所以目标格选择必须推迟到 Phase 2（ap_pick_dest）。 */
    return enemyChunk;
}

/* Phase 2 决策：已进入移动状态，此时 moveScope 才有值。
 * 返回 dest（可以等于原地 = 原地待命走个过场），-1 = 完全无法执行。 */
static int ap_pick_dest(void *stage, int targetChunk) {
    int64_t curEnt = FN(AP_RVA_GET_CUR_ENTITY_ID, int64_t, void *, void *)(stage, NULL);
    if (!curEnt) return -1;

    /* Move scope: Dictionary<Int32,Int32> 的 key 集合 = 可达 chunk。 */
    int mbuf[512], mn = 0;
    bool have_ms = false;
    if (ap_fld_moveScope) {
        void *mscope = NULL;
        ap_il.field_get_value(stage, ap_fld_moveScope, &mscope);
        have_ms = ap_dict_i32_keys(mscope, mbuf, 512, &mn, 0x10) && mn > 0;
        if (have_ms && config.verbose_log)
            logmsg("Approach: moveScope dict has %d chunks", mn);
        else if (config.verbose_log) {
            /* 指针合法性守卫：野指针不许喂给 class_name。 */
            const char *mc = (mscope && (uintptr_t)mscope > 0x10000 && !((uintptr_t)mscope & 7))
                                 ? class_name(PTR(mscope, 0)) : NULL;
            logmsg("Approach: moveScope field obj=%p class=%s -> no usable keys",
                   mscope, mc ? mc : "null/garbage");
            if (mc) ap_dump_obj("moveScope", mscope);
        }
    }
    if (!have_ms) {
        int myChunk = FN(AP_RVA_GET_CUR_CHUNK, int, void *, void *)(stage, NULL);
        void *self = myChunk >= 0 ? FN(AP_RVA_ENT_BY_CHUNK, void *, void *, int, int, void *)(stage, myChunk, 0, NULL) : NULL;
        bool fly = self ? FN(AP_RVA_GET_CAN_FLY, bool, void *, void *)(self, NULL) : false;
        void *ms2 = FN(AP_RVA_MOVE_RANGE_CHUNKS, void *, int64_t, int, bool, int, void *)
                        (curEnt, config.ap_path_range, fly, 2, NULL); /* camp 2 = player side */
        int *mi = NULL;
        have_ms = ap_list_i32(ms2, &mi, &mn) && mn > 0;
        if (config.verbose_log) {
            /* 返回值可能是 List，也可能是裸 int/空——先看类名再决定能否解引用。 */
            const char *mc = (ms2 && (uintptr_t)ms2 > 0x10000 && !((uintptr_t)ms2 & 7))
                                 ? class_name(PTR(ms2, 0)) : NULL;
            logmsg("Approach: GetMoveRangeChunkId ret=%llx class=%s -> %d chunks",
                   (unsigned long long)(uintptr_t)ms2, mc ? mc : "raw/garbage",
                   have_ms ? mn : 0);
            if (mc && !have_ms) ap_dump_obj("MoveRange", ms2);
        }
        if (!have_ms) {
            /* 同名变体 2：参数一致，内部实现不同，作为备选。 */
            ms2 = FN(AP_RVA_MOVE_RANGE_CHUNKS2, void *, int64_t, int, bool, int, void *)
                      (curEnt, config.ap_path_range, fly, 2, NULL);
            have_ms = ap_list_i32(ms2, &mi, &mn) && mn > 0;
            if (config.verbose_log)
                logmsg("Approach: GetMoveRangeChunkId2 -> %d chunks", have_ms ? mn : 0);
        }
        if (have_ms && mn > 512) mn = 512;
        if (have_ms)
            for (int k = 0; k < mn; ++k) mbuf[k] = mi[k];
    }
    /* moveScope 拿不到时不再直接放弃：GetMoveToPath 的 moveRange 参数本身就
     * 会把路径限制在可走范围内，此时跳过范围过滤、用路径终点做目标格。 */
    bool filter_scope = have_ms;
    if (config.verbose_log)
        logmsg("Approach: move scope %s", filter_scope ? "acquired" : "UNAVAILABLE; using raw path (range-clamped)");

    /* Path toward the target enemy chunk.
     * 【2026-09-21 实测复盘定案】Player.log 实锤："計算出來的路徑爲空,
     * fromChunkId:45, toChunkId:230, moveRange:8"——EntityMoveTo 内部用
     * CharEntity$$get_Move(curEntity) 作为 moveRange 重算路径，我们用 range=99
     * 选出的中途格（路径 idx 14/25）超出 8 步移动力，CalcMovePath 返回空、
     * 移动被取消。因此 path range 必须用单位真实移动力，选出的 dest 天然
     * 在可达范围内；moveScope 过滤降级为辅助校验。 */
    int path_range = 0;
    void *curChar = FN(AP_RVA_GET_CUR_CHAR_ENT, void *, void *, void *)(stage, NULL);
    if (curChar)
        path_range = FN(AP_RVA_GET_MOVE, int, void *, void *)(curChar, NULL);
    if (path_range < 1 || path_range > AP_PATH_RANGE_FALLBACK) {
        if (config.verbose_log)
            logmsg("Approach: get_Move unreadable (%d); fallback ap_path_range=%d",
                   path_range, config.ap_path_range);
        path_range = config.ap_path_range;
    } else if (config.verbose_log)
        logmsg("Approach: unit move range=%d", path_range);
    if (config.verbose_log)
        logmsg("Approach: calling GetMoveToPath(target=%d, range=%d)…", targetChunk, path_range);
    void *path = FN(AP_RVA_GET_MOVE_TO_PATH, void *, void *, int64_t, int, int, void *)
                     (stage, curEnt, targetChunk, path_range, NULL);
    int *pi = NULL, pn = 0;
    if (!ap_list_i32(path, &pi, &pn) || !pn) {
        if (config.verbose_log) {
            const char *pc = (path && (uintptr_t)path > 0x10000 && !((uintptr_t)path & 7))
                                 ? class_name(PTR(path, 0)) : NULL;
            logmsg("Approach: no path toward targetChunk=%d (ret=%llx class=%s)",
                   targetChunk, (unsigned long long)(uintptr_t)path, pc ? pc : "raw/garbage");
        }
        return -1;
    }
    if (config.verbose_log) {
        logmsg("Approach: path has %d chunks toward targetChunk=%d", pn, targetChunk);
        ap_dump_obj("path", path);
    }

    /* Pick the last path chunk that is inside the move scope (when the scope
     * is known), not the enemy's own chunk and not occupied by anyone.
     * Walking the path backwards yields the closest reachable point. */
    for (int i = pn - 1; i >= 0; --i) {
        int c = pi[i];
        if (c == targetChunk) continue;
        if (filter_scope) {
            bool in_scope = false;
            for (int k = 0; k < mn; ++k)
                if (mbuf[k] == c) { in_scope = true; break; }
            if (!in_scope) continue;
        }
        if (config.verbose_log)
            logmsg("Approach: checking occupant of chunk=%d…", c);
        void *occupant = FN(AP_RVA_ENT_BY_CHUNK, void *, void *, int, int, void *)(stage, c, 0, NULL);
        if (occupant) continue;                    /* someone stands there */
        if (config.verbose_log)
            logmsg("Approach: dest=%d (path idx %d/%d, targetChunk=%d)", c, i, pn, targetChunk);
        return c;
    }
    if (config.verbose_log) logmsg("Approach: no usable path chunk; will stand still");
    return -1;
}

/* ------------------------------------------------------------------ */
/* Per-frame ticks                                                      */
/* ------------------------------------------------------------------ */

/* Phase 2: the move button was clicked; once the FSM shows FsmState_Move,
 * drive handleMoveAsync exactly like a player's chunk click would. */
static void ap_pending_tick(void) {
    if (ap_state != AP_STATE_PENDING_CLICK) return;
    if (GetTickCount64() - ap_pending_at > AP_PENDING_TIMEOUT_MS) {
        logmsg("Approach: pending move-click timed out; standing down");
        ap_state = AP_STATE_IDLE;
        ap_actor_done = true;
        return;
    }
    void *fields = menu_game_fields();
    void *fsm_data = fields ? PTR(fields, 0x90) : NULL;
    void *fsm = fsm_data ? PTR(fsm_data, 0x10) : NULL;
    void *state = fsm ? PTR(fsm, 0x20) : NULL;
    if (!menu_class_is(state, "FsmState_Move")) return;   /* keep waiting */

    void *stage = PTR(fields, 0xF0);
    int origin = stage ? FN(AP_RVA_GET_CUR_CHUNK, int, void *, void *)(stage, NULL) : -1;
    /* 到了移动状态才选目标格：此时游戏才把 moveScope 算好。 */
    int dest = stage ? ap_pick_dest(stage, ap_pending_dest) : -1;
    if (dest < 0 || !fsm_data) {
        /* 【2026-09-21 修订】选不出目标格时直接放弃本次移动、交还玩家
         * （FSM 停留在正常的移动选择态，等价于玩家还没点）。不再伪造
         * 「原地 CurEntityMoveTo(0)」——CalcMovePath(X,X) 未经验证，
         * 与 230 空路径同源，只会再吃一次 LogError。 */
        if (config.verbose_log)
            logmsg("Approach: no dest computable (dest=%d fsm=%d); handing back to player",
                   dest, fsm_data ? 1 : 0);
        ap_state = AP_STATE_IDLE;
        ap_actor_done = true;
        return;
    }
    /* 【2026-09-21 反汇编+Player.log 双重实锤】官方点击移动（handleMoveAsync/
     * handleLeftClickMoveAtkAsync）的完整序列：
     *   1) Fsm_StageData$$SetEntityMoveToChunkId(fsm, dest)
     *   2) Fsm_StageData$$PushMoveState(fsm, curChunk, dest)   ← 把移动状态压进
     *      FSM 状态机栈；缺了它没人接收/推进这次移动（MoveAtk 变体还会置
     *      [fsm+0x64]=1 即 IsMoveAtk，普通移动不需要）
     *   3) StageManager$$CurEntityMoveTo(dest, waitWalk=1, follow=0)
     *      —— 16B sret 返回；rcx=&返回缓冲, rdx=this(stage), r8=toChunkId,
     *      r9=waitWalk, 栈上 follow(bool) + MethodInfo*。与全部 13 个原生
     *      调用点的传参完全一致。
     * fsm 实例 = fields+0x90（与官方 [static+0xB8]→[+0x90] 同源，probe 实锤）。 */
    if (fsm_data) {
        if (config.verbose_log)
            logmsg("Approach: FSM prelude SetEntityMoveToChunkId(%d) + PushMoveState(%d,%d)",
                   dest, origin, dest);
        FN(AP_RVA_FSM_SET_MOVE_TO, void, void *, int, void *)(fsm_data, dest, NULL);
        FN(AP_RVA_FSM_PUSH_MOVE, void, void *, int, int, void *)(fsm_data, origin, dest, NULL);
    } else if (config.verbose_log)
        logmsg("Approach: fsm_data unavailable; skipping FSM prelude");
    if (config.verbose_log)
        logmsg("Approach: calling CurEntityMoveTo dest=%d waitWalk=1 follow=0", dest);
    struct { float v[4]; } ret16;
    FN(AP_RVA_CUR_MOVE_TO, void, void *, void *, int, uint8_t, uint8_t, void *)
        (&ret16, stage, dest, 1, 0, NULL);
    if (config.verbose_log)
        logmsg("Approach: CurEntityMoveTo returned (%.1f, %.1f, %.1f)",
               ret16.v[0], ret16.v[1], ret16.v[2]);
    ap_state = AP_STATE_IDLE;
    ap_actor_done = true;   /* one auto-move per unit per turn */
}

/* Phase 1: an actionable AMenu — decide and click the move button. */
static void ap_menu_tick(void *input) {
    MenuContext ctx;
    if (!menu_safe_context(input, &ctx)) {
        /* Silent failure here blinded the first live test: the mode was ON
         * but nothing in the log said WHY no decision ever happened. */
        if (config.verbose_log) {
            static uint64_t last_ctx_fail;
            uint64_t now = GetTickCount64();
            if (now - last_ctx_fail > 3000) {
                last_ctx_fail = now;
                logmsg("Approach: menu context unavailable; waiting for an actionable menu");
            }
        }
        return;
    }
    /* First usable context must always decide: without the seen flag, a
     * generation starting at 0 equals the zeroed ap_handled_gen and every
     * frame dedupes itself into a permanent no-op. */
    if (ap_gen_seen && ctx.generation == ap_handled_gen) return;
    ap_gen_seen = true;
    ap_handled_gen = ctx.generation;
    if (config.verbose_log)
        logmsg("Approach: menu context ok; generation=%llu actor=%p",
               (unsigned long long)ctx.generation, ctx.actor);

    int turnId = FN(AP_RVA_GET_TURN_ID, int, void *, void *)(ctx.stage, NULL);
    if (ctx.actor != ap_last_actor || turnId != ap_actor_turn) {
        ap_last_actor = ctx.actor;
        ap_actor_turn = turnId;
        ap_actor_done = false;
        if (config.verbose_log)
            logmsg("Approach: new acting unit %p turn=%d", ctx.actor, turnId);
    }
    if (ap_actor_done || ap_state != AP_STATE_IDLE) return;

    /* Phase 1 决策：只判断"要不要动 + 敌人在哪"，目标格选择推迟到
     * 进入移动状态之后（此时 moveScope 才被游戏计算）。 */
    int target = ap_should_act(ctx.stage, ctx.actor);
    ap_actor_done = true;                       /* decided: act or stand */
    if (target < 0) return;                     /* stay — hand back */

    void *btn = menu_eligible_button(ctx.menu, 0);  /* 0 = "move" button */
    if (!btn) {
        if (config.verbose_log) logmsg("Approach: move button not clickable right now");
        return;
    }
    ap_pending_dest = target;                   /* 复用为"目标敌人 chunk" */
    ap_pending_at = GetTickCount64();
    ap_state = AP_STATE_PENDING_CLICK;
    FN(0xA86FB0, void, void *, void *)(btn, NULL);  /* native click pipeline */
    VAL(bool, input, 0x17D) = true;                 /* mark input consumed  */
    logmsg("Approach: move button clicked, target enemy chunk=%d (actor turn=%d)", target, ap_actor_turn);
}

/* Hotkey entry, called every frame from the input tick hook.
 * active  = approach entry switch is on in the game's mod manager
 * key_bit = toggle key held this frame (edge-detected here)            */
static void approach_tick(void *input, bool focused, bool active, bool key_bit) {
    /* Toggle only reacts while the entry itself is on; the key level is
     * tracked either way so a press made while disabled can't replay when
     * re-enabled. */
    bool toggle_now = focused && active && key_bit && !ap_key_last;
    ap_key_last = key_bit;
    if (toggle_now) {
        if (!ap_runtime_ok) {
            logmsg("Approach: hotkey ignored (approach runtime disabled by "
                   "fingerprint/metadata checks); wait hotkey unaffected");
            return;
        }
        ap_mode_on = !ap_mode_on;
        logmsg("Approach mode turned %s by hotkey", ap_mode_on ? "ON" : "OFF");
        if (!ap_mode_on) ap_cleanup();
        return;                                   /* no decisions on the toggle frame */
    }
    if (!ap_runtime_ok || !active || !ap_mode_on || !focused) return;

    ap_pending_tick();
    ap_menu_tick(input);
}

#endif
