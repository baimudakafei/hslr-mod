/* Speed Hotkey for The Legend of Fancy Realm Remake (HSLR 2.2.4 / build 25767968).
 *
 * Adds +/- keys (main row and numpad) that step the game's base speed
 * through a configurable ladder (default 1 / 2 / 5 / 10) while a fight
 * (ScenarioLogic) or the big map (BigMapLogic) is active. Implemented the
 * same way as the audited wait-hotkey mod: an independent d3d11.dll proxy
 * that hooks the Unity main-thread input tick.
 *
 * What this mod does, in full:
 *   - reads the physical state of the keys named in hslr-speed-hotkey.ini
 *   - writes a log next to itself and, once, a default .ini if none exists
 *   - calls the game's own StageManager.SetBaseGameSpeed(float) (the same
 *     entry its debug panel uses), then keeps a per-frame guard that
 *     re-asserts the chosen step after scene/instance changes and yields
 *     whenever the game's own GameSpeedCtrl reports dialogue/event activity
 *     (optional extra safety: hold 1x while dialogue is up)
 *   - the dialogue-skip systems (long-press Space, m_HotKey_DialogueTalkSkip,
 *     auto-skip-read) are never touched: stepping the base speed is a
 *     different subsystem from skipping
 * It installs no network code, starts no external process, reads no save file,
 * patches no game file on disk, and never touches the game's own save/load
 * shortcuts.
 *
 * Address strategy (per the research report §6 decision):
 *   - the hooks, key reads, frame counter, mod-manager switch and the
 *     CurLogic range gate keep their audited RVAs + byte fingerprints
 *     (fail-closed, verified by tools/verify_fingerprints.py)
 *   - everything NEW to this mod (StageManager, SetBaseGameSpeed,
 *     baseGameSpeed offset, the LogicMgr instance, GameSpeedCtrl signals) is
 *     resolved at runtime through the IL2CPP C API BY NAME, so no new
 *     hardcoded RVA enters this build
 *   - v1.0.1: LogicMgr has no static singleton in the metadata (verified with
 *     tools/analyze/logicmgr_holders.py), so its instance is acquired from
 *     the scene via UnityEngine.Object.FindObjectOfType, cached per frame;
 *     GameSpeedCtrl is a static class and its signals are invoked as such
 *   - v1.1.0: three live-machine fixes, all driven by the user's log:
 *     1) il2cpp_runtime_invoke expects params[i] to point AT the argument
 *        slot ("array of pointers to the argument values"), so a reference
 *        argument must be passed as &pointer, not the pointer itself.
 *        The old code passed the Type object directly, FindObjectOfType
 *        dereferenced it as a bogus argument and threw every call — the
 *        speed chain never became usable on any machine.
 *     2) thrown exceptions now log their class name; the FindObjectOfType
 *        retry loop is capped (20 fast tries, then 1/60s) instead of
 *        spamming one line every 5 s forever.
 *     3) speed ladder is hard-clamped to [1x, 10x] (user requirement), and
 *        the guard restores the step whenever the game itself leaves the
 *        base speed below 1x.
 *     Plus the requested OSD: after a successful speed write the mod calls
 *     the game's own StageManager.SyncGameSpeed() (and prefers the full
 *     SetGameSpeed(float) entry), and optionally GodsetToast.Show("速度 x5")
 *     so the current step is visible like the game's own Ctrl speedup UI.
 *   - v1.1.1: the first live FindObjectOfType crashed the game inside
 *     il2cpp_runtime_class_init while the game's startup coroutine was still
 *     running (user's Player.log stack, 2026-09-20). In v1.0.1 the probe
 *     always threw before the engine call executed, so that path had never
 *     run on any machine. Fix: the LogicMgr probe is now ARMED only after a
 *     live StageManager has been seen (runtime mature, real scene loaded),
 *     the first probe waits another 5 s for the scene to settle, and a
 *     "no LogicMgr in the scene yet" result is logged instead of staying
 *     silent (v1.1.0's silent NULL made the crash look like a freeze).
 *   - v1.1.2: switching the Mod ON still crashed at boot (2026-09-20, third
 *     crash). Stack: our d3d11 frame -> il2cpp_runtime_class_init x2, inside
 *     the startup coroutine; the mod log ended right after "Mod enabled"
 *     with the speed chain already ready. Root cause: guard_tick's first
 *     line proactively CALLED the get_StageMgr static method pointer every
 *     frame; during the boot coroutine that call triggers the declaring
 *     class's static initialization (il2cpp_runtime_class_init) and crashes
 *     the game. With the Mod switch OFF the enabled check returns before
 *     that line, which is why the same session never crashed. Two deeper
 *     problems are fixed alongside:
 *     1) the arming signal is now PASSIVE: MinHook hooks the compiled code
 *        of get_StageMgr (and of Singleton`1<LogicMgr>.get_Instance) so the
 *        instances are captured from the game's OWN calls. guard_tick makes
 *        ZERO proactive IL2CPP object-model calls; a cautious direct probe
 *        only runs 120 s after boot if the game never touched the getter.
 *     2) direct method calls (SetBaseGameSpeed/SetGameSpeed/SyncGameSpeed/
 *        dialogue signals) now go through MethodInfo->methodPointer (the
 *        first field), not through the MethodInfo* itself cast to code —
 *        that old convention had never executed far enough to be verified.
 *     3) the base-class diagnostic (LogicMgr -> Singleton`1 -> Object,
 *        i.e. System.Object) means FindObjectOfType can only be a fallback;
 *        the hooked Singleton getter is the primary LogicMgr source.
 *   - v1.1.3: the third live-machine issue — no speed readout after +/-.
 *     The v1.1.2 log shows every keypress reached "SetBaseGameSpeed(N) sent
 *     but baseGameSpeed reads 0": the field verify-read never matched, so
 *     SyncGameSpeed() and GodsetToast never ran. Offline probe
 *     (tools/analyze/speed_osd_probe.py -> speedkey/build/meta/speed-osd.txt)
 *     proved the conventions RIGHT (SetBaseGameSpeed/SetGameSpeed/
 *     SyncGameSpeed are instance methods, baseGameSpeed an instance field)
 *     and that SetBaseGameSpeed's real write target is not provably the
 *     field the verify-read samples (it arbitrates through GameSpeedCtrl and
 *     the Global singleton). Therefore:
 *     1) the verify-read is demoted to a diagnostics log line; the write is
 *        considered sent and apply_speed() succeeds unconditionally —
 *        exactly the v1.1.2 call pattern the user confirmed working;
 *     2) SyncGameSpeed() — the game's own per-frame OnUpdate chain, which
 *        feeds Global.SetGameSpeed and the '速度設為{0}x' readout — now
 *        ALWAYS runs after the write, plus the GodsetToast fallback;
 *     3) the guard's floor/reconcile checks ignore a 0.0 field read (an
 *        unreadable value must not trigger a per-frame re-assert loop);
 *     4) speed_resolve dumps the speed-related StageManager fields
 *        (name/offset/static) once, so the live log pins down the real
 *        layout for the next iteration if 0-reads persist.
 *   - v1.1.4: first live run of v1.1.3 crashed the game after the user
 *     stepped up to 10x (WER 12:57:13: c0000005 in GameAssembly+0x56992e;
 *     stack frames beside il2cpp_runtime_class_init and il2cpp_value_box —
 *     the il2cpp allocator/class-init region, same area as the v1.1.0 boot
 *     crash). The v1.1.3 field dump exposed the real root cause the
 *     offline probe had gotten WRONG: baseGameSpeed is a STATIC field
 *     (off=0x24 static), not an instance field. Every VAL(mgr, 0x24)
 *     "verify read" sampled garbage inside the instance object; in
 *     v1.1.2 those garbage-driven guard branches were inert because
 *     apply_speed() always returned false, but v1.1.3 made it succeed
 *     unconditionally — so the dialogue-force-1x and speed-floor branches
 *     started running apply_speed() EVERY FRAME on garbage decisions,
 *     each call going through the full SetGameSpeed entry whose only
 *     official caller is the scene-load coroutine. Fixes:
 *     1) baseGameSpeed is read through il2cpp_field_static_get_value
 *        (the same API already used for f_grace) when the field is
 *        static, so the guard finally sees the real speed; API failure
 *        degrades to a 0.0 read which the guard already ignores.
 *     2) the full SetGameSpeed entry now runs ONLY on a player keypress
 *        (step_apply); every guard-driven apply uses SetBaseGameSpeed +
 *        SyncGameSpeed alone — both are executed by the game itself
 *        every frame (OnUpdate), proven-safe call targets.
 *     3) a 1 s fuse on all guard-driven applies so no future garbage
 *        input can ever drive per-frame writes again.
 *     4) diagnostics logging is throttled (dialogue branch + mismatch
 *        line), and the resolve log states the field's static/instance
 *        nature explicitly.
 *   - v1.1.5: v1.1.4 crashed again with the IDENTICAL WER signature
 *     (GameAssembly+0x56992e, c0000005, same bucket hash as v1.1.3) —
 *     so the v1.1.4 guard fixes were right but had not touched the real
 *     trigger. The captured crash-session mod log (12 clean keypresses,
 *     no guard activity, ends right after the last keypress log line)
 *     plus the Unity stack pinned it:
 *       RIP +0x56992e (il2cpp internals)
 *       +0x52fefa  (array/box allocation path)
 *       +0xb761f2  INSIDE GodsetToast$$DrawGUI (0xb75fa0..0xb76460)
 *       +0x5354e9  (runtime_invoke / class-init infrastructure)
 *     i.e. our runtime_invoke'd GodsetToast.Show() succeeded, set the
 *     toast state, and the game's OWN per-frame DrawGUI crashed while
 *     rendering it (GUI style/box allocation). GodsetToast is a debug
 *     UI; DrawGUI has zero production mileage and we cannot control
 *     what our marshalled (string, float?) argument leaves behind.
 *     Fix: GodsetToast is NO LONGER called or even resolved — the
 *     in-game readout question moves to a D3D11-drawn OSD (next
 *     release); until then the mod is stable-first, OSD-less.
 *   - v1.1.6: two things at once, in the order the user asked for.
 *     1) OSD FINALLY ARRIVES, and it is ours: a layered click-through
 *        child window painted with plain GDI shows "x2 / x5 / x10" in the
 *        lower-right corner for 2.5 s after every speed change. No D3D
 *        state, no IL2CPP object, nothing borrowed from the game — the
 *        exact opposite of the GodsetToast approach that cost two
 *        crashes. Hidden whenever there is nothing to show.
 *     2) the "sudden slowdown" report (no crash this time) is traced to
 *        the guard's own semantics rather than a new game bug: the live
 *        log shows the game legitimately taking 1x during story events
 *        and our guard correctly restoring the step afterwards, but the
 *        reconcile branch also treated any game-side change as a
 *        legitimate "let it win" release. When the game takes the speed
 *        for a moment and never hands it back (a longer cutscene whose
 *        dialogue flags we cannot see), the step stays lost — which the
 *        player experiences as "suddenly slower, no reason". The guard
 *        now re-asserts on a bounded delay instead of releasing forever,
 *        and the OSD above makes the actual value visible while it
 *        happens.
 *   - v1.1.7: two follow-ups straight from the v1.1.6 playtest.
 *     1) the OSD badge is no longer a 2.5 s flash — it is PERSISTENT
 *        (user: "当前速度样式始终显示，不需要消失"). After the first +/-
 *        press the lower-right badge stays on screen and tracks the live
 *        step; it only hides while the game is not foreground.
 *     2) story sequences could still run the "walk/move" segments at the
 *        configured multiplier (user: 剧情中人物移动按设置速度移动，只有
 *        说话才是原速). Root cause is a signal-coverage gap, not a guard
 *        bug: the guard keys on the game's own arbitration signals
 *        get_IsDialogueNow / get_IsEventShowingNow, and during a
 *        story-driven character walk BOTH can read false — so the guard
 *        keeps re-asserting the step and the movement is sped up. Only
 *        when the talk panel pops (dialogue) do they turn true and we
 *        drop to 1x. Offline disassembly of the game's GameSpeedCtrl
 *        located a WIDER predicate the game itself already exposes:
 *        get_IsEventContext (RVA 0x799B40 = "we are inside the story
 *        region, walk included"), whose body calls
 *        get_IsEventShowingNow plus UnityEngine.Time$$get_unscaledTime
 *        — i.e. it ORs the live flag with a grace window
 *        (lastEventShowingAt + EventShowGraceSeconds) that covers the
 *        whole event interval, not just the dialogue frames.
 *        Fix (user picked "只补齐「说话」之外的空档"): resolve
 *        get_IsEventContext BY NAME (no new hardcoded RVA) and treat it
 *        as a third, wider trigger for the existing 1x hold — dialogue
 *        lock and event lock behaviours are unchanged, we merely extend
 *        the same hold to the gaps between talking. The log line now
 *        names which predicate fired (dialogue / event / event context).
 *   - v1.1.8: v1.1.7's wider trigger over-corrected, and the playtest found
 *     the mirror-image case. A character walking in from off-map (both
 *     ours and the enemy's) is ALSO an "event": it satisfies
 *     get_IsEventContext, so v1.1.7 held it at 1x — the user reported the
 *     entrance walk still ran at normal speed while everything else
 *     obeyed the step ("人物从地图外入场，入场走路速度还是慢的").
 *     So the two situations — story movement (should be 1x) and an
 *     entrance walk (should follow the step) — both look like "in an
 *     event" from the outside. The game does distinguish them, though:
 *     it tracks segmentHasDialogue, "did this event segment contain any
 *     dialogue at all". An entrance walk is pure movement and never sets
 *     it; a story scene with talking does.
 *     Fix: when the trigger is the WIDE event context (never when it is
 *     the talk panel itself or a live event show), consult
 *     segmentHasDialogue — unset means pure walk-in, so we let the step
 *     stand instead of forcing 1x. Resolved by name and type-checked as
 *     a static System.Boolean; if anything about it is unexpected we log
 *     and fall back to exact v1.1.7 behaviour. Story scenes with dialogue
 *     are unaffected, so the v1.1.7 fix is preserved.
 *   - v1.1.9: the user corrected the criterion itself and reported a race.
 *     (a) "具有配音的剧情才恢复原速，有的比如对手战死，但是没有语音输出的
 *     情况，这种情况也是保持设置速度" — the rule is NOT "was there any
 *     dialogue in this segment" but "is there any VOICE right now". A
 *     battle sequence where the opponent dies (no voice line at all) must
 *     keep the configured step.
 *     (b) "少数情况下会出现对战过程原速度的情况" — segmentHasDialogue is a
 *     LATCH: once any line of dialogue has played in the current event
 *     segment it stays true, so a later voiceless battle beat inside the
 *     same segment inherited the 1x hold. That is exactly the intermittent
 *     "the fight suddenly ran at normal speed" report.
 *     Offline forensics (speedkey/build/meta/voice-asm.txt, voice-asm2.txt,
 *     voice-criteria-research.md) pinned the game's own voice plumbing:
 *       - NewUIDialogueTalk.PlayTalk is an async state machine
 *         (<PlayTalk>d__134.MoveNext @RVA 0x8A2C40) whose 9th parameter is
 *         `voiceName`; it calls TableMgr.TryGetVoice and only enters the
 *         Game$$get_Audio playback path when a voice resource exists. The
 *         disassembly shows the explicit `call TableMgr.TryGetVoice` /
 *         `test eax,eax` / `jz <no-voice branch>` sequence.
 *       - The UI keeps the per-page answer in NewUIDialogueTalk's
 *         `_curPageHasVoice` (instance bool, declaration index 62), next to
 *         `_curPageVoiceEnd` (the voice-finished callback) and
 *         `_curPageActive`.
 *     Fix (user picked option A): resolve NewUIDialogueTalk BY NAME and read
 *     `_curPageHasVoice` off the live instance, which is captured passively
 *     by a MinHook detour on NewUIDialogueTalk.Update (the same passive-
 *     capture idiom already used for get_StageMgr / Singleton<LogicMgr>).
 *     The 1x hold now requires "the talk UI is up AND its current page has a
 *     voice"; everything else — entrance walks, the death of an opponent,
 *     any voiceless beat — follows the configured step. No new hardcoded
 *     RVA. If the class, the field or the hook cannot be resolved we log
 *     once and fall back to exact v1.1.8 behaviour.
 *   - v1.1.10: v1.1.9's voice criterion turned out NEVER TO FIRE in the real
 *     game. The user reported "剧情中的人物移动依旧还是慢的" and asked,
 *     when prompted, whether a dialogue box was on screen at the time: it
 *     was not — the character just walked across with no dialogue box. The
 *     live log confirmed it: "NewUIDialogueTalk.Update hooked" appears, but
 *     "NewUIDialogueTalk instance captured" never does, and neither do the
 *     "voiced page" / "Voiceless beat" log lines. `_curPageHasVoice` lives on
 *     the dialogue UI component, so when no dialogue box is open there is no
 *     instance at all and the passive hook has nothing to capture: the
 *     criterion read "unavailable" every single frame, took the fallback
 *     branch, and let the segmentHasDialogue latch decide — i.e. behaviour
 *     silently degraded to v1.1.8, which is exactly the symptom reported.
 *     Fix: add the missing gate. GameSpeedCtrl.talkPanelShowing is a STATIC
 *     bool ("is the dialogue panel on screen right now"), read straight from
 *     the static area with no instance and no hook involved. The decision
 *     becomes two-stage:
 *       panel OFF -> a walk / a death / an entrance / a silent beat
 *                    -> follow the configured step (user's explicit choice:
 *                       only a page WITH a voice holds 1x)
 *       panel ON  -> voiced page   -> hold 1x
 *                    voiceless page -> follow the step
 *                    unreadable     -> hold 1x (conservative: better slow
 *                                      than to fast-forward real dialogue)
 *     segmentHasDialogue is no longer consulted at all while the gate is
 *     available — it is a latch, and that latch was the v1.1.8 bug. It
 *     remains only as the fallback when talkPanelShowing itself cannot be
 *     resolved, so the worst case is still exact v1.1.8 behaviour.
 *
 *   - v1.1.12 (v1.1.11 was the v2.0.3 umbrella-integration build): two
 *     real-machine bugs, one root cause. On the big map there is usually
 *     NO StageManager (battle-scene object) — apply_speed() hard-requires
 *     an instance, so +/- only REMEMBERED the step ("No StageManager yet;
 *     step N remembered", 18 times in 10 s in the live log) and once the
 *     game reset the speed to 1 (map re-entry / menus) the guard could
 *     not re-assert because it also bails without a StageManager. The
 *     user saw: speed not adjustable on the big map (especially slowing
 *     down) and menus/item use knocking the speed back to 1x forever.
 *     Fix (user-picked option A):
 *     1) Global singleton channel. Offline evidence (meta/bigma-speed-asm
 *        .txt): StageManager.SetBaseGameSpeed itself forwards into
 *        Global.get_Instance, and the GM command's sequence is exactly
 *        get_Instance() -> SetGameSpeed(speed). Global.SetGameSpeed is
 *        the game-wide speed arbiter and needs NO StageManager. The
 *        instance comes from the static field Global._instance (the same
 *        read the game's own getter makes) — no new hook, zero boot-
 *        window risk.
 *     2) Menu state machine (user-picked behaviour: hold 1x while a menu
 *        is open, restore the step IMMEDIATELY when it closes — no 3 s
 *        yield). Signals: GameSpeedCtrl.battlePopupShowing (static bool)
 *        for battle popups, BigMapLogic.IsOtherUIVisible() for big-map
 *        UI. Keypresses while a menu is open only remember the step so
 *        the guard never fights the keypress; the deferred step applies
 *        on close.
 *     3) The 3 s guard-yield re-assert now goes through the Global
 *        channel when no StageManager exists, so a speed the game
 *        borrowed on the big map comes back after the yield instead of
 *        being lost. The dialogue-hold and speed-floor branches stay
 *        instance-anchored (battle-tuned, unchanged).
 *
 * Layout and the proxy/ABI spine follow the MIT-licensed reference
 * implementation by Doomsdayer (see LICENSE.txt).
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

#define SK_LOG_FILE L"hslr-speed-hotkey.log"
#define SK_CONFIG_FILE L"hslr-speed-hotkey.ini"
#define SK_VERSION "1.1.25"

/* Accepted GameAssembly.dll builds. Same pair the wait-hotkey mod accepts:
 * the stock build and the build patched by the unrelated "nightmare
 * randomiser" Mod. Anything else disables the Mod entirely. */
static const char *const sk_accepted_hashes[] = {
    "1fda3e3add16e97abfd8ced98c24de839f0b079db803ae6f5a7867800fe03f1e",
    "7333475e7176b1f7395c724470a80fffd08e706295ba58fbbf59f0105e6634f1",
    "6aea28ddb7951f5df240f2fecc8cb16a79a8ecd0eb1cdc2b1343798a02be198c",
    "d969e10b94b47d423c701c1dd5fa6986cc8c8c5774519a89b2605fe91285bc09",
    "b023545ec4bc3bc23bf6193e55bb61e8583173b9dc65b1dec2e452e580296b45"
};

static HMODULE self, ga;
static HANDLE logfile = INVALID_HANDLE_VALUE;

/* 本模块的 SHA256 白名单（known_build 用）。 */
#define HSLR_ACCEPTED_HASHES sk_accepted_hashes

/* 日志 / 长路径 / 构建校验 —— 四模块共用实现，见 shared/common.h。 */
#include "common.h"

static bool enabled, state_seen;
static SKConfig config;

/* 【v2.3.0 伞形总控】读 hslr-wait-hotkey.ini 的 [features] speed 键。
 * 文件缺失或键不存在 = 视为开，老配置文件行为不变；mtime 变化时热重载。 */
#define SK_UMBRELLA_INI L"hslr-wait-hotkey.ini"
static bool umbrella_enable = true;
static bool umbrella_seen;
static ULONGLONG umbrella_stamp;
/* 前置声明：定义在 config 热重载区（config_setup 之前要能调用）。 */
static void umbrella_read_speed(bool *out);
static void umbrella_features_reload(void);

/* 【v2.3.0 modId 自愈白名单】ini 的 mod_id 在游戏条目列表里一个都对不上时，
 * 按这份内置列表回退——全是自家历史/现用 ID：
 *   0 = 整合条目「待机快捷键」（v2.0.0 起的正式 ID）
 *   1 = 老独立版条目「战斗倍速快捷键」（残留 ini 可能仍指向它） */
#define SK_FALLBACK_IDS 2
static WCHAR sk_fallback_ids[SK_FALLBACK_IDS][SK_MOD_ID_MAX];
static int sk_fallback_chars[SK_FALLBACK_IDS];
static bool sk_fallback_ready;
static bool sk_fallback_active;   /* 当前是否正用回退 ID（供日志只提示一次） */

static void sk_fallback_init(void) {
    static const char *const ids[SK_FALLBACK_IDS] = {
        SK_DEFAULT_MOD_ID_UTF8,   /* 待机快捷键 */
        "战斗倍速快捷键",
    };
    for (int i = 0; i < SK_FALLBACK_IDS; ++i) {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ids[i], -1,
                                    sk_fallback_ids[i], SK_MOD_ID_MAX - 1);
        if (n <= 0)
            n = MultiByteToWideChar(CP_ACP, 0, ids[i], -1,
                                    sk_fallback_ids[i], SK_MOD_ID_MAX - 1);
        sk_fallback_chars[i] = (n > 0) ? n - 1 : 0;
    }
    sk_fallback_ready = true;
}

#define PTR(p, o) (*(void **)((BYTE *)(p) + (o)))
#define VAL(t, p, o) (*(t *)((BYTE *)(p) + (o)))
/* 【v2.9.0 免版本跟进】FN 先过运行时重定位层，详见 hotkey/src/rva_reloc.h。 */
#define FN(rva, ret, ...) ((ret(*)(__VA_ARGS__))((BYTE *)ga + rva_of(rva)))

/* Field flag / method flag bits we check (System.Reflection semantics). */
#define IL2CPP_FIELD_ATTRIBUTE_STATIC 0x0010u
#define IL2CPP_METHOD_ATTRIBUTE_STATIC 0x0010u

/* 【v2.9.0 免版本跟进】运行时重定位层（把"上一次重解时的 RVA"翻译成当前版本
 * 的 RVA）。NATIVE = 恒等映射（零开销）；ADAPTIVE = 长掩码指纹锚点窗口搜索。
 * 锚点表 rva_anchors.h 由 tools/analyze/capture_fingerprints.py 生成。 */
#define RVA_LOG logmsg
#include "rva_reloc.h"

/* ------------------------------------------------------------------ */
/* Build identity                                                      */
/* ------------------------------------------------------------------ */

/* 字节指纹校验。构建未变时 = 原逐点字节校验（行为与 v2.8.0 相同）；
 * 构建未知时 = 重定位层的逐点结果（方案 C：不再因哈希不认识就整体停机）。 */
static bool check_fingerprints(void) {
    if (rva_mode == RVA_MODE_NATIVE) {
        for (unsigned i = 0; i < sizeof(sk_fingerprints) / sizeof(sk_fingerprints[0]); i++) {
            const SKFingerprint *f = &sk_fingerprints[i];
            if (memcmp((BYTE *)ga + f->rva, f->bytes, 16)) {
                logmsg("Fingerprint mismatch %s RVA=%llx", f->name, (unsigned long long)f->rva);
                return false;
            }
        }
        logmsg("%u fingerprints verified", (unsigned)(sizeof(sk_fingerprints) / sizeof(sk_fingerprints[0])));
        return true;
    }
    if (!rva_all_ok()) {
        logmsg("reloc: %d of %u anchors could not be relocated; battle-speed disabled",
               rva_failed(), (unsigned)RVA_ANCHOR_COUNT);
        return false;
    }
    logmsg("reloc: all %u anchors relocated; battle-speed active on an unknown build",
           (unsigned)RVA_ANCHOR_COUNT);
    return true;
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
static void *(*i_class_method)(void *, const char *, int);
static void *(*i_class_field)(void *, const char *);
static void *(*i_class_fields)(void *, void **);
static const char *(*i_field_name)(void *);          /* diagnostics only */
static uint32_t (*i_field_flags)(void *);
static int32_t (*i_field_offset)(void *);
static void *(*i_field_type)(void *);
static const char *(*i_type_name)(void *);            /* il2cpp_type_get_name (UTF-8) */
static void (*i_field_static_get_value)(void *, void *);
static void *(*i_type_class)(void *);                 /* il2cpp_type_get_class_or_element_class */
static uint32_t (*i_method_flags)(void *, uint32_t *);
static void *(*i_class_get_type)(void *);             /* il2cpp_class_get_type */
static void *(*i_type_get_object)(void *);            /* Il2CppType* -> System.Type object */
static void *(*i_runtime_invoke)(void *, void *, void **, void **);
static void *(*i_class_get_parent)(void *);           /* il2cpp_class_get_parent */
static void *(*i_string_new)(const char *);           /* il2cpp_string_new (UTF-8) */
static void *(*i_method_get_param)(void *, unsigned); /* il2cpp_method_get_param */

static bool il2cpp_bind(void) {
    struct { const char *name; void **target; bool required; } table[] = {
        {"il2cpp_domain_get",              (void **)&i_domain_get,           true},
        {"il2cpp_domain_get_assemblies",   (void **)&i_domain_assemblies,    true},
        {"il2cpp_assembly_get_image",      (void **)&i_assembly_image,       true},
        {"il2cpp_image_get_class_count",   (void **)&i_image_class_count,    true},
        {"il2cpp_image_get_class",         (void **)&i_image_class,          true},
        {"il2cpp_class_get_name",          (void **)&i_class_name,           true},
        {"il2cpp_class_get_method_from_name", (void **)&i_class_method,      true},
        {"il2cpp_class_get_field_from_name",  (void **)&i_class_field,       true},
        {"il2cpp_class_get_fields",        (void **)&i_class_fields,         true},
        {"il2cpp_field_get_name",          (void **)&i_field_name,           false},
        {"il2cpp_field_get_flags",         (void **)&i_field_flags,          true},
        {"il2cpp_field_get_offset",        (void **)&i_field_offset,         true},
        {"il2cpp_field_get_type",          (void **)&i_field_type,           true},
        {"il2cpp_type_get_name",           (void **)&i_type_name,            false},
        {"il2cpp_field_static_get_value",  (void **)&i_field_static_get_value, true},
        {"il2cpp_type_get_class_or_element_class", (void **)&i_type_class,   true},
        {"il2cpp_method_get_flags",        (void **)&i_method_flags,         true},
        {"il2cpp_class_get_type",          (void **)&i_class_get_type,       true},
        {"il2cpp_type_get_object",         (void **)&i_type_get_object,      true},
        {"il2cpp_runtime_invoke",          (void **)&i_runtime_invoke,       true},
        {"il2cpp_class_get_parent",        (void **)&i_class_get_parent,     true},
        {"il2cpp_string_new",              (void **)&i_string_new,           true},
        {"il2cpp_method_get_param",        (void **)&i_method_get_param,     true},
    };
    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        *table[i].target = (void *)GetProcAddress(ga, table[i].name);
        if (!*table[i].target && table[i].required) {
            logmsg("Missing IL2CPP export %s; speed chain cannot be resolved", table[i].name);
            return false;
        }
    }
    return true;
}

/* Iterate every loaded image and every class exactly once, calling cb. */
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

/* Find a class by simple name; an optional validator re-checks members so a
 * same-named class from another namespace cannot satisfy the lookup. */
struct find_ctx { const char *name; bool (*validate)(void *); void *result; };
static void find_cb(void *klass, void *ud) {
    struct find_ctx *ctx = (struct find_ctx *)ud;
    if (ctx->result) return;
    const char *nm = i_class_name(klass);
    if (!nm || strcmp(nm, ctx->name)) return;
    if (ctx->validate && !ctx->validate(klass)) return;
    ctx->result = klass;
}
static void *find_class(const char *name, bool (*validate)(void *)) {
    struct find_ctx ctx = {name, validate, NULL};
    for_each_class(find_cb, &ctx);
    return ctx.result;
}

/* Global search for a STATIC method by name+arity (used for the get_StageMgr
 * facade when it does not live on StageManager itself). */
struct mfind_ctx { const char *name; int params; void *result; };
static void mfind_cb(void *klass, void *ud) {
    struct mfind_ctx *ctx = (struct mfind_ctx *)ud;
    if (ctx->result) return;
    void *m = i_class_method(klass, ctx->name, ctx->params);
    if (!m) return;
    uint32_t flags = i_method_flags(m, NULL);
    if (!(flags & IL2CPP_METHOD_ATTRIBUTE_STATIC)) return;
    ctx->result = m;
}
static void *find_static_method_global(const char *name, int params) {
    struct mfind_ctx ctx = {name, params, NULL};
    for_each_class(mfind_cb, &ctx);
    return ctx.result;
}

/* First static field whose type is exactly type_klass (the classic singleton
 * pattern). Prefers a field that already holds an instance. */
static void *find_static_field_typed(void *klass, void *type_klass) {
    void *iter = NULL, *field, *fallback = NULL;
    while ((field = i_class_fields(klass, &iter))) {
        if (!(i_field_flags(field) & IL2CPP_FIELD_ATTRIBUTE_STATIC)) continue;
        void *t = i_field_type(field);
        if (!t || i_type_class(t) != type_klass) continue;
        void *v = NULL;
        i_field_static_get_value(field, &v);
        if (v) return field;
        if (!fallback) fallback = field;
    }
    return fallback;
}

/* ------------------------------------------------------------------ */
/* Speed chain: resolved by NAME on the game's main thread, once       */
/* ------------------------------------------------------------------ */

static struct {
    void *stage_klass;        /* StageManager */
    void *m_set_speed;        /* StageManager.SetBaseGameSpeed(float) */
    void *f_base_speed;       /* StageManager.baseGameSpeed */
    int   base_speed_off;     /* its field offset (v1.1.4: this is a
                                 STATIC-field offset — reads must go
                                 through il2cpp_field_static_get_value,
                                 see base_speed_static) */
    bool  base_speed_static;  /* v1.1.4: runtime FieldInfo says static —
                                 the v1.1.3 offline probe that called it
                                 an instance field was wrong, and every
                                 VAL(mgr, off) read was garbage */
    void *m_get_stage_mgr;    /* static get_StageMgr (facade), optional */
    void *f_stage_singleton;  /* static StageManager instance, fallback */
    void *m_set_speed_full;   /* StageManager.SetGameSpeed(float) — the full
                                 official entry (field + UI sync), optional */
    void *m_sync_speed;       /* StageManager.SyncGameSpeed() — refreshes the
                                 game's own speed readout, optional */
    void *toast_klass;        /* GodsetToast (optional, OSD text) */
    void *m_toast;            /* GodsetToast.Show(string, float?) */
    void *logic_klass;        /* LogicMgr */
    void *m_foaf;             /* Object.FindObjectOfType(Type). Metadata has
                                 NO static field of type LogicMgr anywhere
                                 (verified by tools/analyze/logicmgr_holders.py),
                                 so the instance can only come from the scene.
                                 v1.1.2: this is now only the FALLBACK path —
                                 the primary source is the hooked getter. */
    void *logic_get_inst;     /* Singleton`1<LogicMgr>.get_Instance (static) —
                                 hooked in v1.1.2; the game's own calls hand
                                 us the instance with zero proactive work */
    void *logic_cached;       /* last LogicMgr instance we acquired */
    uint64_t logic_cached_at; /* when the cache was last refreshed */
    uint64_t logic_retry_at;  /* FindObjectOfType retry throttle */
    bool logic_tried;         /* a FindObjectOfType attempt has happened */
    int logic_fail_count;     /* failed FindObjectOfType attempts so far */
    void *gs_klass;           /* GameSpeedCtrl (optional) */
    void *gs_instance;        /* its singleton (optional) */
    void *m_is_dialogue;      /* get_IsDialogueNow (optional) */
    void *m_is_event;         /* get_IsEventShowingNow (optional) */
    void *m_is_event_ctx;     /* v1.1.7: get_IsEventContext (optional, wider) */
    void *f_grace;            /* EventShowGraceSeconds (optional) */
    float grace_seconds;
    /* v1.1.8: segmentHasDialogue — "本次演出段落里出现过台词吗" 的游戏自有标志。
     * 入场走位（乙方/敌方从地图外走进来）也是一个 event，会命中 IsEventContext
     * 而被锁 1x；但入场段落自始至终没有台词，游戏把这个标志保持为假。用它把
     * 「纯走位」与「剧情演出」区分开，入场就跟随设定档位。
     * 同样只按名解析 + il2cpp_field_static_get_value，不加硬编码偏移。 */
    void *f_seg_dialogue;
    bool seg_dialogue_ready;  /* 该字段成功解析且确实是静态 bool */
    /* v1.1.10: talkPanelShowing —— 游戏自己的「对话框此刻是否可见」标志。
     *
     * 为什么非有它不可：v1.1.9 把判据换成 NewUIDialogueTalk._curPageHasVoice
     * 后，实机日志暴露它**从未生效** —— 该字段挂在对话框 UI 组件上，对话框
     * 不弹出时组件根本没有实例，钩子也就收不到，读值恒为「不可用」。
     * 于是每次都走两级回退、由 segmentHasDialogue（残留 latch）决定，行为
     * 退化成 v1.1.8：剧情里人物默默走位（无对话框）照样被锁 1x。
     *
     * talkPanelShowing 是 GameSpeedCtrl 的**静态 bool**（声明序 6，见
     * meta/event-fields.txt），静态区直读，不依赖任何实例或钩子。它回答
     * 「对话框现在开着吗」，正好补上缺的那一环：
     *
     *   talkPanelShowing 假 → 对话框没开 = 走位/战死/入场 → 跟随设定档位
     *   talkPanelShowing 真 → 再看 _curPageHasVoice 决定这一页锁不锁 1x
     *
     * 只按名解析 + il2cpp_field_static_get_value，不加硬编码偏移。 */
    void *f_talk_panel;
    bool talk_panel_ready;
    /* v1.1.9: 「本页有没有配音」判据。
     * 用户纠正：判据不是「这段演出有没有台词」（那是会残留的 latch），
     * 而是「这一页有没有语音输出」。对手战死这类无语音的战斗演出必须
     * 保持玩家设定的档次。
     * 离线取证（meta/voice-criteria-research.md）：游戏在 PlayTalk 里先用
     * TableMgr.TryGetVoice 探测配音资源，有才走 Game.Audio 播放；UI 层把
     * 结果落在 NewUIDialogueTalk._curPageHasVoice（实例 bool，声明序号 62），
     * 紧邻 _curPageVoiceEnd（配音播完回调）与 _curPageActive。
     * 实例来源：MinHook 钩 NewUIDialogueTalk.Update 的编译体被动收（与本
     * Mod 既有的 get_StageMgr / Singleton`1<LogicMgr> 捕获范式一致），
     * 全程不做主动 IL2CPP 对象模型调用（启动窗口期铁律）。 */
    void *talk_klass;         /* NewUIDialogueTalk (optional) */
    void *f_page_has_voice;   /* _curPageHasVoice (instance bool, optional) */
    int   page_has_voice_off; /* 其运行时偏移 */
    void *talk_update_code;   /* Update() 编译体，MinHook 目标 */
    void *talk_cached;        /* 被动收到的 NewUIDialogueTalk 实例 */
    uint64_t talk_cached_at;
    bool  talk_ready;         /* 类 + 字段 + 偏移全部校验通过 */
    /* v1.1.12: Global 单例通道 —— 大地图/菜单场景的官方写速入口。
     *
     * 为什么非有它不可（实机日志 2026-09-20）：StageManager 是战斗场景
     * 对象，大地图上通常不存在 —— apply_speed 硬性要求实例，于是大地图
     * 上按 +/- 只记档不写入（"No StageManager yet; step N remembered"），
     * 游戏把速度压回 1x 后守卫也无法抢回（guard_tick 在 mgr 为空时直接
     * 退出）= 用户报的两个 BUG。
     *
     * 取证（meta/bigma-speed-asm.txt）：
     *   StageManager.SetBaseGameSpeed 内部自己就调 Global.get_Instance
     *   转发；GM 调试命令 GmCommand.SetGameSpeed 的调用序列正是
     *   get_Instance() -> SetGameSpeed(speed)。Global.SetGameSpeed 是
     *   全游戏写速总闸，全程不需要 StageManager 实例。
     *
     * 实例来源：读 Global._instance 静态字段（get_Instance 的编译体里
     * 就是这么读的），静态区直读，不加新钩子、零启动窗口风险。 */
    void *global_klass;       /* Global */
    void *f_global_inst;      /* Global._instance（静态） */
    void *m_global_set_speed; /* Global.SetGameSpeed(float)（实例方法） */
    bool  global_ready;       /* 三者齐 + 方法指针可执行 */
    /* v1.1.12: 菜单/弹窗判据（用户拍板：菜单期间保持 1x，关闭立即恢复）。 */
    void *f_battle_popup;     /* GameSpeedCtrl.battlePopupShowing（静态 bool） */
    bool  battle_popup_ready;
    void *m_is_other_ui;      /* BigMapLogic.IsOtherUIVisible()（实例 bool） */
    /* v1.1.13: 菜单信号诊断探针（用户拍板方案 A：先诊断后修）。
     * 全部只读，任何一项解析失败只降级不打日志噪音。 */
    void *f_uishow;           /* Global.UIShow（静态，类型待运行时确认） */
    int   uishow_size;        /* 1=bool / 4=int32，按类型名判定；0=不可用 */
    int32_t state_off;        /* StageManager.state（实例）偏移；-1=不可用 */
    int32_t optpause_off;     /* StageManager._optionPauseLckSt（实例）偏移 */
    void *f_any_depth;        /* GameSpeedCtrl.anyActionDepth（静态 int） */
    void *f_dlg_depth;        /* GameSpeedCtrl.dialogueActionDepth（静态 int） */
    void *f_intent;           /* GameSpeedCtrl.eventSpeedIntent（静态 bool/int） */
} speed;
static bool speed_ready;
static int resolve_attempts;
static uint64_t resolve_next_try;

/* v1.1.2 passive capture state (see the header comment for the why). */
static void *m_get_stage_code;       /* get_StageMgr compiled code pointer */
static void *m_logic_get_inst_code;  /* Singleton`1<LogicMgr>.get_Instance code */
static void *(*orig_get_stage)(void *);       /* MinHook trampolines */
static void *(*orig_logic_get_inst)(void *);
static bool stage_hook_done, stage_hook_failed;
static bool logic_hook_done, logic_hook_failed;
static void *(*orig_talk_update)(void *, void *);   /* v1.1.9 trampoline */
static bool talk_hook_done, talk_hook_failed;
/* v1.1.14 写入者追踪（诊断版）：被动钩 Global.SetGameSpeed，记录每次写速的
 * 值 + 调用方模块偏移，定位「大地图上是谁在把速度压回 1x」。 */
static void (*orig_global_set_speed)(void *, float, void *);
static void hk_global_set_speed(void *self, float v, void *mi);
static bool gset_hook_done, gset_hook_failed;
static void *stage_cached;           /* last non-null StageManager the game got */
static uint64_t stage_cached_at;
static uint64_t arm_probe_next;      /* 120 s fallback arming probe */

/* forward decls (definitions live further down; resolvers run earlier) */
static void *method_code(void *mi);
static void install_speed_hooks(void);

static bool stage_validate(void *k) {
    return i_class_method(k, "SetBaseGameSpeed", 1) &&
           i_class_field(k, "baseGameSpeed");
}

/* v1.1.12: "Global" 是常见类名 —— 校验器要求成员指纹（静态 _instance、
 * 实例 SetGameSpeed(float)、UnlockOptionPause），三个都命中才算正主，
 * 防止别的程序集里的同名 Global 抢先匹配。 */
static bool global_validate(void *k) {
    return i_class_field(k, "_instance") &&
           i_class_method(k, "SetGameSpeed", 1) &&
           i_class_method(k, "UnlockOptionPause", 0);
}

static bool speed_resolve(void) {
    memset(&speed, 0, sizeof(speed));
    speed.grace_seconds = 2.0f;

    speed.stage_klass = find_class("StageManager", stage_validate);
    if (!speed.stage_klass) {
        logmsg("Speed resolve: no class named StageManager with SetBaseGameSpeed + baseGameSpeed");
        return false;
    }
    speed.m_set_speed = i_class_method(speed.stage_klass, "SetBaseGameSpeed", 1);
    speed.f_base_speed = i_class_field(speed.stage_klass, "baseGameSpeed");
    if (!speed.m_set_speed || !speed.f_base_speed) {
        logmsg("Speed resolve: StageManager members incomplete");
        return false;
    }
    /* Full official entry + UI sync (both verified to exist in the metadata).
     * v1.1.4: SetGameSpeed(float) is the scene-load coroutine's entry — it
     * runs ONLY on a player keypress (see apply_speed's with_full), while
     * guard paths write through SetBaseGameSpeed alone. SyncGameSpeed()
     * refreshes the game's own speed readout — that is what makes the game
     * display the new multiplier. */
    speed.m_set_speed_full = i_class_method(speed.stage_klass, "SetGameSpeed", 1);
    speed.m_sync_speed = i_class_method(speed.stage_klass, "SyncGameSpeed", 0);
    speed.base_speed_off = i_field_offset(speed.f_base_speed);
    /* v1.1.4: the runtime FieldInfo decides HOW the field is read. The live
     * dump proved baseGameSpeed static; trusting the metadata for real so
     * the guard reads the actual value instead of instance garbage. */
    speed.base_speed_static =
        (i_field_flags(speed.f_base_speed) & IL2CPP_FIELD_ATTRIBUTE_STATIC) != 0;
    /* Sanity: field offsets are positive and small; a bogus offset would
     * make any instance-fallback read scan random memory. */
    if (speed.base_speed_off < 0x10 || speed.base_speed_off > 0x10000) {
        logmsg("Speed resolve: baseGameSpeed offset 0x%x looks wrong", speed.base_speed_off);
        return false;
    }
    logmsg("Speed resolve: baseGameSpeed is a %s field (off=0x%x); reads via %s",
           speed.base_speed_static ? "STATIC" : "instance",
           (unsigned)speed.base_speed_off,
           speed.base_speed_static ? "il2cpp_field_static_get_value"
                                   : "instance+offset");
    /* v1.1.3 diagnostics: the live verify-read of baseGameSpeed always
     * returned 0 (v1.1.2 log). Dump the speed-related neighbourhood of the
     * class once so the real layout (and any static/offset surprise) is in
     * the log for the next iteration. */
    {
        void *iter = NULL, *fld;
        int dumped = 0;
        while (dumped < 16 && (fld = i_class_fields(speed.stage_klass, &iter))) {
            const char *fn = i_field_name(fld);
            int off = i_field_offset(fld);
            if (!fn || off < 0) continue;
            bool st = (i_field_flags(fld) & IL2CPP_FIELD_ATTRIBUTE_STATIC) != 0;
            bool named = strstr(fn, "peed") != NULL;      /* speed/Speed */
            bool close_by = off >= speed.base_speed_off - 0x20 &&
                            off <= speed.base_speed_off + 0x20;
            if (!named && !close_by) continue;
            logmsg("  StageManager field %-28s off=0x%x%s", fn, off,
                   st ? " static" : "");
            dumped++;
        }
    }

    /* StageManager instance: prefer the game's own static facade accessor,
     * fall back to a typed static singleton field. v1.1.2: the getter is
     * HOOKED (compiled code = MethodInfo->methodPointer) so the instance
     * arrives passively; the pointer must validate as executable
     * GameAssembly code or the passive channel is declared unusable. */
    speed.m_get_stage_mgr = i_class_method(speed.stage_klass, "get_StageMgr", 0);
    if (speed.m_get_stage_mgr &&
        !(i_method_flags(speed.m_get_stage_mgr, NULL) & IL2CPP_METHOD_ATTRIBUTE_STATIC))
        speed.m_get_stage_mgr = NULL;
    if (!speed.m_get_stage_mgr)
        speed.m_get_stage_mgr = find_static_method_global("get_StageMgr", 0);
    if (speed.m_get_stage_mgr)
        m_get_stage_code = method_code(speed.m_get_stage_mgr);
    if (speed.m_get_stage_mgr && !m_get_stage_code)
        logmsg("Speed resolve: get_StageMgr found but its code pointer failed "
               "validation; passive capture unavailable, probe uses runtime_invoke");
    if (!speed.m_get_stage_mgr)
        speed.f_stage_singleton = find_static_field_typed(speed.stage_klass, speed.stage_klass);
    if (!speed.m_get_stage_mgr && !speed.f_stage_singleton) {
        logmsg("Speed resolve: no way to obtain the StageManager instance");
        return false;
    }

    /* LogicMgr: an instance can NOT be reached through a static field — the
     * whole metadata contains no field of type LogicMgr (the game keeps it in
     * LogicMgr.logics, a container, and drives it from the scene). The scene
     * lookup UnityEngine.Object.FindObjectOfType(LogicMgr) is the audited
     * replacement; logic_mgr_instance() calls it, cached and throttled. */
    speed.logic_klass = find_class("LogicMgr", NULL);
    if (!speed.logic_klass) {
        logmsg("Speed resolve: no class named LogicMgr");
        return false;
    }
    speed.m_foaf = find_static_method_global("FindObjectOfType", 1);
    if (!speed.m_foaf) {
        logmsg("Speed resolve: Object.FindObjectOfType(Type) not found; "
               "LogicMgr has no static singleton to fall back on");
        return false;
    }
    /* v1.1.2 primary LogicMgr source: the game's own
     * Singleton`1<LogicMgr>.Instance getter. The base-class diagnostic
     * below shows LogicMgr's ancestry is Singleton`1 -> System.Object, so
     * FindObjectOfType (UnityEngine.Object derivatives only) is at best a
     * fallback. Hooking the getter hands us the instance whenever the game
     * itself asks for one — zero proactive calls, zero scene scans. */
    {
        void *lp = i_class_get_parent(speed.logic_klass);
        const char *lp_name = lp ? i_class_name(lp) : NULL;
        if (lp && lp_name && !strcmp(lp_name, "Singleton`1")) {
            void *m = i_class_method(lp, "get_Instance", 0);
            if (m && (i_method_flags(m, NULL) & IL2CPP_METHOD_ATTRIBUTE_STATIC)) {
                speed.logic_get_inst = m;
                m_logic_get_inst_code = method_code(m);
            }
        }
        if (speed.logic_get_inst && m_logic_get_inst_code) {
            logmsg("LogicMgr source: hooked Singleton`1<LogicMgr>.get_Instance "
                   "(code=%p); FindObjectOfType stays as fallback",
                   m_logic_get_inst_code);
        } else {
            logmsg("LogicMgr singleton getter not found%s; "
                   "FindObjectOfType stays the acquisition path",
                   lp_name && strcmp(lp_name, "Singleton`1")
                       ? " (parent is not Singleton`1)" : "");
        }
    }
    /* Diagnostic: walk LogicMgr's base-class chain once. FindObjectOfType
     * only works for UnityEngine.Object derivatives; the chain names in the
     * log tell us immediately whether the scene lookup can ever succeed
     * (offline the parent typeIndex could not be resolved to a class). */
    {
        void *k = speed.logic_klass;
        char chain[220] = "";
        for (int i = 0; k && i < 8; ++i) {
            const char *nm = i_class_name(k);
            if (!nm) break;
            snprintf(chain + strlen(chain), sizeof(chain) - strlen(chain),
                     "%s%s", i ? " -> " : "", nm);
            if (!strcmp(nm, "MonoBehaviour") || !strcmp(nm, "Behaviour") ||
                !strcmp(nm, "Component") || !strcmp(nm, "Object"))
                break;
            k = i_class_get_parent(k);
        }
        logmsg("LogicMgr base-class chain: %s%s", chain,
               k ? "" : " (Object derivative NOT confirmed)");
    }

    /* GodsetToast: the game's own debug toast (GodsetToast.Show(string, ...),
     * static). Used as the visible speed readout next to the game's official
     * SyncGameSpeed UI refresh. Optional — its absence only disables the
     * text OSD, never the speed feature. */
    /* v1.1.5: GodsetToast is NOT resolved and NEVER called. Two live
     * crashes (v1.1.3/v1.1.4, identical WER bucket) landed inside the
     * game's own GodsetToast$$DrawGUI while it rendered the toast our
     * runtime_invoke'd Show() had queued. A debug UI with zero
     * production mileage is not a foundation for the OSD. */

    /* GameSpeedCtrl: optional. In build 25293526 it is an abstract-sealed
     * STATIC class (every member static, no instance singleton), so the
     * dialogue signals are called as static methods; call_bool() re-checks
     * the flags at runtime so either layout works. When absent the guard
     * degrades to the instance-anchored mode (re-assert on new instances
     * only), which still never accelerates a dialogue the game slowed down. */
    speed.gs_klass = find_class("GameSpeedCtrl", NULL);
    if (speed.gs_klass) {
        speed.gs_instance = NULL;
        void *f = find_static_field_typed(speed.gs_klass, speed.gs_klass);
        if (f) i_field_static_get_value(f, &speed.gs_instance);
        speed.m_is_dialogue = i_class_method(speed.gs_klass, "get_IsDialogueNow", 0);
        speed.m_is_event = i_class_method(speed.gs_klass, "get_IsEventShowingNow", 0);
        /* v1.1.7: 用户报「剧情中人物移动按设定档位跑，只有说话是原速」。
         * 离线取证（tools/analyze/event_context_probe.py）确认：
         *   get_IsEventContext@0x799B40 = IsEventShowingNow() || (刚结束演出不久)
         * ——它是比 IsDialogueNow 更宽的「剧情区间」判据，覆盖演出中的移动。
         * 只按名解析，不加新 RVA（沿用本 Mod 的按名解析策略）。 */
        speed.m_is_event_ctx = i_class_method(speed.gs_klass, "get_IsEventContext", 0);
        speed.f_grace = i_class_field(speed.gs_klass, "EventShowGraceSeconds");
        if (speed.f_grace && (i_field_flags(speed.f_grace) & IL2CPP_FIELD_ATTRIBUTE_STATIC)) {
            float g = 0;
            i_field_static_get_value(speed.f_grace, &g);
            if (g > 0.0f && g < 30.0f) speed.grace_seconds = g;
        }
        /* v1.1.8: 入场走位用的判据字段。语义取证见 meta/event-fields.txt：
         * GameSpeedCtrl 四个 bool（eventSpeedIntent / segmentHasDialogue /
         * talkPanelShowing / battlePopupShowing）里，只有 segmentHasDialogue
         * 表示「本段演出出现过台词」。入场走位段落全程无台词 → 该标志为假
         * → 我们不再把速度压回 1x，让它跟随玩家设定档位。
         * 必须验证它确实是静态 bool，否则宁可不用（fail-safe：退回 v1.1.7 行为）。 */
        speed.f_seg_dialogue = i_class_field(speed.gs_klass, "segmentHasDialogue");
        if (speed.f_seg_dialogue) {
            void *ft = i_field_type(speed.f_seg_dialogue);
            const char *tn = ft ? i_type_name(ft) : NULL;
            bool is_static = (i_field_flags(speed.f_seg_dialogue) &
                              IL2CPP_FIELD_ATTRIBUTE_STATIC) != 0;
            if (is_static && tn && (strcmp(tn, "System.Boolean") == 0 ||
                                    strcmp(tn, "Boolean") == 0 ||
                                    strcmp(tn, "bool") == 0)) {
                speed.seg_dialogue_ready = true;
            } else {
                logmsg("segmentHasDialogue type is '%s' (static=%s); "
                       "entrance-walk fast-forward DISABLED, keeping v1.1.7 "
                       "behaviour", tn ? tn : "(null)", is_static ? "y" : "n");
                speed.f_seg_dialogue = NULL;
            }
        }
        /* v1.1.10: talkPanelShowing —— 「对话框此刻是否可见」。
         * 这是 v1.1.9 配音判据失效后补上的关键一环（见结构体注释）：
         * 它决定「现在到底该不该谈『这一页有没有配音』」。
         * 同样是静态 bool + 三重校验，拿不到就退回旧行为。 */
        speed.f_talk_panel = i_class_field(speed.gs_klass, "talkPanelShowing");
        if (speed.f_talk_panel) {
            void *ftp = i_field_type(speed.f_talk_panel);
            const char *tnp = ftp ? i_type_name(ftp) : NULL;
            bool stp = (i_field_flags(speed.f_talk_panel) &
                        IL2CPP_FIELD_ATTRIBUTE_STATIC) != 0;
            if (stp && tnp && (strcmp(tnp, "System.Boolean") == 0 ||
                               strcmp(tnp, "Boolean") == 0 ||
                               strcmp(tnp, "bool") == 0)) {
                speed.talk_panel_ready = true;
            } else {
                logmsg("talkPanelShowing type is '%s' (static=%s); panel gate "
                       "DISABLED (falling back to the segment criterion)",
                       tnp ? tnp : "(null)", stp ? "y" : "n");
                speed.f_talk_panel = NULL;
            }
        }
        /* v1.1.12: battlePopupShowing —— 战斗内的弹窗/菜单（使用物品、技能
         * 选择等）标志，静态 bool。菜单状态机用它判断「菜单开着吗」。
         * 同 talkPanelShowing：静态 bool 三重校验，拿不到就不用。 */
        speed.f_battle_popup = i_class_field(speed.gs_klass, "battlePopupShowing");
        if (speed.f_battle_popup) {
            void *fbp = i_field_type(speed.f_battle_popup);
            const char *tnb = fbp ? i_type_name(fbp) : NULL;
            bool stb = (i_field_flags(speed.f_battle_popup) &
                        IL2CPP_FIELD_ATTRIBUTE_STATIC) != 0;
            if (stb && tnb && (strcmp(tnb, "System.Boolean") == 0 ||
                               strcmp(tnb, "Boolean") == 0 ||
                               strcmp(tnb, "bool") == 0)) {
                speed.battle_popup_ready = true;
            } else {
                logmsg("battlePopupShowing type is '%s' (static=%s); "
                       "battle-popup menu signal DISABLED",
                       tnb ? tnb : "(null)", stb ? "y" : "n");
                speed.f_battle_popup = NULL;
            }
        }
        if (!speed.m_is_dialogue && !speed.m_is_event)
            logmsg("GameSpeedCtrl found but dialogue signals unavailable; "
                   "dialogue backoff degrades to instance-anchored mode");
    } else {
        logmsg("GameSpeedCtrl not found; dialogue backoff degrades to instance-anchored mode");
    }

    /* v1.1.12: Global 单例通道。校验策略（全过才启用，否则退回旧行为）：
     *   1) 类按名找到，且有静态字段 _instance
     *   2) SetGameSpeed(float) 存在、非 static（实例方法）、方法指针可执行
     * 拿不到只影响大地图/菜单场景的写速，战斗内原通道不受影响。 */
    speed.global_klass = find_class("Global", global_validate);
    if (speed.global_klass) {
        speed.f_global_inst = i_class_field(speed.global_klass, "_instance");
        speed.m_global_set_speed = i_class_method(speed.global_klass, "SetGameSpeed", 1);
        if (speed.f_global_inst && speed.m_global_set_speed) {
            bool inst_static = (i_field_flags(speed.f_global_inst) &
                                IL2CPP_FIELD_ATTRIBUTE_STATIC) != 0;
            bool set_static = (i_method_flags(speed.m_global_set_speed, NULL) &
                               IL2CPP_METHOD_ATTRIBUTE_STATIC) != 0;
            void *gcode = method_code(speed.m_global_set_speed);
            if (inst_static && !set_static && gcode) {
                speed.global_ready = true;
            } else {
                logmsg("Global.SetGameSpeed unusable (_instance static=%s, "
                       "method static=%s, code=%p); big-map speed writes stay "
                       "on the remember-only path",
                       inst_static ? "y" : "n", set_static ? "y" : "n", gcode);
                speed.m_global_set_speed = NULL;
            }
        }
    }
    logmsg("Global channel: Global.SetGameSpeed=%s — %s",
           speed.global_ready ? "usable" : "unavailable",
           speed.global_ready
               ? "speed writes on the big map / menus no longer need StageManager"
               : "big map stays remember-only (v1.1.11 behaviour)");

    /* v1.1.13: 菜单信号诊断探针的判据解析（全部只读；任何失败只降级）。
     * 目标：给「战斗中开物品栏」找一个会翻转的信号。候选：
     *   Global.UIShow / StageManager.state / StageManager._optionPauseLckSt /
     *   GameSpeedCtrl.anyActionDepth / dialogueActionDepth / eventSpeedIntent。
     * 实测对比见 menu_probe_tick() 的 MENUPROBE 日志行。 */
    speed.state_off = speed.optpause_off = -1;
    if (speed.stage_klass) {
        void *fs = i_class_field(speed.stage_klass, "state");
        if (fs && !(i_field_flags(fs) & IL2CPP_FIELD_ATTRIBUTE_STATIC))
            speed.state_off = i_field_offset(fs);
        void *fop = i_class_field(speed.stage_klass, "_optionPauseLckSt");
        if (fop && !(i_field_flags(fop) & IL2CPP_FIELD_ATTRIBUTE_STATIC))
            speed.optpause_off = i_field_offset(fop);
        logmsg("MENUPROBE resolve: StageManager.state off=%d, "
               "_optionPauseLckSt off=%d",
               (int)speed.state_off, (int)speed.optpause_off);
    }
    if (speed.global_klass) {
        void *fu = i_class_field(speed.global_klass, "UIShow");
        if (fu) {
            bool st = (i_field_flags(fu) & IL2CPP_FIELD_ATTRIBUTE_STATIC) != 0;
            void *tu = i_field_type(fu);
            const char *tn = tu ? i_type_name(tu) : NULL;
            if (st && tn && (strstr(tn, "Boolean") || strstr(tn, "bool"))) {
                speed.f_uishow = fu;
                speed.uishow_size = 1;
            } else if (st && tn && strstr(tn, "Int32")) {
                speed.f_uishow = fu;
                speed.uishow_size = 4;
            }
            logmsg("MENUPROBE resolve: Global.UIShow type='%s' static=%s -> %s",
                   tn ? tn : "(null)", st ? "y" : "n",
                   speed.uishow_size == 1 ? "bool probe"
                   : speed.uishow_size == 4 ? "int32 probe" : "unavailable");
        }
    }
    if (speed.gs_klass) {
        struct { const char *name; void **slot; } gs_probe[] = {
            {"anyActionDepth",      &speed.f_any_depth},
            {"dialogueActionDepth", &speed.f_dlg_depth},
            {"eventSpeedIntent",    &speed.f_intent},
        };
        for (unsigned i = 0; i < 3; i++) {
            void *f = i_class_field(speed.gs_klass, gs_probe[i].name);
            if (!f) continue;
            bool st = (i_field_flags(f) & IL2CPP_FIELD_ATTRIBUTE_STATIC) != 0;
            void *t = i_field_type(f);
            const char *tn = t ? i_type_name(t) : NULL;
            if (st && tn && (strstr(tn, "Int32") || strstr(tn, "Boolean"))) {
                *gs_probe[i].slot = f;
                logmsg("MENUPROBE resolve: GameSpeedCtrl.%s type='%s' -> ok",
                       gs_probe[i].name, tn);
            } else {
                logmsg("MENUPROBE resolve: GameSpeedCtrl.%s type='%s' static=%s "
                       "-> skip", gs_probe[i].name, tn ? tn : "(null)",
                       st ? "y" : "n");
            }
        }
    }

    /* v1.1.9: NewUIDialogueTalk._curPageHasVoice —— 「这一页有没有配音」。
     *
     * 用户纠正的判据：有配音的剧情才恢复原速；对手战死这类无语音的战斗
     * 演出保持设定速度。v1.1.8 用的 segmentHasDialogue 是**会残留的 latch**
     * （本段演出只要出现过台词就一直为真），所以同一段里后面的无语音战斗
     * 节拍被误压 1x —— 正是「少数情况下对战过程原速度」。
     *
     * 离线取证链（见 meta/voice-criteria-research.md / voice-asm2.txt）：
     *   NewUIDialogueTalk.PlayTalk 是 async 状态机，第 9 参 voiceName；
     *   其 MoveNext 里 `call TableMgr.TryGetVoice` → `test eax,eax` →
     *   `jz <无配音分支>`，有配音才 `call Game$$get_Audio` 播放。
     *   UI 把每页的结果写成 _curPageHasVoice（实例 bool）。
     *
     * 校验策略（全部通过才启用，否则 fail-safe 退回 v1.1.8 行为）：
     *   1) 类按名找到
     *   2) _curPageHasVoice 存在、**非 static**、类型是 Boolean
     *   3) 偏移合法（>0 且不大）
     *   4) Update() 存在且 methodPointer 可执行（作为被动捕获的钩子目标）
     * 全程不加任何硬编码 RVA。 */
    speed.talk_klass = find_class("NewUIDialogueTalk", NULL);
    if (speed.talk_klass) {
        speed.f_page_has_voice = i_class_field(speed.talk_klass, "_curPageHasVoice");
        void *m_update = i_class_method(speed.talk_klass, "Update", 0);
        if (speed.f_page_has_voice && m_update) {
            void *ft = i_field_type(speed.f_page_has_voice);
            const char *tn = ft ? i_type_name(ft) : NULL;
            bool is_static = (i_field_flags(speed.f_page_has_voice) &
                              IL2CPP_FIELD_ATTRIBUTE_STATIC) != 0;
            int off = i_field_offset(speed.f_page_has_voice);
            bool is_bool = tn && (strcmp(tn, "System.Boolean") == 0 ||
                                  strcmp(tn, "Boolean") == 0 ||
                                  strcmp(tn, "bool") == 0);
            if (is_bool && !is_static && off > 0 && off < 0x10000) {
                speed.page_has_voice_off = off;
                /* v1.1.10 取证：把 _curPage* / Voice / Speaker 相关字段的真实偏移
                 * 打出来，用来核对「读的位置对不对」。声明序 61..64 应当连续
                 * 排布，因此它们的偏移也应当彼此接近。 */
                logmsg("  _curPageHasVoice resolved at off=0x%x (type=%s)",
                       (unsigned)off, tn);
                {
                    void *iter2 = NULL, *fl2;
                    int dumped2 = 0;
                    while (dumped2 < 24 && (fl2 = i_class_fields(speed.talk_klass, &iter2))) {
                        const char *fn2 = i_field_name(fl2);
                        if (!fn2) continue;
                        if (!strstr(fn2, "_curPage") && !strstr(fn2, "Voice") &&
                            !strstr(fn2, "Typing") && !strstr(fn2, "Speaker"))
                            continue;
                        {
                            int of2 = i_field_offset(fl2);
                            bool st2 = (i_field_flags(fl2) &
                                        IL2CPP_FIELD_ATTRIBUTE_STATIC) != 0;
                            void *ft2 = i_field_type(fl2);
                            const char *tn2 = ft2 ? i_type_name(ft2) : NULL;
                            logmsg("  Talk field %-26s off=0x%-5x %-22s%s",
                                   fn2, (unsigned)of2, tn2 ? tn2 : "?",
                                   st2 ? " static" : "");
                            dumped2++;
                        }
                    }
                }
                speed.talk_update_code = method_code(m_update);
                if (speed.talk_update_code) {
                    speed.talk_ready = true;
                } else {
                    logmsg("NewUIDialogueTalk.Update has no executable body; "
                           "voice criterion DISABLED (keeping v1.1.8 behaviour)");
                }
            } else {
                logmsg("NewUIDialogueTalk._curPageHasVoice is '%s' "
                       "(static=%s off=0x%x); voice criterion DISABLED "
                       "(keeping v1.1.8 behaviour)",
                       tn ? tn : "(null)", is_static ? "y" : "n", (unsigned)off);
            }
        } else {
            logmsg("NewUIDialogueTalk is missing %s; voice criterion DISABLED "
                   "(keeping v1.1.8 behaviour)",
                   !speed.f_page_has_voice ? "_curPageHasVoice" : "Update");
        }
    } else {
        logmsg("NewUIDialogueTalk not found; voice criterion DISABLED "
               "(keeping v1.1.8 behaviour)");
    }

    logmsg("Speed chain ready: StageManager klass=%p, SetBaseGameSpeed=%p, "
           "baseGameSpeed %s off=0x%x, instance via %s, LogicMgr via %s, "
           "GameSpeedCtrl signals=%s (dialogue=%s event=%s eventCtx=%s), "
           "grace=%.1fs, "
           "full-entry=%s (keypress only) sync=%s toast=DISABLED (v1.1.5, "
           "GodsetToast$$DrawGUI crashed the game; GDI OSD replaced it)",
           speed.stage_klass, speed.m_set_speed,
           speed.base_speed_static ? "STATIC" : "instance",
           (unsigned)speed.base_speed_off,
           m_get_stage_code ? "get_StageMgr (hooked)"
                            : speed.m_get_stage_mgr ? "get_StageMgr (invoke)"
                            : "static field",
           (speed.logic_get_inst && m_logic_get_inst_code) ? "singleton getter (hooked)"
                                                           : "FindObjectOfType",
           (speed.m_is_dialogue || speed.m_is_event) ? "on" : "off",
           speed.m_is_dialogue ? "y" : "n",
           speed.m_is_event ? "y" : "n",
           speed.m_is_event_ctx ? "y" : "n",
           speed.grace_seconds,
           speed.m_set_speed_full ? "SetGameSpeed" : "off",
           speed.m_sync_speed ? "SyncGameSpeed" : "off");
    /* v1.1.8: 单独一行报入场放行判据的可用性，便于实机排查。 */
    logmsg("Entrance-walk fast-forward: segmentHasDialogue=%s "
           "(%s) — %s", speed.seg_dialogue_ready ? "usable" : "unavailable",
           speed.f_seg_dialogue ? "field found" : "field missing",
           speed.seg_dialogue_ready
               ? "entrance walks follow the step; story walks stay at 1x"
               : "DISABLED (v1.1.7 behaviour: everything in an event stays at 1x)");
    /* v1.1.10: 闸门可用性单独一行 —— 这是本版修复的核心信号。 */
    logmsg("Talk-panel gate: GameSpeedCtrl.talkPanelShowing=%s (%s) — %s",
           speed.talk_panel_ready ? "usable" : "unavailable",
           speed.f_talk_panel ? "field found" : "field missing",
           speed.talk_panel_ready
               ? "1x hold only while the dialogue panel is ON SCREEN; walks, "
                 "deaths and entrances follow the step"
               : "DISABLED (falling back to the v1.1.8 segment criterion)");
    /* v1.1.9: 配音判据可用性单独一行。 */
    logmsg("Voice criterion: NewUIDialogueTalk._curPageHasVoice=%s (%s) — %s",
           speed.talk_ready ? "usable" : "unavailable",
           speed.talk_ready
               ? "class+field+hook resolved"
               : speed.talk_klass ? "class found, members/hook incomplete"
                                  : "class missing",
           speed.talk_ready
               ? "1x hold only when the current page carries a voice; "
                 "voiceless beats (e.g. the opponent's death) keep the step"
               : "DISABLED (v1.1.8 behaviour: any dialogue in the segment holds 1x)");
    return true;
}

/* Called from the input hook until it succeeds; bounded retries so a
 * transiently uninitialised runtime does not turn the mod off for good. */
static void speed_resolve_tick(void) {
    if (speed_ready) return;
    if (resolve_attempts >= 15) return;
    uint64_t now = GetTickCount64();
    if (resolve_attempts && now < resolve_next_try) return;
    resolve_next_try = now + 2000;
    resolve_attempts++;
    if (speed_resolve()) {
        speed_ready = true;
        install_speed_hooks();   /* v1.1.2: passive capture channels, main thread */
    } else if (resolve_attempts == 1) {
        logmsg("Speed chain not ready yet (retrying every 2s for up to 30s)");
    } else if (resolve_attempts >= 15) {
        logmsg("Speed chain could not be resolved after %d attempts; "
               "the mod stays passive and nothing is hooked for speed", resolve_attempts);
    }
}

/* ------------------------------------------------------------------ */
/* Speed primitives                                                    */
/* ------------------------------------------------------------------ */

/* Guard state lives with the primitives: both apply and guard share it. */
static int step_index;            /* current rung on config.steps */
static bool applied;              /* we have set a speed at least once */
static bool released;             /* game changed speed; guard yields */
static uint64_t released_at;      /* v1.1.6: when the yield started */
static void *last_mgr;            /* instance anchor for re-assert */
static bool dlg_seen;             /* dialogue was active last frame */
static bool grace_timing;         /* counting out the post-dialogue grace */
static uint64_t dlg_end_tick;

/* v1.1.6：游戏借走速度后的让路时长。官方 Ctrl 加速/事件仲裁是游戏自己
 * 的正当行为，让路是对的；但游戏若迟迟不归还（较长的演出、我们看不到
 * 标志的过场），档位会永久丢失——用户报的「非剧情突然变慢」。3 秒足够
 * 尊重正常借用，又不至于让玩家久等。 */
#define GUARD_YIELD_MS 3000

/* 用户需求（2026-09-20 实测拍板）：实际速度不允许低于 1x、不允许超过 10x。
 * 档位序列仍然由 speed_steps 配置，但任何对外生效的值都经过这里钳制。 */
static float current_target(void) {
    int idx = step_index;
    if (idx < 0) idx = 0;
    if (idx >= config.step_count) idx = config.step_count - 1;
    float v = config.steps[idx];
    if (v < 1.0f) v = 1.0f;
    if (v > 10.0f) v = 10.0f;
    return v;
}

/* IL2CPP MethodInfo* is NOT executable code: the compiled body lives at
 * MethodInfo->methodPointer (first field, stable across every IL2CPP
 * version this mod targets). v1.1.1 and earlier cast the MethodInfo itself
 * to a function pointer — a convention that had never executed far enough
 * on a real machine to be verified (the speed chain never ran). Validate
 * hard and fail closed: the pointer must be non-null, inside GameAssembly,
 * and on an executable page. */
static void *method_code(void *mi) {
    if (!mi) return NULL;
    void *code = PTR(mi, 0);
    if (!code) return NULL;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(code, &mbi, sizeof(mbi))) return NULL;
    if (mbi.AllocationBase != ga) return NULL;
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & exec)) return NULL;
    return code;
}

/* Get the StageManager instance — PASSIVE ONLY (v1.1.2).
 *
 * v1.1.1 proactively called the get_StageMgr method pointer here every
 * frame; with the Mod switch on, the first call happened during the game's
 * startup coroutine, triggered il2cpp_runtime_class_init inside the getter
 * and crashed the game (Player.log: d3d11 frame -> il2cpp_runtime_class_init
 * x2, 2026-09-20). This function now reads nothing but passively captured
 * state; the instance arrives through hk_get_stage (MinHook on the getter's
 * compiled code) whenever the game itself asks for it. */
static void *get_stage_mgr(void) {
    if (stage_cached && PTR(stage_cached, 0) == speed.stage_klass &&
        GetTickCount64() - stage_cached_at < 10000)
        return stage_cached;
    return NULL;
}

/* The ONE remaining proactive call, and it only runs where the runtime is
 * known to be mature: (a) the 120 s fallback arming probe in guard_tick —
 * minutes after the boot coroutine is gone; (b) an in-battle cache refresh
 * after a scene switch. Uses the REAL code pointer (methodPointer), not the
 * MethodInfo. If the getter is hooked, go through the trampoline so the
 * detour bookkeeping stays uniform. */
static void *stage_probe(void) {
    if (!speed.m_get_stage_mgr) {
        /* No getter at all (never seen on this build): last resort is the
         * typed static singleton field — same maturity requirements. */
        if (!speed.f_stage_singleton) return NULL;
        void *inst = NULL;
        i_field_static_get_value(speed.f_stage_singleton, &inst);
        if (inst && PTR(inst, 0) == speed.stage_klass) {
            stage_cached = inst;
            stage_cached_at = GetTickCount64();
            return inst;
        }
        return NULL;
    }
    void *inst = NULL;
    if (orig_get_stage) {
        inst = orig_get_stage(speed.m_get_stage_mgr);
    } else if (m_get_stage_code) {
        inst = ((void *(*)(void *))m_get_stage_code)(speed.m_get_stage_mgr);
    } else {
        /* No usable code pointer (or hook): the proper IL2CPP API instead. */
        void *exc = NULL;
        inst = i_runtime_invoke(speed.m_get_stage_mgr, NULL, NULL, &exc);
        if (exc) inst = NULL;
    }
    /* The facade search is name-based: verify the returned object really
     * is a StageManager before anyone dereferences it. */
    if (inst && PTR(inst, 0) == speed.stage_klass) {
        stage_cached = inst;
        stage_cached_at = GetTickCount64();
        return inst;
    }
    return NULL;
}

/* The one call that changes the speed.
 *
 * v1.1.3 semantics: send both official entries exactly like v1.1.2 (the
 * pattern the user confirmed working on the real machine), then ALWAYS run
 * SyncGameSpeed() — the game's own per-frame OnUpdate chain that feeds
 * Global.SetGameSpeed and the '速度設為{0}x' readout — and report success.
 *
 * The old field verify-read (baseGameSpeed == v) is gone as a gate: the
 * live v1.1.2 log showed it reading 0 on every keypress while the speed
 * demonstrably applied, and the offline probe (speed-osd.txt) shows
 * SetBaseGameSpeed arbitrates through GameSpeedCtrl/the Global singleton,
 * so the sampled field is not a reliable witness of the write. The read
 * values are now a one-line diagnostics record; a persistent mismatch is
 * logged (so the field dump in speed_resolve can be compared against it)
 * but nothing is blocked on it. */
static float read_base_speed(void *mgr) {
    /* v1.1.4: baseGameSpeed is a STATIC field — the value lives in the
     * class's static storage, so the only correct read goes through
     * il2cpp_field_static_get_value (the same API already used for
     * f_grace). An instance-object read at the same offset hits unrelated
     * memory: exactly the garbage that drove the v1.1.3 guard mad. */
    if (speed.base_speed_static) {
        float v = 0.0f;
        if (i_field_static_get_value && speed.f_base_speed)
            i_field_static_get_value(speed.f_base_speed, &v);
        return v;                       /* API missing -> 0: guard ignores 0 */
    }
    return mgr ? VAL(float, mgr, speed.base_speed_off) : 0.0f;
}

static bool apply_speed(void *mgr, float v, bool with_full) {
    if (!mgr || !speed.m_set_speed) return false;
    float before = read_base_speed(mgr);
    /* v1.1.4: the full SetGameSpeed(float) entry runs ONLY on a player
     * keypress. Its only official caller is the scene-load coroutine
     * (speed-osd.txt call graph), so hammering it from the per-frame
     * guard is off-spec and was the prime crash suspect in v1.1.3
     * (c0000005 inside the il2cpp allocator/class-init region). Guard
     * paths use SetBaseGameSpeed + SyncGameSpeed — both are executed by
     * the game's own OnUpdate every frame, proven-safe targets.
     * v1.1.2: call through MethodInfo->methodPointer, never through the
     * MethodInfo itself. Both entries are instance methods (metadata
     * flags 0x0086 — see speed-osd.txt), so (this, float, MethodInfo*)
     * is right. */
    if (with_full && speed.m_set_speed_full) {
        void *full = method_code(speed.m_set_speed_full);
        if (full)
            ((void (*)(void *, float, void *))full)(mgr, v, speed.m_set_speed_full);
    }
    float after_full = read_base_speed(mgr);
    void *base = method_code(speed.m_set_speed);
    if (!base) {
        logmsg("SetBaseGameSpeed has no usable code pointer; speed write skipped");
        return false;
    }
    ((void (*)(void *, float, void *))base)(mgr, v, speed.m_set_speed);
    float after_base = read_base_speed(mgr);
    if (after_base < v - 0.001f || after_base > v + 0.001f) {
        /* Throttled: guard paths may legally re-apply while the game holds
         * a different value, and this line must not flood the log. */
        static uint64_t last_mismatch;
        uint64_t now = GetTickCount64();
        if (now - last_mismatch > 1500) {
            last_mismatch = now;
            logmsg("Speed -> %.10gx sent (baseGameSpeed before=%.10g "
                   "after-full=%.10g after-base=%.10g)",
                   (double)v, (double)before, (double)after_full,
                   (double)after_base);
        }
    }
    /* Official OSD chain — the same SyncGameSpeed() call the game's own
     * StageManager.OnUpdate makes every frame (and GameSpeedCtrl.MarkShowEnded
     * on show end): it pushes the effective speed into Global.SetGameSpeed
     * and refreshes the in-game readout, exactly like the Ctrl speedup. */
    if (speed.m_sync_speed) {
        void *sync = method_code(speed.m_sync_speed);
        if (sync)
            ((void (*)(void *, void *))sync)(mgr, speed.m_sync_speed);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* v1.1.12: Global 单例通道 —— 无 StageManager 场景的官方写速入口        */
/* ------------------------------------------------------------------ */

static void *global_cached;      /* last validated Global instance */
static uint64_t global_cached_at;

/* Read Global._instance (the exact read the game's own get_Instance makes),
 * validate the klass header and cache with the same 10 s TTL as the
 * StageManager cache. Static-field read only: no hook, no proactive
 * object-model call, safe on the game thread at any time. */
static void *get_global(void) {
    if (global_cached && PTR(global_cached, 0) == speed.global_klass &&
        GetTickCount64() - global_cached_at < 10000)
        return global_cached;
    if (!i_field_static_get_value || !speed.f_global_inst) return NULL;
    void *inst = NULL;
    i_field_static_get_value(speed.f_global_inst, &inst);
    if (inst && PTR(inst, 0) == speed.global_klass) {
        global_cached = inst;
        global_cached_at = GetTickCount64();
        return inst;
    }
    return NULL;
}

/* Global.SetGameSpeed(float) through MethodInfo->methodPointer — the same
 * calling convention as every other direct call in this mod. This is the
 * game's own speed arbiter (SetBaseGameSpeed forwards into it; the GM
 * command uses it), so no SyncGameSpeed companion is needed. */
static bool apply_speed_global(float v) {
    if (!speed.global_ready) return false;
    void *inst = get_global();
    if (!inst) return false;
    void *code = method_code(speed.m_global_set_speed);
    if (!code) return false;
    ((void (*)(void *, float, void *))code)(inst, v, speed.m_global_set_speed);
    return true;
}

/* Unified write: prefer the battle channel (instance-anchored, full entry on
 * keypresses); fall back to the Global channel when no StageManager exists
 * (big map, scene transitions). */
static bool apply_speed_any(void *mgr, float v, bool with_full) {
    if (mgr) return apply_speed(mgr, v, with_full);
    return apply_speed_global(v);
}

/* ------------------------------------------------------------------ */
/* OSD: the game's own GodsetToast.Show(string, ...)                   */
/* ------------------------------------------------------------------ */

/* Il2CppType layout (x64): { void* data; unsigned bits; } with the type enum
 * in bits[16..24). We only branch on the two shapes Show's second parameter
 * can take; anything else disables the toast instead of guessing. */
#define IL2CPP_TYPE_R4        0x0C
#define IL2CPP_TYPE_VALUETYPE 0x11

static bool toast_broken;

static void type_enum_and_data(void *t, int *tenum, void **tdata) {
    *tenum = -1;
    *tdata = NULL;
    if (!t) return;
    *tenum = (int)((VAL(unsigned, t, 8) >> 16) & 0xFF);
    *tdata = PTR(t, 0);
}

/* Show the current step like the game's own GM toast ("速度 x5"). Failures
 * are logged once and then the OSD silently retires — the speed feature
 * itself never depends on it.
 *
 * v1.1.5: RETIRED — never called anywhere. Two live crashes (v1.1.3 and
 * v1.1.4, identical WER bucket GameAssembly+0x56992e) landed inside the
 * game's own GodsetToast$$DrawGUI while it rendered the entry our
 * runtime_invoke'd Show() had queued. Kept for reference only; the
 * unused attribute keeps -Wextra quiet. */
__attribute__((unused))
static void toast_show(float v) {
    if (!speed.m_toast || toast_broken) return;
    int tenum = -1; void *tdata = NULL;
    type_enum_and_data(i_method_get_param(speed.m_toast, 1), &tenum, &tdata);
    char text[48];
    snprintf(text, sizeof(text), "速度 x%g", (double)v);
    void *str = i_string_new(text);
    if (!str) { toast_broken = true; return; }
    float dur = 2.0f;
    /* Nullable<float> parameter slot: { int hasValue; float value; } */
    unsigned char nullable[8] = { 1, 0, 0, 0, 0, 0, 0, 0 };
    void *arg2;
    if (tenum == IL2CPP_TYPE_R4) {
        arg2 = &dur;
    } else if (tenum == IL2CPP_TYPE_VALUETYPE && tdata) {
        const char *nm = i_class_name(tdata);
        if (!nm || strcmp(nm, "Nullable`1") != 0) {
            logmsg("GodsetToast.Show 2nd param is %s; OSD text disabled", nm ? nm : "?");
            toast_broken = true;
            return;
        }
        memcpy(nullable + 4, &dur, 4);
        arg2 = nullable;
    } else {
        logmsg("GodsetToast.Show 2nd param type enum 0x%X; OSD text disabled", tenum);
        toast_broken = true;
        return;
    }
    void *args[2] = { &str, arg2 };
    void *exc = NULL;
    i_runtime_invoke(speed.m_toast, NULL, args, &exc);
    if (exc) {
        void *ek = PTR(exc, 0);
        logmsg("GodsetToast.Show threw %s; OSD text disabled",
               ek ? i_class_name(ek) : "an exception");
        toast_broken = true;
    }
}

/* The LogicMgr instance, acquired from the scene through
 * UnityEngine.Object.FindObjectOfType(LogicMgr). The metadata has no static
 * field of this type anywhere, so the scene is the only source. The result
 * is cached and re-validated by its klass header every frame; a fresh scene
 * lookup only happens when the cache fails.
 *
 * v1.1.0: il2cpp_runtime_invoke takes params[i] = POINTER TO the argument
 * slot — for a reference-type parameter that is &pointer, not the pointer.
 * The old code passed the System.Type object itself, so the invoke
 * dereferenced it as a bogus argument and threw on every call (the user's
 * v1.0.1 log). Retries are capped: 20 fast tries at 5 s, then one try per
 * 60 s so a broken chain degrades quietly instead of spamming the log.
 *
 * v1.1.1 arming gate: the probe must not fire during the game's startup
 * coroutine / main-menu phase. v1.1.0's first live FindObjectOfType crashed
 * the game inside il2cpp_runtime_class_init exactly there (Player.log
 * stack, 2026-09-20) — in v1.0.1 the invoke always threw before the engine
 * call executed, so this code had never really run before. The probe is
 * armed only after guard_tick sees a live StageManager (real scene, mature
 * runtime) and then waits 5 more seconds for the scene to settle. */
static bool logic_hunt_armed;
static uint64_t logic_hunt_at;      /* first probe no sooner than this */

/* ------------------------------------------------------------------ */
/* v1.1.2 passive capture: MinHook detours on the game's own accessors */
/* ------------------------------------------------------------------ */

/* Detour ABI note: the hooked target is the COMPILED body of a static,
 * zero-argument method. The IL2CPP calling convention appends the
 * MethodInfo* as the trailing argument, so for a 0-arg static the whole
 * signature is f(MethodInfo* mi) — one pointer in RCX. The detour mirrors
 * it exactly and adds nothing else: no IL2CPP API calls, no allocation —
 * just a validated pointer cache and a log line. */

static void *hk_get_stage(void *mi) {
    void *inst = orig_get_stage(mi);
    if (inst && PTR(inst, 0) == speed.stage_klass) {
        if (!stage_cached)
            logmsg("StageManager instance captured from the game's own "
                   "get_StageMgr call (%p)", inst);
        stage_cached = inst;
        stage_cached_at = GetTickCount64();
        /* Arming signal: the game itself reached a live StageManager, so
         * the runtime is mature and a real scene is up — the exact premise
         * v1.1.1 formalised, now observed instead of probed. */
        if (!logic_hunt_armed) {
            logic_hunt_armed = true;
            logic_hunt_at = stage_cached_at + 5000;   /* let the scene settle */
            logmsg("StageManager seen via game call; LogicMgr probe armed (first try in 5 s)");
        }
    }
    return inst;
}

static void *hk_logic_get_inst(void *mi) {
    void *inst = orig_logic_get_inst(mi);
    if (inst && PTR(inst, 0) == speed.logic_klass) {
        if (speed.logic_cached != inst)
            logmsg("LogicMgr instance captured from the game's own "
                   "Singleton get_Instance call (%p)", inst);
        speed.logic_cached = inst;
        speed.logic_cached_at = GetTickCount64();
        if (!logic_hunt_armed) {
            logic_hunt_armed = true;
            logic_hunt_at = speed.logic_cached_at + 5000;
            logmsg("LogicMgr singleton seen via game call; probe armed (first try in 5 s)");
        }
    }
    return inst;
}

/* v1.1.9 detour on NewUIDialogueTalk.Update — the passive capture channel
 * for the talk UI instance.
 *
 * ABI note: Update() is an INSTANCE method with zero parameters, so IL2CPP
 * passes (this, MethodInfo*) — `this` in RCX, the MethodInfo* in RDX. The
 * detour mirrors that signature exactly and, per the startup-window rule at
 * the top of this file, does NOTHING but validate the pointer against the
 * expected class and cache it: no IL2CPP API call, no allocation, no string
 * work. The game itself calls Update every frame while the talk UI exists,
 * so the cache is fresh whenever it matters. */
static void hk_talk_update(void *self, void *mi) {
    if (self && PTR(self, 0) == speed.talk_klass) {
        if (speed.talk_cached != self)
            logmsg("NewUIDialogueTalk instance captured from its own Update (%p)", self);
        speed.talk_cached = self;
        speed.talk_cached_at = GetTickCount64();
        /* v1.1.10 诊断（只读，verbose_log 打开才生效）：
         * 每秒采样一次 instance 字段，用来判断 _curPageHasVoice 到底是
         * 「随页变化」还是「恒为 1 的残留」。同时打印 Update 调用频率，
         * 这直接决定「面板在不在场」能否用 Update 是否被调用来判定。 */
        if (config.verbose_log) {
            static uint64_t next_sample;
            uint64_t now = GetTickCount64();
            if (now >= next_sample) {
                next_sample = now + 1000;
                unsigned char *p = (unsigned char *)self + speed.page_has_voice_off;
                logmsg("TALKPROBE inst=%p off=0x%x hasVoice=%d "
                       "bytes[-4..+4]=%02x %02x %02x %02x [%02x] %02x %02x %02x %02x",
                       self, (unsigned)speed.page_has_voice_off,
                       (int)VAL(uint8_t, self, speed.page_has_voice_off),
                       p[-4], p[-3], p[-2], p[-1], p[0], p[1], p[2], p[3], p[4]);
            }
        }
    }
    orig_talk_update(self, mi);
}

/* v1.1.14 诊断：Global.SetGameSpeed 写入者追踪。
 * 全游戏写速都过这个总闸（SetBaseGameSpeed 内部转发、GM 命令同款），钩住它
 * 就能看到「谁在什么值上写速度」。调用方地址用 __builtin_return_address(0)
 * 取，换算成模块内偏移（GameAssembly+0x… / mod-self+0x…），离线用元数据
 * RVA 表反查归属。限频：值变化立即记；同值重复压回只计数，每秒最多补一条。
 * 纯转发 + 整数/浮点比较，零分配，满足启动窗口铁律。 */
static void hk_global_set_speed(void *self_, float v, void *mi) {
    void *ret = __builtin_return_address(0);
    uint64_t now = GetTickCount64();
    static float last_v = -1.0f;
    static uint64_t last_t;
    static uint64_t suppressed;
    bool changed = (v != last_v) || (now - last_t > 1000);
    if (changed && last_v != -1.0f && suppressed) {
        logmsg("GSET ... (%llu repeat writes suppressed)", suppressed);
        suppressed = 0;
    }
    if (changed) {
        uintptr_t ra = (uintptr_t)ret;
        uintptr_t gbase = (uintptr_t)ga, sbase = (uintptr_t)self;
        const char *who = "unknown";
        uintptr_t off = 0;
        if (gbase && ra >= gbase && ra < gbase + 0x8000000ull) {
            who = "GameAssembly"; off = ra - gbase;
        } else if (sbase && ra >= sbase && ra < sbase + 0x100000ull) {
            who = "mod-self"; off = ra - sbase;
        }
        logmsg("GSET v=%.10gx caller=%s+0x%llX self=%p", (double)v, who,
               (unsigned long long)off, self_);
        last_v = v;
        last_t = now;
    } else {
        suppressed++;
    }
    orig_global_set_speed(self_, v, mi);
}

/* Fresh NewUIDialogueTalk instance, or NULL. Same 10 s freshness window as
 * the StageManager/LogicMgr caches: a destroyed UI object must not survive a
 * scene transition. Passively captured only — never probed. */
static void *talk_instance(void) {
    if (!speed.talk_ready || !speed.talk_cached) return NULL;
    if (PTR(speed.talk_cached, 0) != speed.talk_klass) return NULL;
    if (GetTickCount64() - speed.talk_cached_at > 10000) return NULL;
    return speed.talk_cached;
}

/* 「当前这一页有没有配音」。返回三态：
 *   1 = 有配音（恢复原速）
 *   0 = 没有配音（保持设定档次）
 *  -1 = 判据不可用（调用方退回 v1.1.8 行为）
 * 只读实例字段，不做任何方法调用 / 分配 —— 守卫是每帧路径。 */
static int current_page_has_voice(void) {
    if (!speed.talk_ready) return -1;
    void *inst = talk_instance();
    if (!inst) return -1;          /* 台词面板不在场：交给上层判据决定 */
    return VAL(uint8_t, inst, speed.page_has_voice_off) ? 1 : 0;
}

/* Called once from the main thread right after the speed chain resolves
 * (~ the first input tick, seconds after boot — long before any battle).
 * MH_CreateHook/MH_EnableHook from the main thread is standard MinHook
 * usage; it briefly suspends the other threads while patching. Failures
 * degrade to the 120 s fallback probe instead of aborting the mod. */
static void install_speed_hooks(void) {
    if (m_get_stage_code && !stage_hook_done && !stage_hook_failed) {
        MH_STATUS s = MH_CreateHook(m_get_stage_code, hk_get_stage,
                                    (void **)&orig_get_stage);
        if (s == MH_OK) s = MH_EnableHook(m_get_stage_code);
        if (s == MH_OK) {
            stage_hook_done = true;
            logmsg("get_StageMgr hooked: StageManager arrives passively with the game's own calls");
        } else {
            stage_hook_failed = true;
            logmsg("get_StageMgr hook failed (status=%d); arming falls back to the 120 s probe", s);
        }
    }
    if (m_logic_get_inst_code && !logic_hook_done && !logic_hook_failed) {
        MH_STATUS s = MH_CreateHook(m_logic_get_inst_code, hk_logic_get_inst,
                                    (void **)&orig_logic_get_inst);
        if (s == MH_OK) s = MH_EnableHook(m_logic_get_inst_code);
        if (s == MH_OK) {
            logic_hook_done = true;
            logmsg("Singleton`1<LogicMgr>.get_Instance hooked: LogicMgr arrives passively");
        } else {
            logic_hook_failed = true;
            logmsg("get_Instance hook failed (status=%d); FindObjectOfType stays the fallback", s);
        }
    }
    /* v1.1.9: the talk UI instance arrives through its own Update. */
    if (speed.talk_update_code && !talk_hook_done && !talk_hook_failed) {
        MH_STATUS s = MH_CreateHook(speed.talk_update_code, hk_talk_update,
                                    (void **)&orig_talk_update);
        if (s == MH_OK) s = MH_EnableHook(speed.talk_update_code);
        if (s == MH_OK) {
            talk_hook_done = true;
            logmsg("NewUIDialogueTalk.Update hooked: the talk UI instance "
                   "arrives passively (voice criterion live)");
        } else {
            talk_hook_failed = true;
            speed.talk_ready = false;
            logmsg("NewUIDialogueTalk.Update hook failed (status=%d); voice "
                   "criterion DISABLED (v1.1.8 behaviour)", s);
        }
    }
    /* v1.1.14 诊断：Global.SetGameSpeed 写入者追踪（纯观察，只记日志）。 */
    if (speed.global_ready && !gset_hook_done && !gset_hook_failed) {
        void *gcode = method_code(speed.m_global_set_speed);
        if (gcode) {
            MH_STATUS s = MH_CreateHook(gcode, hk_global_set_speed,
                                        (void **)&orig_global_set_speed);
            if (s == MH_OK) s = MH_EnableHook(gcode);
            if (s == MH_OK) {
                gset_hook_done = true;
                logmsg("GSET tracer: Global.SetGameSpeed hooked (write/caller logging live)");
            } else {
                gset_hook_failed = true;
                logmsg("GSET tracer: hook failed (status=%d); diagnosis degrades", s);
            }
        }
    }
}

static void *logic_mgr_instance(void) {
    if (!speed.logic_klass || !logic_hunt_armed) return NULL;
    /* Primary: passively captured by hk_logic_get_inst. Freshness window
     * keeps a destroyed scene object from being used across a transition —
     * in battle the game calls the getter every frame, so the cache is
     * effectively always fresh; the window only covers menu time. */
    if (speed.logic_cached && PTR(speed.logic_cached, 0) == speed.logic_klass &&
        GetTickCount64() - speed.logic_cached_at < 10000)
        return speed.logic_cached;
    if (!speed.m_foaf) return NULL;
    uint64_t now = GetTickCount64();
    if (now < logic_hunt_at) return NULL;   /* scene settling after arming */
    if (speed.logic_tried && now < speed.logic_retry_at) return NULL;
    if (!speed.logic_tried)
        logmsg("LogicMgr probe started (FindObjectOfType)");
    speed.logic_tried = true;
    /* fast phase: 5 s apart; after 20 failures: 60 s apart */
    speed.logic_retry_at = now + (speed.logic_fail_count < 20 ? 5000 : 60000);
    speed.logic_fail_count++;
    bool loud = speed.logic_fail_count <= 3 || speed.logic_fail_count % 10 == 0;
    void *type = i_class_get_type(speed.logic_klass);
    if (!type) return NULL;
    void *rt = i_type_get_object(type);
    if (!rt) return NULL;
    void *args[1] = { &rt };            /* &slot — NOT the object itself */
    void *exc = NULL;
    void *obj = i_runtime_invoke(speed.m_foaf, NULL, args, &exc);
    if (exc) {
        void *ek = PTR(exc, 0);
        if (loud)
            logmsg("FindObjectOfType attempt %d threw %s; %s", speed.logic_fail_count,
                   ek ? i_class_name(ek) : "an exception",
                   speed.logic_fail_count < 20
                       ? "will retry every 5 s"
                       : "will keep trying every 60 s");
        return NULL;
    }
    if (!obj) {
        /* LogicMgr not in the scene (yet). v1.1.0 kept this silent, which
         * made a crash look like a freeze in the user's log. */
        if (loud)
            logmsg("FindObjectOfType attempt %d: no LogicMgr in the scene yet; %s",
                   speed.logic_fail_count,
                   speed.logic_fail_count < 20
                       ? "retry every 5 s"
                       : "keep trying every 60 s");
        return NULL;
    }
    /* Fail-closed: the object must really be a LogicMgr. */
    if (PTR(obj, 0) != speed.logic_klass) {
        logmsg("FindObjectOfType returned a non-LogicMgr object; ignored");
        return NULL;
    }
    speed.logic_cached = obj;
    speed.logic_cached_at = GetTickCount64();
    logmsg("LogicMgr instance acquired via FindObjectOfType (%p) after %d failed attempt(s)",
           obj, speed.logic_fail_count);
    return obj;
}

/* Range gate: only while a fight (ScenarioLogic) or the big map (BigMapLogic)
 * is the current logic. Main menu / loading / cutscene-only screens stay
 * untouched, which also debounces keys during transitions. */
static void *cached_logic_klass;
static bool cached_logic_in_scope;
/* v1.1.12: 大地图菜单判据 —— 当前 logic 实例与 BigMapLogic 专用信号方法。 */
static void *cached_logic_inst;
static bool cached_logic_is_bigmap;
static void *m_is_other_ui;      /* BigMapLogic.IsOtherUIVisible() */
static bool in_scope(void) {
    void *manager = logic_mgr_instance();
    if (!manager) return false;
    void *logic = FN(0x669160, void *, void *, void *)(manager, NULL);
    if (!logic) return false;
    void *k = PTR(logic, 0);
    cached_logic_inst = logic;
    if (k == cached_logic_klass) return cached_logic_in_scope;
    const char *nm = i_class_name(k);
    cached_logic_in_scope = nm && (!strcmp(nm, "ScenarioLogic") || !strcmp(nm, "BigMapLogic"));
    cached_logic_klass = k;
    cached_logic_is_bigmap = nm && !strcmp(nm, "BigMapLogic");
    /* 只在首次确认 BigMapLogic 时解析一次（按名解析，无硬编码 RVA）。 */
    if (cached_logic_is_bigmap && !m_is_other_ui)
        m_is_other_ui = i_class_method(k, "IsOtherUIVisible", 0);
    if (config.verbose_log && cached_logic_in_scope)
        logmsg("Range gate: current logic is %s", nm);
    return cached_logic_in_scope;
}

/* Invoke a bool-returning method the way its flags demand: static methods
 * take no this (GameSpeedCtrl in build 25293526 is a static class).
 * v1.1.2: through MethodInfo->methodPointer, like every direct call. */
static bool call_bool(void *method, void *inst) {
    if (!method) return false;
    void *code = method_code(method);
    if (!code) return false;
    bool is_static = i_method_flags(method, NULL) & IL2CPP_METHOD_ATTRIBUTE_STATIC;
    return ((bool (*)(void *, void *))code)(is_static ? NULL : inst, method);
}

/* 剧情/演出活动判定，取游戏自己的仲裁结果。
 *
 * v1.1.7：用户报「剧情中人物移动按设定档位跑，只有说话是原速」——
 * 说明单看 IsDialogueNow/IsEventShowingNow 不够：剧情演出里「人物移动」
 * 期间这两个信号可能都为假（只有台词面板弹出来时为真），于是守卫继续
 * 维持设定档位，移动就被加速了。
 *
 * 离线取证确认 GameSpeedCtrl.get_IsEventContext 是更宽的判据：
 *   IsEventContext = IsEventShowingNow() || (刚结束演出不久)
 * 它自身就会调用 IsEventShowingNow，另加时间窗口检查
 * （lastEventShowingAt + EventShowGraceSeconds），即「整段剧情区间」。
 *
 * 现在我们三层取或：对话 / 演出中 / 演出上下文。
 * 返回时通过 *evt_ctx 告知调用方命中的是不是「宽判据」，便于日志区分。
 *
 * v1.1.8（用户报「人物从地图外入场，入场走路还是慢的，需要跟设置一致」）：
 * 上面这个「更宽」正是问题所在 —— 入场走位本身也是一个 event，会命中
 * IsEventContext 而被压回 1x。区分办法用游戏自己的标志位
 * segmentHasDialogue：「本段演出出现过台词吗」。
 *   纯入场走位 → 全程无台词 → segmentHasDialogue 假 → 不锁 1x，跟随设定档位
 *   剧情演出   → 有台词        → 真             → 整段锁 1x（v1.1.7 行为不变）
 * 通过 *dialogue_seen 把「段内是否有台词」回传给调用方做这个决策。 */
static bool gs_bool_field(void *field) {
    /* 读一个静态 bool 字段；任何一步不满足都返回 false（fail-safe）。 */
    if (!field || !i_field_static_get_value) return false;
    uint8_t v = 0;
    i_field_static_get_value(field, &v);
    return v != 0;
}

/* v1.1.13: 读一个静态 int32 字段（诊断探针用，4 字节直读）。 */
static int gs_int_field(void *field) {
    if (!field || !i_field_static_get_value) return -1;
    int32_t v = 0;
    i_field_static_get_value(field, &v);
    return (int)v;
}

/* v1.1.12: 菜单/弹窗开着吗（用户拍板 2026-09-20：菜单期间保持 1x，
 * 关闭后立即恢复档位）。
 *
 * 两个信号源，任一为真即菜单开着：
 *   1) GameSpeedCtrl.battlePopupShowing（静态 bool 直读）
 *      —— 战斗内的弹窗/菜单（使用物品、技能选择等）
 *   2) BigMapLogic.IsOtherUIVisible()（实例方法，经 call_bool）
 *      —— 大地图上非地图 UI（物品栏、商店、系统菜单等）可见
 *
 * 任一信号不可用只影响对应场景，互不阻塞。全程零分配、零主动
 * IL2CPP 对象模型调用之外的常规游戏内读取，与既有判据同级安全。 */
static bool menu_open_now(void) {
    if (speed.battle_popup_ready && gs_bool_field(speed.f_battle_popup))
        return true;
    if (cached_logic_is_bigmap && cached_logic_inst && m_is_other_ui)
        return call_bool(m_is_other_ui, cached_logic_inst);
    return false;
}

/* v1.1.13: 菜单信号诊断探针（用户拍板方案 A）。每 500ms 一行 MENUPROBE，
 * 汇总全部候选信号值；玩家开一次物品栏，哪个信号翻转一目了然。
 * 全部只读（静态字段直读 / 实例字段偏移直读 / 既有 call_bool 模式），
 * 不写任何游戏状态、不装新钩子。 */
static void menu_probe_tick(void) {
    static uint64_t next;
    uint64_t now = GetTickCount64();
    if (now < next) return;
    next = now + 500;
    if (!config.verbose_log || !in_scope()) return;

    void *mgr = get_stage_mgr();          /* passive only */
    int uishow = -1;
    if (speed.f_uishow && i_field_static_get_value) {
        uint32_t raw = 0;
        i_field_static_get_value(speed.f_uishow, &raw);
        uishow = (int)(raw & 0xFF);       /* bool 只取低字节；int32 取低 8 位也够判翻转 */
        if (speed.uishow_size == 4 && (raw & 0xFFFFFF00))
            uishow = (int)raw;            /* int32 有高位信息时保留全值 */
    }
    int st_val = (mgr && speed.state_off >= 0) ? (int)VAL(int, mgr, speed.state_off) : -1;
    int op_val  = (mgr && speed.optpause_off >= 0) ? (int)VAL(int, mgr, speed.optpause_off) : -1;
    float actual = -1.0f;
    if (mgr) actual = read_base_speed(mgr);
    else if (i_field_static_get_value && speed.f_base_speed) {
        /* 大地图上没有实例：静态字段照样能读（v1.1.4 语义）。 */
        float v = 0;
        i_field_static_get_value(speed.f_base_speed, &v);
        actual = v;
    }
    int other_ui = -1;
    if (cached_logic_is_bigmap && cached_logic_inst && m_is_other_ui)
        other_ui = call_bool(m_is_other_ui, cached_logic_inst) ? 1 : 0;
    logmsg("MENUPROBE actual=%.10g uishow=%d state=%d optpause=%d "
           "anyD=%d dlgD=%d intent=%d popup=%d talk=%d otherUI=%d",
           actual, uishow, st_val, op_val,
           gs_int_field(speed.f_any_depth), gs_int_field(speed.f_dlg_depth),
           gs_int_field(speed.f_intent),
           speed.battle_popup_ready ? (int)gs_bool_field(speed.f_battle_popup) : -1,
           speed.talk_panel_ready ? (int)gs_bool_field(speed.f_talk_panel) : -1,
           other_ui);
}

static bool dialogue_active_ex(bool *evt_ctx) {
    if (evt_ctx) *evt_ctx = false;
    if (!speed.gs_klass) return false;
    if (call_bool(speed.m_is_dialogue, speed.gs_instance)) return true;
    if (call_bool(speed.m_is_event, speed.gs_instance)) return true;
    if (call_bool(speed.m_is_event_ctx, speed.gs_instance)) {
        if (evt_ctx) *evt_ctx = true;
        return true;
    }
    return false;
}

/* v1.1.8: 入场走位放行判定。
 * 只有在「命中的是宽判据（非台词面板本身）」且「本段演出从未出现过台词」
 * 时才返回 true —— 此时不把速度压回 1x，让它跟随玩家设定档位。
 * segmentHasDialogue 没解析成功时恒返回 false，即完整保留 v1.1.7 行为。 */
static bool entrance_walk_fastforward(bool evt_ctx) {
    if (!evt_ctx) return false;            /* 真·对话/演出中：绝不放行 */
    if (!speed.seg_dialogue_ready) return false;   /* 没拿到判据：退回旧行为 */
    return !gs_bool_field(speed.f_seg_dialogue);
}

/* ------------------------------------------------------------------ */
/* Speed guard: keep the chosen step across scenes, yield to dialogue  */
/* ------------------------------------------------------------------ */

/* v1.1.6: forward declarations — the OSD implementation block sits far
 * below (after the hooks section) so keep_speed()/step_apply() can call
 * it without an implicit-declaration error under C99. */
static void osd_show(float v);
static void osd_tick(void);

/* v1.1.4 fuse: guard-driven applies are rate-limited to 1 Hz. With the
 * field read fixed (static API) the decision input is real, but if any
 * future layout surprise feeds the guard garbage again, this caps the
 * worst case at one write per second instead of the per-frame storm that
 * crashed v1.1.3. Player keypresses bypass the fuse. */
static bool guard_set_gate(void) {
    static uint64_t last;
    uint64_t now = GetTickCount64();
    if (now - last < 1000) return false;
    last = now;
    return true;
}

static void guard_tick(void) {
    if (!enabled) {
        /* 需求拍板：关闭 Mod 开关恢复 1x 原速。 */
        if (applied) {
            void *mgr = get_stage_mgr();   /* passive only — never probes */
            if (mgr && apply_speed(mgr, 1.0f, false))
                logmsg("Mod switched off; speed restored to 1x");
            applied = false;
        }
        released = false; dlg_seen = false; grace_timing = false;
        last_mgr = NULL;
        return;
    }
    /* v1.1.2: arming is now driven by the game's own accessor calls
     * (hk_get_stage / hk_logic_get_inst). guard_tick makes ZERO proactive
     * IL2CPP object-model calls during the startup window — that is what
     * crashed the game in v1.1.1 (mod on) and v1.1.0. If the game has not
     * touched the getter within 120 s of a ready speed chain (e.g. the mod
     * was enabled while the game sits in a screen that never asks for a
     * StageManager), ONE cautious direct probe runs; the boot-coroutine
     * danger window is minutes gone by then. */
    if (!logic_hunt_armed) {
        uint64_t now = GetTickCount64();
        if (!arm_probe_next) arm_probe_next = now + 120000;
        if (now >= arm_probe_next) {
            arm_probe_next = now + 120000;
            logmsg("Arming fallback: direct get_StageMgr probe (the game has not called it yet)");
            if (stage_probe()) {
                logic_hunt_armed = true;
                logic_hunt_at = GetTickCount64() + 5000;
                logmsg("StageManager seen via fallback probe; LogicMgr probe armed (first try in 5 s)");
            }
        }
        last_mgr = NULL;
        return;
    }
    /* Range gate first: the battle/big-map check only needs the LogicMgr
     * side, and everything below it runs in a mature, in-battle runtime. */
    if (!in_scope()) {
        last_mgr = NULL;
        return;
    }
    void *mgr = get_stage_mgr();
    if (!mgr) {
        /* In scope but the passive cache went stale (scene transition):
         * one throttled refresh. In battle the game itself calls
         * get_StageMgr constantly, so this almost never fires. */
        static uint64_t probe_next;
        uint64_t now = GetTickCount64();
        if (now >= probe_next) {
            probe_next = now + 1000;
            mgr = stage_probe();
        }
    }
    /* v1.1.12: 没有 StageManager（大地图/过场）不再直接退出 —— 实际速度
     * 是静态字段（读不需要实例），写速还有 Global 通道兜底。两者都没有时
     * 才放弃本轮守卫。 */
    bool have_global = speed.global_ready && get_global() != NULL;
    if (!mgr && !have_global) {
        last_mgr = NULL;
        return;
    }

    if (mgr && mgr != last_mgr) {
        last_mgr = mgr;
        /* 进战斗/读档/切场景 = 新 StageManager 实例：立即恢复当前档位
         * （需求拍板：每次进战斗保持对战速度）。 */
        if (applied && config.guard_reassert) {
            if (apply_speed(mgr, current_target(), false)) {
                released = false;
                logmsg("New stage instance; speed re-asserted to %.10gx", current_target());
            }
        }
        return;
    }
    if (mgr) last_mgr = mgr;
    if (!applied) return;    /* player never pressed +/-: stay fully native */

    float actual = read_base_speed(mgr);   /* v1.1.4: real static-field read */

    /* ---- v1.1.12: 菜单/弹窗状态机（用户拍板 2026-09-20）----
     * 菜单打开期间保持 1x（游戏自己一般已经压回 1x，这里兜底压制），
     * 菜单关闭的瞬间立即抢回设定档位，不走 3 秒限时让路。
     * 这正是用户报的 BUG 2 的修复点：以前游戏在菜单/回图时把速度压回 1x，
     * 大地图上守卫因为没有 StageManager 直接退出，速度就永远停在 1x。 */
    {
        bool menu = menu_open_now();
        static bool menu_was_open;
        if (menu != menu_was_open) {
            menu_was_open = menu;
            if (!menu && config.guard_reassert) {
                if (apply_speed_any(mgr, current_target(), false)) {
                    released = false;
                    logmsg("Menu closed; speed re-asserted to %.10gx", current_target());
                }
            } else if (menu && !mgr) {
                /* v1.1.15 修复：大地图上静态 baseGameSpeed 恒为 1（真实速度
                 * 经 Global 通道生效，不落在这个字段上），旧代码的
                 * 「actual>1 才压 1x」在地图上永远不触发 —— 物品栏因此一直
                 * 保持倍速，拿起物品的交互直接坏掉（用户 2026-09-21 实测）。
                 * 菜单打开的瞬间无条件压 1x。 */
                if (apply_speed_any(NULL, 1.0f, false)) {
                    released = true;
                    released_at = GetTickCount64();
                    logmsg("Menu/popup open (big map); speed forced to 1x");
                }
            }
        }
        if (menu) {
            if (mgr && actual > 1.0001f && guard_set_gate()) {
                if (apply_speed_any(mgr, 1.0f, false)) {
                    released = true;
                    released_at = GetTickCount64();
                    static uint64_t last_menu_log;
                    uint64_t now = GetTickCount64();
                    if (now - last_menu_log > 3000) {
                        last_menu_log = now;
                        logmsg("Menu/popup open; speed held at 1x (was %.10gx)", actual);
                    }
                }
            }
            return;
        }
    }

    if (!mgr) {
        /* v1.1.15：大地图上 actual 恒为 1、不代表真实速度，基于它的
         * yield/3 秒抢回只会空转刷日志（「Game changed the speed to 1」
         * 全是误报）。档位恢复交给菜单关闭沿 + 进战斗沿，这里直接返回。 */
        released = false;
        return;
    }

    bool evt_ctx = false;
    bool dlg = dialogue_active_ex(&evt_ctx);

    if (dlg) {
        grace_timing = false;
        /* ---- v1.1.10 判据：先问「对话框开着吗」，再问「这一页有配音吗」 ----
         *
         * 演进史（每一版都是前一版实机暴露的问题）：
         *   v1.1.7 用三层取或（IsDialogueNow || IsEventShowingNow ||
         *          IsEventContext）→ 剧情里的走位不再被加速，但入场走位
         *          也被一起锁住了。
         *   v1.1.8 加 segmentHasDialogue（本段演出「出现过」台词吗）区分
         *          入场走位 → 但它是个**会残留的 latch**：本段只要说过一句
         *          台词就一直为真，同一段里后面的无语音战斗节拍（对手战死）
         *          被误压 1x = 用户报的「少数情况下对战过程原速度」。
         *   v1.1.9 改用 NewUIDialogueTalk._curPageHasVoice（这一页有没有
         *          配音）→ **实机完全没生效**：该字段挂在对话框 UI 组件上，
         *          对话框不弹出时组件没有实例，被动钩子收不到，读值恒为
         *          「不可用」，于是每次都走回退、由 segmentHasDialogue 决定
         *          → 行为退化成 v1.1.8。用户报「剧情中的人物移动依旧还是
         *          慢的」，正是这个退化。
         *
         * v1.1.10 的修法：补上一直缺的那一环 —— 用 GameSpeedCtrl 的静态
         * 字段 talkPanelShowing（「对话框此刻是否可见」，静态区直读，不依赖
         * 任何实例或钩子）做**闸门**：
         *
         *   talkPanelShowing 假 → 对话框没开
         *       = 人物走位 / 对手战死 / 入场 / 纯演出节拍
         *       → 跟随设定档位（用户在 AskUserQuestion 里拍板：只在「有配音
         *         的那一页」锁 1x，其余全部跟随档位）
         *
         *   talkPanelShowing 真 → 对话框开着，此时才谈得上「这一页」
         *       有配音 _curPageHasVoice=1 → 锁 1x（用户要的原速）
         *       无配音 _curPageHasVoice=0 → 跟随档位（无声对话页）
         *       读不到（实例没捕获）  → 保守锁 1x（对话框开着却不知道有没有
         *                              配音时，宁可慢也不要漏掉剧情）
         *
         * 关键：**不再依赖 segmentHasDialogue**。它是残留 latch，正是
         * v1.1.8 那个 bug 的根源；只有在 talkPanelShowing 也拿不到时，
         * 才回退到它（完整的 v1.1.8 行为），保证任何情况下都不出新问题。 */
        bool panel_up = speed.talk_panel_ready && gs_bool_field(speed.f_talk_panel);
        int has_voice = current_page_has_voice();
        bool follow_step;
        const char *why;
        /* 诊断：报告本帧走的是哪条分支、以及实例是否新鲜。 */
        if (config.verbose_log) {
            static uint64_t next_crit;
            uint64_t t = GetTickCount64();
            if (t >= next_crit) {
                next_crit = t + 1000;
                void *ti = talk_instance();
                logmsg("CRITPROBE dlg=1 evtCtx=%d panel=%d hasVoice=%d inst=%p "
                       "ageMs=%llu segDlg=%d",
                       evt_ctx ? 1 : 0, panel_up ? 1 : 0, has_voice, ti,
                       (unsigned long long)(ti ? GetTickCount64() - speed.talk_cached_at
                                               : 0),
                       speed.seg_dialogue_ready
                           ? (int)gs_bool_field(speed.f_seg_dialogue) : -1);
            }
        }
        if (speed.talk_panel_ready) {
            /* 闸门可用：走 v1.1.10 的三分支。 */
            if (!panel_up) {
                /* 对话框没开 → 走位/战死/入场 → 跟随设定档位。 */
                follow_step = true;
                why = "talk panel closed (walk / death / entrance)";
            } else if (has_voice == 0) {
                /* 对话框开着，但这一页没有配音 → 跟随档位。 */
                follow_step = true;
                why = "voiced-panel page has no voice";
            } else {
                /* 对话框开着且（有配音 或 读不到实例）→ 锁 1x。 */
                follow_step = false;
                why = NULL;
            }
        } else {
            /* 闸门拿不到：完整退回 v1.1.8 的两级判据，确保不出新问题。 */
            if (has_voice == 0) {
                follow_step = true;
                why = "page has no voice (no panel gate)";
            } else if (has_voice < 0) {
                follow_step = entrance_walk_fastforward(evt_ctx);
                why = "no panel gate / criterion unavailable; "
                      "segmentHasDialogue fallback";
            } else {
                follow_step = false;
                why = NULL;
            }
        }

        if (follow_step) {
            static uint64_t last_walk_log;
            uint64_t now = GetTickCount64();
            if (now - last_walk_log > 3000) {
                last_walk_log = now;
                logmsg("Voiceless beat (%s); speed follows the step (actual %.10gx)",
                       why, actual);
            }
            /* fall through：让下面的硬地板 + 对账逻辑照常把档位维持住 */
        } else {
            dlg_seen = true;
            /* 兜底保险：对话/演出期间把速度压回 1x（游戏自己的 ResetEventSpeed
             * 一般已经做了；这一步防个别路径漏掉，见调研报告 §4.3）。
             * v1.1.4：走 guard_set_gate 保险丝（1 Hz 上限），日志 3 秒节流。
             * v1.1.7：判据扩到 IsEventContext 后，「剧情演出中的移动」也会
             * 走这条分支锁 1x（用户报的问题），日志标注命中来源。
             * v1.1.9：判据再收紧为「本页有配音」，日志标注是配音还是旧判据。
             * v1.1.10：闸门改为「对话框可见」，日志标注命中来源。 */
            if (config.dialogue_force_1x && actual > 1.0001f && guard_set_gate()) {
                if (apply_speed(mgr, 1.0f, false)) {
                    released = true;
                    released_at = GetTickCount64();   /* v1.1.6: yield clock */
                    static uint64_t last_dlg_log;
                    uint64_t now = GetTickCount64();
                    if (now - last_dlg_log > 3000) {
                        last_dlg_log = now;
                        logmsg("Story/dialogue with voice active (%s); "
                               "speed held at 1x (was %.10g)",
                               has_voice == 1 ? "voiced page"
                                              : (evt_ctx ? "event context"
                                                         : "dialogue"), actual);
                    }
                }
            }
            return;
        }
    }
    if (dlg_seen) {
        dlg_seen = false;
        grace_timing = true;
        dlg_end_tick = GetTickCount64();
    }
    if (grace_timing) {
        /* 演出结束后的恢复宽限期（防突跳），过后恢复设定档位。
         * 宽限期内不 return —— 继续向下走硬地板检查（见下）。 */
        if (GetTickCount64() - dlg_end_tick >=
            (uint64_t)(speed.grace_seconds * 1000.0f)) {
            grace_timing = false;
            if (config.guard_reassert && guard_set_gate()) {
                if (apply_speed(mgr, current_target(), false)) {
                    released = false;
                    logmsg("Grace period over; speed re-asserted to %.10gx", current_target());
                }
            }
            return;
        }
    }

    /* 硬地板（用户需求 2026-09-20）：只要不是对话让路期间，实际速度低于 1x
     * 就立即抢回设定档位。游戏自己的慢动作演出若把 baseGameSpeed 压到 1 以下
     * 又不恢复，这里兜底；日志 3 秒节流防刷屏。 */
    {
        float target = current_target();
        static uint64_t last_floor_log;
        /* v1.1.4: the read is the real baseGameSpeed (static API), so the
         * floor works on live data; guard_set_gate caps it at 1 Hz as a
         * belt-and-braces fuse on top of the value-triggered logic. */
        if (actual > 0.001f && actual < 0.999f && guard_set_gate()) {
            if (apply_speed(mgr, target, false)) {
                released = false;
                uint64_t now = GetTickCount64();
                if (now - last_floor_log > 3000) {
                    logmsg("Speed floor: actual %.10gx below 1x; restored %.10gx",
                           actual, target);
                    last_floor_log = now;
                }
            }
            return;
        }

        /* 每帧对账：实际速度偏离设定档位 = 游戏自己改了速度（官方热键/Ctrl 加速/
         * 事件仲裁）。让路不抢状态；对话信号结束后或玩家再按键时恢复。
         * v1.1.4：读数已是真实 baseGameSpeed，对账恢复意义——既不误判
         * 「游戏改速」也不误判「回到档位」。
         *
         * v1.1.6（用户报「非剧情突然变慢」）：让路改成**限时**的。
         * 旧逻辑一旦判定「游戏改速」就永久 released，只有对话信号结束
         * 或玩家再按键才抢回；如果游戏借走速度后不还（较长的过场、其
         * 对话标志我们看不到的演出），档位就永久丢失——玩家体验就是
         * 「突然变慢、没有原因」。现在游戏借走速度后最多让 3 秒，之后
         * 按 1 Hz 节奏抢回设定档位（guard_set_gate 限频）。 */
        if (actual > 0.001f &&
            (actual < target - 0.0001f || actual > target + 0.0001f)) {
            uint64_t now = GetTickCount64();
            if (!released) {
                released = true;
                released_at = now;
                logmsg("Game changed the speed to %.10g; guard yields for %.1fs "
                       "(press +/- to re-apply %.10gx immediately)",
                       actual, GUARD_YIELD_MS / 1000.0, target);
        } else if (now - released_at >= GUARD_YIELD_MS && guard_set_gate()) {
            /* 借用超时：游戏没有在合理时间内还回控制权，抢回档位。
             * v1.1.12: 走统一通道 —— 大地图上没有 StageManager 时由
             * Global.SetGameSpeed 兜底，游戏压回 1x 后 3 秒即可抢回。 */
            if (apply_speed_any(mgr, target, false)) {
                released = false;
                logmsg("Game kept %.10g for over %.1fs; speed re-asserted to %.10gx",
                       actual, GUARD_YIELD_MS / 1000.0, target);
            }
        }
        } else if (actual > 0.001f && released) {
            released = false;
            if (config.verbose_log)
                logmsg("Speed back at %.10gx; guard resumed", actual);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Mod enable switch (owned by the game's own Mod manager)             */
/* ------------------------------------------------------------------ */

/* The manifest's modId is an IL2CPP UTF-16 string: length at +0x10, chars at
 * +0x14. Compared against the id named in the .ini, which the installer keeps in
 * sync with the mod.json it installs. */
static bool id_matches(void *s) {
    if (!s || config.mod_id_chars <= 0) return false;
    int slen = VAL(int, s, 0x10);
    if (slen == config.mod_id_chars)
        return !memcmp((BYTE *)s + 0x14, config.mod_id, (size_t)config.mod_id_chars * 2);
    /* 【v1.1.20 工坊条目】游戏把工坊订阅条目的主键写成 "<modId>@<文件ID>"
     * （实测：待机快捷键@3802488917）→ 全等匹配打不中，订阅玩家倍速全关。
     * 放宽为：前缀相同且紧跟 '@'。 */
    if (slen > config.mod_id_chars) {
        const WCHAR *p = (const WCHAR *)((BYTE *)s + 0x14);
        return !memcmp(p, config.mod_id, (size_t)config.mod_id_chars * 2) &&
               p[config.mod_id_chars] == L'@';
    }
    return false;
}

/* 【v2.3.0】回退白名单匹配：与 id_matches 同一布局语义（len@0x10 chars@0x14）。 */
static bool sk_fallback_matches(void *s) {
    if (!sk_fallback_ready || !s) return false;
    int slen = VAL(int, s, 0x10);
    if (slen <= 0 || slen > SK_MOD_ID_MAX - 1) return false;
    for (int i = 0; i < SK_FALLBACK_IDS; ++i)
        if (sk_fallback_chars[i] == slen &&
            !memcmp((BYTE *)s + 0x14, sk_fallback_ids[i], (size_t)slen * 2))
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
                    /* While the configured id has not been matched yet, remember
                     * what the game does have — that line turns "wrong modId"
                     * into a one-glance diagnosis from a remote log. */
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
                    if (!fb_mod && sk_fallback_matches(sid)) fb_mod = mod;
                    if (id_matches(sid)) {
                        void *err = PTR(mod, 0x28);
                        int mf = VAL(BYTE, mod, 0x20), cp = VAL(BYTE, mod, 0x34);
                        int er = (mf && cp) ? (err ? VAL(int, err, 0x18) : 0) : -1;
                        bool on = mf && cp && er == 0;
                        /* 【v1.1.20】本地 + 工坊两条目可能同时命中：任一启用即算开；
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
    /* 【v2.3.0 自愈】主 mod_id 在列表里一个条目都对不上（reason==4 且
     * modflag 仍为初值），而列表里存在自家已知 ID 的条目 → 按其状态生效，
     * 残留旧 ini（mod_id=战斗倍速快捷键）的玩家免重装自愈。 */
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
        char idbuf[SK_MOD_ID_MAX * 4];
        wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
        logmsg("Mod %s; id=%s; reason=%s; enabled=%d compatible=%d validationItems=%d",
               enabled ? "enabled" : "disabled", idbuf, reasons[reason], modflag, compatible, errors);
        if (used_fallback && !sk_fallback_active)
            logmsg("FALLBACK: ini mod_id \"%s\" matches no game entry; using the "
                   "built-in id instead (stale hslr-speed-hotkey.ini? re-run the "
                   "installer to sync mod_id)", idbuf);
        sk_fallback_active = used_fallback;
        if (reason == 4)
            logmsg("Game Mod entries: %d seen; ids: %s", seen_count,
                   seen_ids[0] ? seen_ids : "(none readable)");
        else if (seen_count > 0)
            /* 【v1.1.20 取证】闸门状态每次变化都转储实际见到的条目 ID。 */
            logmsg("Game Mod entries: %d seen; ids: %s", seen_count, seen_ids);
    }
}

/* ------------------------------------------------------------------ */
/* Physical key reader (Unity Input System, main thread only)          */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned actions; /* bit 0 = speed_up, bit 1 = speed_down */
    bool shift, ctrl, alt, meta, available;
} SKKeyState;

/* Set once the reader has polled a frame; a press made before the reader was
 * primed cannot be replayed later. */
static bool key_primed;

static bool modifier_held(uintptr_t ga_, void *keyboard, int key) {
    if (!keyboard) return false;
    void *control = ((void *(*)(void *, int, void *))(ga_ + rva_of(0x39F3E20)))(keyboard, key, 0);
    return control && ((bool (*)(void *, void *))(ga_ + rva_of(0x39A2A20)))(control, 0);
}

/* Poll once per game frame. passing focused=false clears the reader so an
 * unsafe press cannot become a deferred action when focus returns. Only the
 * wasPressedThisFrame edge triggers: holding +/- must not run the ladder. */
static SKKeyState keys_poll(uintptr_t ga_, bool focused) {
    SKKeyState out = {0};
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
        bool edge = ((bool (*)(void *, void *))(ga_ + rva_of(0x39A2AF0)))(control, 0);
        if (edge && key_primed) actions |= 1u << config.bindings[i].action;
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
/* Step ladder                                                         */
/* ------------------------------------------------------------------ */

static void step_apply(const char *dir) {
    void *mgr = get_stage_mgr();
    if (!mgr) mgr = stage_probe();   /* 用户主动按键：战斗上下文，运行时成熟 */
    if (!mgr) {
        /* v1.1.12: 大地图等没有 StageManager 的场景 —— Global 通道直写。
         * Global.SetGameSpeed 是官方总闸（SetBaseGameSpeed 内部就转发到它，
         * GM 命令同款），按键即刻生效，不再「只记档」。Global 也不可用时
         * （尚未解析/实例未就绪）才退回只记档，实例出现后守卫会恢复。 */
        if (apply_speed_global(current_target())) {
            applied = true;
            released = false;
            logmsg("Speed %s -> %.10gx (step %d/%d, global channel — no StageManager here)",
                   dir, current_target(), step_index + 1, config.step_count);
            osd_show(current_target());
        } else {
            if (config.verbose_log)
                logmsg("No StageManager and no Global channel yet; step %d remembered (%.10gx)",
                       step_index + 1, current_target());
        }
        return;
    }
    /* v1.1.4: player keypress — the only path allowed to run the full
     * SetGameSpeed entry (the combo the user confirmed working in v1.1.3). */
    if (apply_speed(mgr, current_target(), true)) {
        applied = true;
        released = false;
        logmsg("Speed %s -> %.10gx (step %d/%d)", dir, current_target(),
               step_index + 1, config.step_count);
        osd_show(current_target());   /* v1.1.7: 自绘徽标常显当前档位 */
        /* v1.1.5: GodsetToast removed — its DrawGUI crashed the game
         * while rendering our toasts (WER +0x56992e, twice, same
         * bucket). See the header comment. OSD returns as a D3D11-drawn
         * overlay in a future release. */
    }
}

static void handle_steps(unsigned actions) {
    if (actions & 1u) {                       /* speed_up */
        if (step_index < config.step_count - 1) {
            step_index++;
            step_apply("up");
        } else if (config.verbose_log) {
            logmsg("Already at the max %.10gx", current_target());
        }
    }
    if (actions & 2u) {                       /* speed_down */
        if (step_index > 0) {
            step_index--;
            step_apply("down");
        } else if (config.verbose_log) {
            logmsg("Already at 1x (original speed)");
        }
    }
}

/* v1.1.12: 菜单打开期间的按键 —— 档位照常记忆（含边界），写入推迟到菜单
 * 关闭。否则会出现「按 + 应用 5x → 下一帧菜单守卫又压回 1x」的抖动。
 * 日志 3 秒节流防刷屏。 */
static void handle_steps_deferred(unsigned actions) {
    bool changed = false;
    if (actions & 1u) {                       /* speed_up */
        if (step_index < config.step_count - 1) {
            step_index++;
            changed = true;
        }
    }
    if (actions & 2u) {                       /* speed_down */
        if (step_index > 0) {
            step_index--;
            changed = true;
        }
    }
    if (!changed) return;
    static uint64_t next_log;
    uint64_t t = GetTickCount64();
    if (t >= next_log) {
        next_log = t + 3000;
        logmsg("Menu open; step %d remembered (%.10gx) — applies when the menu closes",
               step_index + 1, current_target());
    }
}

/* ------------------------------------------------------------------ */
/* Config hot reload                                                   */
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
    char idbuf[SK_MOD_ID_MAX * 4];
    wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
    char steps[128] = "";
    for (int i = 0; i < config.step_count; ++i)
        snprintf(steps + strlen(steps), sizeof(steps) - strlen(steps), "%s%.10g",
                 i ? "/" : "", config.steps[i]);
    logmsg("Configuration %s: %d binding(s), steps=%s, requireNoModifier=%d, "
           "dialogueForce1x=%d, guardReassert=%d, forceEnable=%d, enable=%d, verboseLog=%d, traceGset=%d, modId=%s, parseErrors=%d",
           reload ? "reloaded" : "loaded", config.count, steps,
           config.require_no_modifier ? 1 : 0, config.dialogue_force_1x ? 1 : 0,
           config.guard_reassert ? 1 : 0, config.force_enable ? 1 : 0,
           config.enable ? 1 : 0,
           config.verbose_log ? 1 : 0, config.trace_gset ? 1 : 0, idbuf, config.errors);
    for (int i = 0; i < config.count; ++i)
        logmsg("  binding: %s -> %s", config.bindings[i].key_name, config.bindings[i].action_name);
}

static void config_setup(void) {
    path_for(config_path, SK_CONFIG_FILE);
    if (GetFileAttributesW(config_path) == INVALID_FILE_ATTRIBUTES) {
        if (sk_config_write_default(config_path)) {
            logmsg("Wrote the default configuration next to the Mod DLL");
        } else {
            logmsg("Could not create default configuration; using built-in defaults");
        }
    }
    if (!sk_config_load(&config, config_path)) {
        logmsg("Configuration unreadable or empty; using built-in defaults (+/- steps 1/2/5/10)");
        sk_config_defaults(&config);
    }
    config_log_summary(false);
    umbrella_features_reload();   /* 【v2.3.0】启动时读一次伞形总控 */
    logmsg("Umbrella gate: [features] speed = %d", umbrella_enable ? 1 : 0);
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
    SKConfig candidate;
    if (sk_config_load(&candidate, config_path)) {
        config = candidate;
        /* 档位序列变化后把当前档位收回合法范围。 */
        if (step_index >= config.step_count) step_index = config.step_count - 1;
        if (step_index < 0) step_index = 0;
        config_log_summary(true);
        config_remember_stamp();
    } else {
        logmsg("Configuration reload failed or empty; keeping the previous bindings");
        config_remember_stamp();
    }
}

/* 【v2.3.0 伞形总控】读 hslr-wait-hotkey.ini 的 [features] speed。
 * 只认该段的 speed 键；文件缺失/键缺失 = 缺省开（*out 调用方预置 true）。 */
static void umbrella_read_speed(bool *out) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, SK_UMBRELLA_INI);
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > SK_CONFIG_MAX_BYTES) {
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
        char *s = sk_trim(line);
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
                for (char *n = sk_trim(s + 1); *n && li < 23; ++n) {
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
        char *key = sk_trim(s);
        char klow[24];
        int ki = 0;
        for (; key[ki] && ki < 23; ++ki) {
            char ch = key[ki];
            klow[ki] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
        }
        klow[ki] = '\0';
        if (!strcmp(klow, "speed")) {
            *out = sk_value_is_true(sk_trim(eq + 1));
            break;
        }
        line = next;
    }
    HeapFree(GetProcessHeap(), 0, buffer);
}

/* mtime 变化时重读伞形总控；文件消失 → 回到缺省开。只在值翻转时打日志。 */
static void umbrella_features_reload(void) {
    WCHAR p[HSLR_PATH_MAX];
    path_for(p, SK_UMBRELLA_INI);
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
    if (file_there) umbrella_read_speed(&value);
    if (value != umbrella_enable) {
        umbrella_enable = value;
        logmsg("Umbrella ini [features] speed -> %d; speed hotkeys %s",
               value ? 1 : 0, value ? "active" : "disabled");
    }
}

/* ------------------------------------------------------------------ */
/* v1.1.6 OSD: our own on-screen speed badge (GDI overlay)               */
/* v1.1.7: the badge is now PERSISTENT instead of a 2.5 s flash          */
/* ------------------------------------------------------------------ */
/* Why we draw it ourselves instead of borrowing a game UI (v1.1.5 lesson)
 * ---------------------------------------------------------------------
 * Borrowing the game's GodsetToast cost two crashes: our runtime_invoke'd
 * Show() queued an entry and the game's own DrawGUI died while rendering
 * it (WER GameAssembly+0x56992e, twice). An overlay WE own has no such
 * coupling: nothing IL2CPP is touched and no game object is modified.
 *
 * Why GDI rather than hooking D3D11 Present
 * -----------------------------------------
 * Hooking the swap chain would mean owning shader/pipeline state inside
 * the game's render thread - a new class of risk for a mod whose last
 * three releases were all crash fixes. A layered, click-through child
 * window painted with plain GDI is the classic overlay route: it
 * composites over the game with zero access to any D3D state. If anything
 * in this path fails we simply stop drawing; the speed feature never
 * depends on it.
 *
 * Geometry and lifetime: a WS_EX_LAYERED | WS_EX_TRANSPARENT |
 * WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE child of the game window, sitting in
 * the lower-right corner, showing "x2" / "x5" / "x10".
 *
 * v1.1.6 flashed the badge for 2.5 s after each speed change (osd_until).
 * v1.1.7（用户要求）makes it always on: after the first +/- press the
 * badge stays put and keeps reflecting the live step, so the player can
 * always answer "what speed am I at?" at a glance. Before the first press
 * there is nothing meaningful to show (the step is still at its default),
 * so the window is not created/shown - a player who never touches the
 * hotkeys still gets no extra window over the game. The badge hides only
 * while the game is not in the foreground (Alt-Tab away) and returns on
 * focus. */

#define OSD_CLASS_NAME L"HSLR_SpeedHotkey_OSD"

static HWND     osd_window;          /* layered overlay child window     */
static HWND     osd_host;            /* game window we are attached to   */
static bool     osd_class_done;
static HFONT    osd_font;
static volatile float    osd_value = 1.0f;
static volatile bool     osd_shown_once;  /* v1.1.7: badge becomes permanent */
static volatile bool     osd_visible_state;
static bool     osd_failed;
static uint64_t osd_create_try_at;

/* v1.1.7（用户要求）：速度样式**常显**，不再自动消失。
 * 首次切档后徽标一直挂在右上角（v2.6.0 起自右下角移入），实时反映当前档位；
 * 只在「游戏不在前台」（Alt-Tab 离开）时隐藏，回到游戏即恢复。 */
static void osd_show(float v) {
    osd_value = v;
    osd_shown_once = true;
}

/* 渲染一帧：把 "xN" 画在小底板上。全程不碰游戏对象；
 * 失败则静默停画（osd_failed）——速度功能不依赖它。 */
static void osd_paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (hdc) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH plate = CreateSolidBrush(RGB(12, 12, 14));
        FillRect(hdc, &rc, plate);
        DeleteObject(plate);
        /* 1 px 描边：亮色场景下同样看得清 */
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(240, 210, 90));
        HGDIOBJ old_pen = SelectObject(hdc, pen);
        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, rc.right, rc.bottom);
        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_brush);
        DeleteObject(pen);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(250, 235, 150));
        HGDIOBJ old_font = SelectObject(hdc, osd_font);
        char text[32];
        if (osd_value >= 9.95f)       snprintf(text, sizeof(text), "x%.0f", (double)osd_value);
        else if (osd_value >= 1.95f)  snprintf(text, sizeof(text), "x%.0f", (double)osd_value);
        else                          snprintf(text, sizeof(text), "x%.1f", (double)osd_value);
        DrawTextA(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old_font);
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK osd_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:      osd_paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;              /* osd_paint 自己填底 */
    case WM_NCHITTEST:  return HTTRANSPARENT;  /* 鼠标穿透 */
    default:            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

/* 每帧从 input hook 调用（游戏主线程）：只做廉价 Win32 调用。
 * 游戏窗口一旦失效/换新（读档、切全屏）会在下次 tick 重新挂载。 */
static void osd_tick(void) {
    if (osd_failed) return;
    uint64_t now = GetTickCount64();

    /* 只在前台是本进程时显示（alt-tab 离开自动隐藏） */
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    if (!fg || pid != GetCurrentProcessId()) {
        if (osd_window && osd_visible_state) {
            ShowWindow(osd_window, SW_HIDE);
            osd_visible_state = false;
        }
        return;
    }
    if (fg != osd_host) osd_host = fg;

    if (!osd_class_done) {
        if (now < osd_create_try_at) return;
        osd_create_try_at = now + 3000;
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = osd_wndproc;
        wc.hInstance = self;
        wc.lpszClassName = OSD_CLASS_NAME;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            logmsg("OSD window class registration failed err=%lu; overlay disabled",
                   (unsigned long)GetLastError());
            osd_failed = true;
            return;
        }
        osd_class_done = true;
    }

    RECT wr;
    if (!GetClientRect(osd_host, &wr)) return;
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    if (w <= 0 || h <= 0) return;

    if (!osd_window) {
        /* 字号随窗口高度自适应：0.045 * 高度，夹在 16..48 px */
        int font_h = (int)((float)h * 0.045f);
        if (font_h < 16) font_h = 16;
        if (font_h > 48) font_h = 48;
        osd_font = CreateFontA(-font_h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Consolas");
        osd_window = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            OSD_CLASS_NAME, L"", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, osd_host, NULL, self, NULL);
        if (!osd_window) {
            logmsg("OSD window creation failed err=%lu; overlay disabled",
                   (unsigned long)GetLastError());
            osd_failed = true;
            return;
        }
        /* v2.6.0 rev3（用户要求）：整窗 alpha 230→120（≈47%），只求看得清、
         * 不遮挡身后画面（底板与文字一并变淡，相对对比度不变）。 */
        SetLayeredWindowAttributes(osd_window, 0, 120, LWA_ALPHA);
        logmsg("OSD overlay ready (GDI badge, font=%dpx, host %dx%d)", font_h, w, h);
    }

    if (!osd_shown_once) {
        /* v1.1.7 常显：玩家还没按过 +/-（档位无意义）时不占用画面。 */
        if (osd_visible_state) { ShowWindow(osd_window, SW_HIDE); osd_visible_state = false; }
        return;
    }

    /* 右上角自适应（v2.6.0 用户指定，原右下角）：宽 11.5% 高 5.8%，右边距 2.5% 窗宽、
     * 上边距 13% 窗高（rev2：5.5% 会挡住游戏右上角的按钮，按要求再下移） */
    int bw = (int)((float)h * 0.115f), bh = (int)((float)h * 0.058f);
    if (bw < 64) bw = 64;
    if (bh < 30) bh = 30;
    int bx = w - bw - (int)((float)w * 0.025f);
    int by = (int)((float)h * 0.13f);
    SetWindowPos(osd_window, HWND_TOP, bx, by, bw, bh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    osd_visible_state = true;
    InvalidateRect(osd_window, NULL, FALSE);
}

static bool input_frame_seen;
static uint32_t last_input_frame;
/* v2.0.3 整合：输入对（0x65A4E0/0x657a40）钩子收归伞形 DLL 独占——三个独立
 * MinHook 实例并行钩同一目标地址，目标上最终只有一个 JMP，最后写入者的
 * detour 存活、其余静默失效（v2.0.2 实机实锤：待机/倍速全部无效）。
 * 本模块的焦点/每帧逻辑改由伞形经导出函数 sk_input_focus / sk_frame_tick 驱动。 */
__declspec(dllexport) void sk_input_focus(void) {
    key_primed = false;
}

static bool process_focused(void) {
    DWORD pid = 0;
    HWND window = GetForegroundWindow();
    if (!window) return false;
    GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}

/* 每帧一次；由伞形 DLL 的输入钩子代为调用（v2.0.3，见 sk_input_focus 注释）。 */
/* 【v2.9.0 免版本跟进】模块级闸门：check_fingerprints() 通过（NATIVE 哈希命中，
 * 或 ADAPTIVE 下本模块全部锚点唯一找回）后由 worker 置真。未通过前 tick 直接
 * 返回，绝不调用任何尚未解析的地址。 */
static bool sk_reloc_ok = false;

__declspec(dllexport) void sk_frame_tick(void) {
    if (!sk_reloc_ok) return;
    uint32_t frame = FN(0x3FFA550, uint32_t, void *)(NULL);
    if (input_frame_seen && frame == last_input_frame) return;
    input_frame_seen = true;
    last_input_frame = frame;

    config_maybe_reload();
    umbrella_features_reload();   /* 【v2.3.0】伞形总控热重载（mtime 变化才真读） */
    read_mods();

    /* v2.0.0 整合版：ini 子开关 enable；v2.3.0 起再与伞形 [features] speed
     * 总控取与。关掉时不再解析速度/守卫/监听按键，且在翻转瞬间把档位收回
     * 1x（prev_enable 锁存保证只做一次）——防止玩家在 10x 状态下改开关后
     * 被守卫永远钉在 10x。 */
    static bool prev_enable = true;
    if (!config.enable || !umbrella_enable) {
        if (prev_enable) {
            prev_enable = false;
            if (osd_window && osd_visible_state) {
                ShowWindow(osd_window, SW_HIDE);
                osd_visible_state = false;
            }
            if (speed_ready && step_index != 0) {
                step_index = 0;
                step_apply("down");
                logmsg("feature gate off (enable/umbrella): speed restored to 1x");
            } else {
                logmsg("feature gate off (enable/umbrella): speed hotkeys disabled");
            }
        }
        return;
    }
    prev_enable = true;

    /* Poll every frame, even while disabled, so the reader stays primed and a
     * press cannot be replayed once the Mod is switched on. */
    bool focused = process_focused();
    SKKeyState keys = keys_poll((uintptr_t)ga, focused);

    if (!speed_ready) speed_resolve_tick();
    if (speed_ready) guard_tick();
    if (speed_ready) menu_probe_tick();   /* v1.1.13: 500ms 节流，verbose 日志 */
    osd_tick();      /* v1.1.7: 自绘速度徽标（首次切档后常显） */

    if (!enabled || !focused || !keys.available) return;
    if (!keys.actions) return;
    /* require_no_modifier 只挡 Ctrl/Alt/Win —— Shift 放行：主键盘「+」本身就要
     * Shift 配合，不能因为按了 Shift 就把加速键拒掉（用户反馈 2026-09-20）。 */
    if (config.require_no_modifier && (keys.ctrl || keys.alt || keys.meta)) {
        if (config.verbose_log)
            logmsg("Hotkey ignored: Ctrl/Alt/Win is held (require_no_modifier=1)");
        return;
    }
    if (!in_scope()) {
        /* 主菜单/过图/演出中忽略按键（调研 §4.4：加载/过图中忽略防误触）。 */
        if (config.verbose_log)
            logmsg("Hotkey ignored: not during a fight or on the big map");
        return;
    }
    /* v1.1.12: 菜单/弹窗开着时只记档不写入（菜单期间速度保持 1x，关闭后
     * 由守卫立即恢复档位）。先跑一次 in_scope 已经缓存了 logic 实例与
     * BigMapLogic 判据，这里的判定是廉价的。 */
    if (menu_open_now()) {
        handle_steps_deferred(keys.actions);
        return;
    }
    handle_steps(keys.actions);
}

/* v2.0.3 起本模块静态阶段不再装钩子；保留该工具函数供 speed_resolve_tick 的
 * 动态钩子路径使用（若未来重构移除，一并删掉）。 */
__attribute__((unused)) static bool addhook(uintptr_t rva, void *fn, void **original) {
    uintptr_t at = rva_of(rva);
    if (!at) {
        logmsg("Hook skipped: anchor RVA=%llx could not be relocated", (unsigned long long)rva);
        return false;
    }
    MH_STATUS status = MH_CreateHook((BYTE *)ga + at, fn, original);
    if (status != MH_OK) {
        logmsg("Hook creation failed RVA=%llx status=%d", (unsigned long long)rva, status);
        return false;
    }
    return true;
}

static DWORD WINAPI worker(void *unused) {
    (void)unused;
    hslr_log_open(SK_LOG_FILE);
    logmsg("HSLR Speed Hotkey %s; independent d3d11.dll; game 2.2.4 / build 25767968", SK_VERSION);
    hslr_log_self_path();
    config_setup();
    sk_fallback_init();           /* 【v2.3.0】modId 自愈白名单（静态 UTF-8 → UTF-16） */
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
    /* 【v2.9.0】重定位层必须在任何 FN 之前就绪。配置读伞形 ini（四个模块共用）。 */
    {
        WCHAR up[HSLR_PATH_MAX];
        path_for(up, SK_UMBRELLA_INI);
        rva_reloc_configure(up);
    }
    rva_reloc_init(build_ok == 1);
    sk_reloc_ok = check_fingerprints();
    if (!sk_reloc_ok) {
        logmsg("Anchor gate failed; Mod inactive and nothing is hooked");
        return 0;
    }
    if (!il2cpp_bind()) {
        logmsg("IL2CPP API binding failed; the input hooks still work but the "
               "speed chain will never resolve; Mod passive");
    }
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK) {
        logmsg("MinHook initialization failed status=%d; Mod inactive", init_status);
        return 0;
    }
    /* v2.0.3：不再钩输入对（收归伞形 DLL 独占，见 sk_input_focus 处注释）；
     * 动态速度链钩子在 speed_resolve_tick 里按需装配，此处无静态钩子。 */
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        logmsg("Hook installation failed; Mod inactive");
        return 0;
    }
    char idbuf[SK_MOD_ID_MAX * 4];
    wide_to_utf8(config.mod_id, idbuf, sizeof(idbuf));
    logmsg("Hooks ready; input frames via umbrella DLL; waiting for modId=\"%s\" "
           "to be switched on in the game's Mod manager",
           idbuf);
    return 0;
}

/* 入口引导：DLL 被加载器映射进来时只做最轻的一件事——拉起 worker 线程，
 * 全部初始化都在那边做。DllMain 里严禁干重活（加载器锁）。 */
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
