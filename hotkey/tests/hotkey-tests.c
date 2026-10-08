/* Host-side checks for the wait-hotkey configuration layer.
 *
 * These run on a normal desktop and cover everything that does not need the
 * game: key-name mapping, action mapping, INI parsing, the fallback contract,
 * and a write-then-read round trip of the generated default file.
 *
 * Build + run:
 *   zig cc -target x86_64-windows-gnu -I src tests/hotkey-tests.c -o build/hotkey-tests.exe
 *   build/hotkey-tests.exe
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "../src/config.h"

static int failures = 0;
static int checks = 0;

static void check(bool cond, const char *what) {
    ++checks;
    if (cond) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        ++failures;
    }
}

static int count_first(WKConfig *c, int action) {
    int n = 0;
    for (int i = 0; i < c->count; ++i)
        if (c->bindings[i].action == action) ++n;
    return n;
}

/* Compare a parsed mod id against the UTF-8 spelling used in the .ini. */
static bool id_is(const WKConfig *c, const char *utf8) {
    WCHAR want[WK_MOD_ID_MAX];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, want, WK_MOD_ID_MAX);
    return n > 0 && c->mod_id_chars == (int)wcslen(want) && !wcscmp(c->mod_id, want);
}

static void test_keymap(void) {
    printf("key names\n");
    check(wk_key_code("space") == 1, "space -> 1");
    check(wk_key_code("SPACE") == 1, "case insensitive");
    check(wk_key_code("空格") == 1, "chinese alias 空格 -> 1");
    check(wk_key_code("enter") == 2, "enter -> 2");
    check(wk_key_code("f") == 20, "f -> 20");
    check(wk_key_code("digit1") == 41, "digit1 -> 41");
    check(wk_key_code("digit0") == 50, "digit0 -> 50");
    check(wk_key_code("leftshift") == 51, "leftshift -> 51");
    check(wk_key_code("shift") == 51, "alias shift -> 51");
    check(wk_key_code("escape") == 60, "escape -> 60");
    check(wk_key_code("numpadperiod") == 82, "numpadperiod -> 82");
    check(wk_key_code("numpad0") == 84, "numpad0 -> 84");
    check(wk_key_code("f12") == 105, "f12 -> 105");
    check(wk_key_code("nosuchkey") == -1, "unknown key rejected");
    check(wk_key_code("") == -1, "empty key rejected");
}

static void test_actions(void) {
    printf("action names\n");
    check(wk_action_code("wait") == 6, "wait -> 6");
    check(wk_action_code("待机") == 6, "待机 -> 6");
    check(wk_action_code("MOVE") == 0, "MOVE -> 0");
    check(wk_action_code("skill") == 2, "skill -> 2");
    check(wk_action_code("bogus") == -1, "unknown action rejected");
}

static void test_parse_basic(void) {
    printf("parsing\n");
    WKConfig c;
    char text[] = "[hotkeys]\nspace = wait\n";
    check(wk_config_parse(&c, text), "single binding parses");
    check(c.count == 1 && c.bindings[0].key == 1 && c.bindings[0].action == 6,
          "space -> wait stored");

    char multi[] = "[hotkeys]\nspace = wait\nf = attack\nnumpad0 = wait\n";
    wk_config_parse(&c, multi);
    check(c.count == 3, "three bindings stored");
    check(count_first(&c, 6) == 2, "wait bound twice");
    check(count_first(&c, 1) == 1, "attack bound once");

    char dup[] = "[hotkeys]\nspace = attack\nspace = wait\n";
    wk_config_parse(&c, dup);
    check(c.count == 1 && c.bindings[0].action == 6, "duplicate key keeps the last action");

    char chinese[] = "[hotkeys]\n空格 = 待机\n";
    wk_config_parse(&c, chinese);
    check(c.count == 1 && c.bindings[0].action == 6, "chinese key and action accepted");
}

static void test_parse_safety(void) {
    printf("fallback contract\n");
    WKConfig c;
    char all_bad[] = "[hotkeys]\nnosuchkey = wait\nspace = nosuchaction\n";
    check(wk_config_parse(&c, all_bad), "all-invalid file still yields a usable config");
    /* v2.0.2 起默认绑定是两条：Space->wait + G->approach（count==2）。 */
    check(c.count == 2 && c.bindings[0].key == 1 && c.bindings[0].action == 6,
          "invalid entries fall back to Space -> wait (+ G -> approach)");
    check(c.errors == 2, "both invalid entries counted");

    char empty[] = "\n\n# only comments\n";
    check(wk_config_parse(&c, empty), "comment-only file yields a usable config");
    check(c.count == 2 && c.bindings[0].action == 6, "default binding retained");

    char wrong_section[] = "[other]\nspace = wait\n";
    wk_config_parse(&c, wrong_section);
    check(c.count == 2 && c.bindings[0].action == 6, "binding outside [hotkeys] ignored");
    check(c.errors == 1, "stray line counted as an error");

    char options[] = "[options]\nrequire_no_modifier = 0\nverbose_log = 1\nforce_enable = 1\n";
    wk_config_parse(&c, options);
    check(!c.require_no_modifier && c.verbose_log && c.force_enable, "options parsed");
    check(c.count == 2, "options alone keep the default binding");

    /* 【v2.3.0】[features] 分功能总控段：0/off/false 均为关，缺省键保持开。 */
    char feats[] = "[features]\nwait = 0\nspeed = off\npinattr = true\n";
    wk_config_parse(&c, feats);
    check(!c.feat_wait && c.feat_approach && !c.feat_speed && c.feat_pinattr,
          "features parsed (0/off/true, missing key stays default-on)");
    check(c.errors == 0, "features file has no parse errors");
    char feats_bad[] = "[features]\nnosuch = 1\n";
    wk_config_parse(&c, feats_bad);
    check(c.errors == 1 && c.feat_wait && c.feat_approach, "unknown feature key counted, defaults kept");

    char crlf[] = "[hotkeys]\r\nspace = wait\r\n\r\nf = item\r\n";
    wk_config_parse(&c, crlf);
    check(c.count == 2, "CRLF line endings handled");
    check(c.errors == 0, "CRLF produces no errors");
}

static void test_overflow(void) {
    printf("limits\n");
    char text[1024];
    strcpy(text, "[hotkeys]\n");
    static const char *keys[] = {"a","b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q","r"};
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        strcat(text, keys[i]);
        strcat(text, " = move\n");
    }
    WKConfig c;
    wk_config_parse(&c, text);
    check(c.count == WK_MAX_BINDINGS, "binding count capped at WK_MAX_BINDINGS");
    check(c.errors == 2, "overflow counted");
}

static void test_mod_id(void) {
    printf("game-side mod id\n");
    WKConfig c;

    char plain[] = "[hotkeys]\nspace = wait\n";
    wk_config_parse(&c, plain);
    check(id_is(&c, "hslr-wait-hotkey"), "id defaults to hslr-wait-hotkey");
    check(c.mod_id_chars == 16, "default id is 16 UTF-16 units");

    char custom[] = "[options]\nmod_id = my-wait-mod\n";
    wk_config_parse(&c, custom);
    check(id_is(&c, "my-wait-mod"), "custom id parsed");
    check(c.errors == 0, "custom id produces no errors");

    /* The game derives the id from whatever the author typed in its Mod page,
     * so a Chinese id is the normal case for a hand-made package. */
    char chinese[] = "[options]\nmod_id = 待机快捷键\n";
    wk_config_parse(&c, chinese);
    check(id_is(&c, "待机快捷键"), "utf-8 chinese id parsed");
    check(c.mod_id_chars == 5, "chinese id counted in UTF-16 units");

    char quoted[] = "[options]\nmod_id = \"hslr-wait-hotkey\"\n";
    wk_config_parse(&c, quoted);
    check(id_is(&c, "hslr-wait-hotkey"), "surrounding quotes stripped");

    char blank[] = "[options]\nmod_id =\n";
    wk_config_parse(&c, blank);
    check(id_is(&c, "hslr-wait-hotkey"), "blank id falls back to the default");
    check(c.errors == 1, "blank id counted as an error");

    char toolong[256];
    strcpy(toolong, "[options]\nmod_id = ");
    for (int i = 0; i < 80; ++i) strcat(toolong, "x");
    strcat(toolong, "\n");
    wk_config_parse(&c, toolong);
    check(id_is(&c, "hslr-wait-hotkey"), "oversized id falls back to the default");
    check(c.errors == 1, "oversized id counted as an error");

    /* A stale .ini from an older build simply has no mod_id line. */
    char legacy[] = "[options]\nforce_enable = 1\n";
    wk_config_parse(&c, legacy);
    check(id_is(&c, "hslr-wait-hotkey"), "missing id keeps the default");
    check(c.force_enable && c.errors == 0, "legacy file still parses cleanly");
}

static void test_round_trip(void) {
    printf("generated default file\n");
    WCHAR path[MAX_PATH];
    GetTempPathW(MAX_PATH, path);
    wcscat(path, L"hslr-waitkey-roundtrip.ini");
    DeleteFileW(path);
    check(wk_config_write_default(path), "default file written");
    check(!wk_config_write_default(path), "second write refuses to overwrite");

    WKConfig c;
    check(wk_config_load(&c, path), "generated file parses");
    check(c.count == 2 && c.bindings[0].key == 1 && c.bindings[0].action == 6,
          "generated file binds Space -> wait (+ G -> approach)");
    check(c.require_no_modifier, "generated file requires no modifier");
    check(!c.verbose_log, "generated file keeps verbose logging off");
    check(!c.force_enable, "generated file does not force-enable");
    check(id_is(&c, "hslr-wait-hotkey"), "generated file names the default mod id");
    check(c.errors == 0, "generated file has no parse errors");

    DeleteFileW(path);
}

static void test_missing_file(void) {
    printf("missing file\n");
    WKConfig c;
    check(!wk_config_load(&c, L"Z:\\definitely\\not\\here.ini"), "unreadable path returns false");
}

int main(void) {
    printf("wait-hotkey configuration tests\n\n");
    test_keymap();
    test_actions();
    test_parse_basic();
    test_parse_safety();
    test_overflow();
    test_mod_id();
    test_round_trip();
    test_missing_file();
    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
