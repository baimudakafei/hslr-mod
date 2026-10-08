/* Numeric Hotkeys: native character action menu, HSLR Steam build 25293526.
 * Include after host defines ga, PTR, VAL, FN, ready_class_slot, class_name, logmsg.
 * Unity main thread only. This file neither reads keys nor installs hooks.
 * Hook 0x93b290 -> hk_menu_show (original stored in orig_menu_show).
 * Call menu_tick AFTER original NewInputManager.Update (0x65A4E0).
 * held bits 0..6 represent top-row 1,2,3,4,5,6,0. Host filters modifiers.
 * blocked includes host text-entry/console/overlay gates and pending quick load.
 *
 * Local change vs. the audited original: the unused helper menu_context_equal()
 * is dropped, so the file compiles warning-free here. Nothing else was touched;
 * the untouched original is kept at reference/upstream/source/menu.h.
 */
typedef struct {
    void *stage, *actor, *menu;
    int action_epoch, stage_epoch;
    uint64_t generation;
} MenuContext;
static uint64_t menu_generation, menu_dispatched_generation;
static void *menu_last_shown; /* comparison only; never dereferenced */
static void (*orig_menu_show)(void *, void *);
static void hk_menu_show(void *self, void *method) {
    orig_menu_show(self, method);
    menu_last_shown = self;
    if (++menu_generation == 0) ++menu_generation;
}
static void menu_reset(void) {
    /* The current presentation can become actionable when enabling this mod. */
    menu_dispatched_generation = 0;
}
static bool menu_class_is(void *object, const char *wanted) {
    if (!object) return false;
    const char *name = class_name(PTR(object,0));
    return name && !strcmp(name,wanted);
}
/* 【2.2.0 排障】运行时字段转储：列出字段名/偏移/是否 static，静态字段再读出值。
 * 全部走 IL2CPP C API（GameAssembly 内部编译代码），不受 Il2CppClass 布局漂移影响。 */
static void wk_dump_fields(void *klass, const char *tag) {
    if (!klass || !ifn_class_get_fields || !ifn_field_get_name ||
        !ifn_field_get_type || !ifn_field_get_offset || !ifn_field_static_get_value)
        return;
    void *iter = NULL, *f;
    char row[1100];
    int n = 0, count = 0;
    while ((f = ifn_class_get_fields(klass, &iter)) != NULL && count < 40) {
        count++;
        const char *fn = ifn_field_get_name(f);
        void *ft = ifn_field_get_type(f);
        int off = ifn_field_get_offset(f);
        /* Il2CppType 内存布局：data@0（8B），attrs 位域在 +8 的低 16 位；0x10=STATIC */
        unsigned attrs = ft ? (*(unsigned int *)((char *)ft + 8)) : 0;
        int is_static = (attrs & 0x10) ? 1 : 0;
        n += snprintf(row + n, sizeof(row) - n, "%s%s@%d%s", n ? " | " : "",
                      fn ? fn : "?", off, is_static ? "[S]" : "");
        if (is_static) {
            unsigned long long v = 0;
            ifn_field_static_get_value(f, &v);
            n += snprintf(row + n, sizeof(row) - n, "=%llx", v);
        }
        if (n > (int)sizeof(row) - 200) break;
    }
    logmsg("%s fields(%d): %s", tag, count, count ? row : "(none)");
}
/* 【2.2.0 排障】类对象原始转储 0xB0..0xF8：定位 static_fields 指针搬去了哪 */
static void wk_dump_klass(void *klass, const char *tag) {
    if (!klass) return;
    char row[300];
    int n = 0;
    for (int i = 0xB0; i < 0x100; i += 8)
        n += snprintf(row + n, sizeof(row) - n, "+%x=%llx ", i,
                      (unsigned long long)(uintptr_t)PTR(klass, i));
    logmsg("%s klass 0xB0-0xF8: %s", tag, row);
}
static void *menu_game_fields(void) {
    void *klass=class_by_name("", "Game");
    if (!klass) return NULL;
    void *fields=PTR(klass,0xB8);
    /* 同 LogicMgr：按名解析不触发 ClassInit，静态区未分配时强制初始化一次 */
    if (!fields && class_init) {
        static int init_tried;
        if (!init_tried) {
            init_tried = 1;
            class_init(klass);
            fields = PTR(klass, 0xB8);
            logmsg("Game class-init forced, static area now %s",
                   fields ? "available" : "STILL NULL");
            wk_dump_fields(klass, "Game");
            wk_dump_klass(klass, "Game");
        }
    }
    return fields;
}
/* 【2.2.0 适配】LogicMgr 的静态单例已被游戏移除（6 字段全实例、无任何 static，
 * 静态区不再分配）。改为挂钩 LogicMgr$$Update（0x668DD0，每帧被 Unity 调用、
 * this 即实例）捕获实例。Hook 装不上时门绕过 LogicMgr 检查（非致命），
 * 由下游 FSM 门兜底。 */
static int wk_logicmgr_hook_ok;
static void *(*orig_logicmgr_update)(void *, void *);
static void *wk_logicmgr_self;
static void hk_logicmgr_update(void *self, void *method) {
    wk_logicmgr_self = self;
    orig_logicmgr_update(self, method);
}
static bool menu_logic_ready(void) {
    if (!wk_logicmgr_hook_ok) return true; /* 退化路径：仅靠 IsBlocking + FSM 门 */
    if (!wk_logicmgr_self) {
        static int logged;
        if (!logged) {
            logged = 1;
            logmsg("LogicMgr instance not captured yet (waiting for Update)");
        }
        return false;
    }
    /* LogicState: 0=None 1=MainMenu 2=Scenario 3=BigMap 4=Loading 5=Empty 6=StorageSetup */
    if ((int)FN(0x601240,int,void*,void*)(wk_logicmgr_self,NULL) != 2 ||
        FN(0x6691E0,bool,void*,void*)(wk_logicmgr_self,NULL)) {
        static char last_sig[96];
        char sig[96];
        int st = (int)FN(0x601240,int,void*,void*)(wk_logicmgr_self,NULL);
        snprintf(sig, sizeof(sig), "state=%d changing=%d", st,
                 (int)FN(0x6691E0,bool,void*,void*)(wk_logicmgr_self,NULL));
        if (strcmp(sig, last_sig)) {
            snprintf(last_sig, sizeof(last_sig), "%s", sig);
            logmsg("menu_logic_ready stage-A: %s", sig);
        }
        return false;
    }
    void *logic=FN(0x669160,void*,void*,void*)(wk_logicmgr_self,NULL);
    if (!logic || !VAL(bool,logic,0x10) || !VAL(bool,logic,0x20)) {
        static char last_sig[96];
        char sig[96];
        snprintf(sig, sizeof(sig), "logic=%p l10=%d l20=%d", logic,
                 logic ? (int)VAL(bool,logic,0x10) : -1,
                 logic ? (int)VAL(bool,logic,0x20) : -1);
        if (strcmp(sig, last_sig)) {
            snprintf(last_sig, sizeof(last_sig), "%s", sig);
            logmsg("menu_logic_ready stage-B: %s", sig);
        }
        return false;
    }
    return true;
}
static bool menu_top_submit(void *input,void *menu) {
    /* Native TriggerEvent continues after a handler returns false. The
       always-registered gamepad cursor precedes all UI, including AMenu.
       Recognize only that exact handler and its side-effect-free false gates;
       never invoke a handler merely to discover whether it would consume. */
    void *delegates=PTR(input,0x60);
    if (!delegates) return false;
    void *list=FN(0x656D90,void*,void*,void*,void*)(input,delegates,NULL);
    if (!list || VAL(int,list,0x18)<=0 || VAL(int,list,0x18)>128) return false;
    void *items=PTR(list,0x10);
    if (!items) return false;
    for (int i=0;i<VAL(int,list,0x18);i++) {
        void *handler=PTR(items,0x20+16*i); /* SubscriberInfoSimple stride 16 */
        void *target=handler?PTR(handler,0x20):NULL;
        if (target==menu) return true;
        if (menu_class_is(target,"UIMouseCursor") &&
            PTR(handler,0x10)==(BYTE*)ga+rva_of(0xAB9C50) &&
            !(VAL(bool,input,0x144) && VAL(bool,target,0x60) && VAL(int,target,0x64)>0)) continue;
        return false; /* unknown higher handler: fail closed */
    }
    return false;
}
/* Remote-diagnosis aid: menu_safe_context records WHERE it gave up, and
 * menu_tick logs it (verbose_log only, throttled to step changes) so a user's
 * log can answer "I pressed the key and nothing happened". */
static int menu_gate_step, menu_gate_logged, menu_btn_logged;
static const char *const menu_gate_names[] = {
    "passed",
    "input frame already consumed",
    "game logic not ready (loading/paused?)",
    "timeScale too small",
    "game fields unavailable",
    "stage/fsm data unavailable",
    "fsm transition in progress",
    "stage is not an actionable AMenu stage",
    "active FSM state is not FsmState_AMenu",
    "fsm sub-state busy",
    "current actor unavailable",
    "action menu panel not open/usable",
    "menu is not the top input subscriber",
};
static bool menu_safe_context(void *input,MenuContext *out) {
    if (out) memset(out,0,sizeof(*out));
    menu_gate_step=1;
    if (!input || PTR(input,0x28) || VAL(bool,input,0x17D)) return false;
    menu_gate_step=2;
    bool blocking = FN(0x6D08A0,bool,void*)(NULL);
    if (blocking) {
        /* 【2.2.0 排障】IsBlocking 恒真的可能性单独打点 */
        static bool logged_blocking;
        if (!logged_blocking) {
            logged_blocking = true;
            logmsg("menu gate: ModUIInputBlocker.get_IsBlocking=true (blocking the gate)");
        }
        return false;
    }
    if (!menu_logic_ready()) return false;
    menu_gate_step=3;
    if (FN(0x3FFA750,float,void*)(NULL)<=0.0001f) return false;
    menu_gate_step=4;
    void *fields=menu_game_fields();
    if (!fields) return false;
    menu_gate_step=5;
    void *stage=PTR(fields,0xF0), *fsm_data=PTR(fields,0x90), *fsm_mgr=PTR(fields,0x30);
    /* StageManager.fsmData is an unused legacy field, normally NULL even
       while the native menu is actionable. Native menu handlers use Game's
       current FsmStageData; validate that active FSM below. */
    if (!stage || !fsm_data || !fsm_mgr) return false;
    menu_gate_step=6;
    if (FN(0x7A1A30,bool,void*,void*)(fsm_mgr,NULL)) return false;
    menu_gate_step=7;
    if (VAL(int,stage,0x40)!=12 || !VAL(bool,stage,0x1C0) ||
        VAL(bool,stage,0x23D) || VAL(bool,stage,0x23E) || VAL(bool,stage,0x2A1) ||
        VAL(int,stage,0xD4)!=0 || VAL(int,fsm_data,0x40)!=-1) return false;
    menu_gate_step=8;
    void *fsm=PTR(fsm_data,0x10), *state=fsm?PTR(fsm,0x20):NULL;
    if (!fsm || VAL(bool,fsm,0x28) || !menu_class_is(state,"FsmState_AMenu")) return false;
    menu_gate_step=9;
    if (FN(0x9BBE90,bool,void*,void*)(fsm_data,NULL)) return false;
    menu_gate_step=10;
    void *actor=FN(0x7FE0D0,void*,void*,void*)(stage,NULL);
    if (!actor) return false;
    menu_gate_step=11;
    void *ui=PTR(fields,0x68);
    void *menu=ui?FN(0xE55F90,void*,void*,int,void*)(ui,202,NULL):NULL;
    if (!menu_class_is(menu,"NewUIScenarioAMenu") || VAL(bool,menu,0x58) ||
        VAL(bool,menu,0x90) || !FN(0x3FE5340,bool,void*,void*)(menu,NULL)) return false;
    menu_gate_step=12;
    void *main=PTR(menu,0xA8);
    if (!main || !FN(0x3FED0F0,bool,void*,void*)(main,NULL) || !menu_top_submit(input,menu)) return false;
    menu_gate_step=0;
    /* Handles activation of the mod while the current menu was already shown.
       All ordinary menu reopenings are counted by hk_menu_show. */
    if (menu_last_shown!=menu || !menu_generation) {
        menu_last_shown=menu;
        if (++menu_generation==0) ++menu_generation;
    }
    if (out) {
        out->stage=stage; out->actor=actor; out->menu=menu;
        out->action_epoch=VAL(int,stage,0xA8); out->stage_epoch=VAL(int,stage,0xAC);
        out->generation=menu_generation;
    }
    return true;
}
/* 【流程 7】按钮资格检查：按 offsets 表从菜单对象里取目标按钮
 * （顺序 = 移动/攻击/特殊技/魔法/道具/状态/待机），要求按钮激活、冷却已到
 * （now-lastClick >= cooldown）、且真实挂在菜单的可点列表里。
 * Java 类比：调用 Service 前的参数+状态预检，任一不满足就返回 null。 */
static void *menu_eligible_button(void *menu,unsigned index) {
    static const unsigned offsets[7]={0xB0,0xB8,0xC8,0xC0,0xE0,0xD8,0xE8};
    if (index>=7) return NULL;
    void *button=PTR(menu,offsets[index]);
    if (!button || !FN(0x3FE5340,bool,void*,void*)(button,NULL) ||
        !FN(0x43700C0,bool,void*,void*)(button,NULL)) return NULL;
    void *go=FN(0x3FE65D0,void*,void*,void*)(button,NULL);
    if (!go || !FN(0x3FED0F0,bool,void*,void*)(go,NULL)) return NULL;
    void *list=PTR(menu,0x68), *items=list?PTR(list,0x10):NULL;
    int count=list?VAL(int,list,0x18):0;
    if (!items || count<0 || count>16) return NULL;
    for (int n=0;n<count;n++) {
        void *entry=PTR(items,0x20+8*n), *ex=entry?PTR(entry,0x18):NULL;
        if (!ex || PTR(ex,0x68)!=button || PTR(entry,0x10)!=go) continue;
        if (!FN(0x3FE5340,bool,void*,void*)(ex,NULL)) return NULL;
        float now=FN(0x3FFA810,float,void*)(NULL);
        if (!(now-VAL(float,ex,0x70)>=VAL(float,ex,0x20))) return NULL;
        return ex;
    }
    return NULL;
}
static bool menu_tick(void *input,unsigned held7,bool blocked) {
    held7 &= 0x7F;
    if (blocked || !held7) return false;
    MenuContext context;
    if (!menu_safe_context(input,&context)) {
        /* Only reachable while a configured key is held; log at most once per
         * distinct failure point so a held key cannot flood the log. */
        if (config.verbose_log && menu_gate_step != menu_gate_logged) {
            menu_gate_logged = menu_gate_step;
            logmsg("Menu gate rejected: %s", menu_gate_names[menu_gate_step]);
        }
        return false;
    }
    if (menu_dispatched_generation==context.generation) return false;
    /* Deterministic priority: 1,2,3,4,5,6,0. Do NOT fall through to another
       held key when the first requested action is unavailable. */
    unsigned index=0;
    while (!(held7&(1u<<index))) ++index;
    void *ex=menu_eligible_button(context.menu,index);
    if (!ex) {
        if (config.verbose_log && menu_gate_step != menu_btn_logged) {
            menu_btn_logged = menu_gate_step;
            logmsg("Button %u not eligible (cooldown or not clickable right now)",
                   index==6?0:index+1);
        }
        return false;
    }
    /* 【流程 8】先记账再派发（先写幂等表，再处理请求）——原生事件可能重入本模块 */
    menu_dispatched_generation=context.generation;
    /* 【流程 9】原生点击：走游戏自己的按钮点击入口，与玩家手点完全同一条路径 */
    FN(0xA86FB0,void,void*,void*)(ex,NULL);
    VAL(bool,input,0x17D)=true; /* 标记本帧输入已被菜单消费 */
    logmsg("Menu shortcut %u dispatched; generation=%llu actionEpoch=%d",index==6?0:index+1,
        (unsigned long long)context.generation,context.action_epoch);
    return true;
}
