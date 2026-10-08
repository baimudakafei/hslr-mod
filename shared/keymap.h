/* Wait-hotkey key name table.
 * Maps configuration key names to UnityEngine.InputSystem.Key numeric values
 * for HSLR 2.2.4 (IL2CPP, Steam build 25767968).
 *
 * Values 41-58, 82 and 84 are cross-checked against the reference
 * implementation's audited constants (Digit1=41, Digit0=50, LeftShift=51,
 * NumpadPeriod=82, Numpad0=84). The remaining values follow the documented
 * Input System enum order, which is contiguous and stable.
 */
#ifndef HSLR_WAITKEY_KEYMAP_H
#define HSLR_WAITKEY_KEYMAP_H

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *name;
    int code;
} WKKeyName;

#define WK_LETTERS                                                                   \
    {"a",15},{"b",16},{"c",17},{"d",18},{"e",19},{"f",20},{"g",21},{"h",22},        \
    {"i",23},{"j",24},{"k",25},{"l",26},{"m",27},{"n",28},{"o",29},{"p",30},        \
    {"q",31},{"r",32},{"s",33},{"t",34},{"u",35},{"v",36},{"w",37},{"x",38},        \
    {"y",39},{"z",40}

#define WK_TOP_DIGITS                                                                \
    {"digit1",41},{"digit2",42},{"digit3",43},{"digit4",44},{"digit5",45},          \
    {"digit6",46},{"digit7",47},{"digit8",48},{"digit9",49},{"digit0",50}

#define WK_NUMPAD_DIGITS                                                             \
    {"numpad0",84},{"numpad1",85},{"numpad2",86},{"numpad3",87},{"numpad4",88},     \
    {"numpad5",89},{"numpad6",90},{"numpad7",91},{"numpad8",92},{"numpad9",93}

#define WK_FUNCTION_KEYS                                                             \
    {"f1",94},{"f2",95},{"f3",96},{"f4",97},{"f5",98},{"f6",99},                    \
    {"f7",100},{"f8",101},{"f9",102},{"f10",103},{"f11",104},{"f12",105}

/* Player-facing names only: these are what the .ini accepts. Aliases point at
 * the same code, so the table may contain duplicates by code. */
static const WKKeyName wk_key_names[] = {
    {"space",1},{"空格",1},{"空格键",1},
    {"enter",2},{"回车",2},{"numpadenter",77},
    {"tab",3},
    {"backquote",4},{"quote",5},{"semicolon",6},{"comma",7},{"period",8},
    {"slash",9},{"backslash",10},{"leftbracket",11},{"rightbracket",12},
    {"minus",13},{"equals",14},
    WK_LETTERS,
    WK_TOP_DIGITS,
    {"leftshift",51},{"rightshift",52},{"shift",51},
    {"leftalt",53},{"rightalt",54},{"alt",53},
    {"leftctrl",55},{"rightctrl",56},{"ctrl",55},{"control",55},
    {"leftmeta",57},{"rightmeta",58},{"meta",57},{"win",57},
    {"contextmenu",59},
    {"escape",60},{"esc",60},
    {"leftarrow",61},{"rightarrow",62},{"uparrow",63},{"downarrow",64},
    {"left",61},{"right",62},{"up",63},{"down",64},
    {"backspace",65},{"pagedown",66},{"pageup",67},{"home",68},{"end",69},
    {"insert",70},{"delete",71},{"capslock",72},{"numlock",73},
    {"printscreen",74},{"scrolllock",75},{"pause",76},
    {"numpaddivide",78},{"numpadmultiply",79},{"numpadplus",80},
    {"numpadminus",81},{"numpadperiod",82},{"numpadequals",83},
    {"numpaddecimal",82},
    WK_NUMPAD_DIGITS,
    WK_FUNCTION_KEYS,
};

/* Copy at most n-1 bytes and lower-case ASCII in place. CJK bytes are >= 0x80
 * and pass through untouched. */
static void wk_lower_into(char *dst, size_t n, const char *src) {
    size_t i = 0;
    if (!n) return;
    for (; src && src[i] && i + 1 < n; ++i) {
        char c = src[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    dst[i] = '\0';
}

/* Return the Unity Key code for a configuration name, or -1 if unknown. */
static int wk_key_code(const char *name) {
    char lowered[64];
    if (!name || !*name) return -1;
    wk_lower_into(lowered, sizeof(lowered), name);
    for (size_t i = 0; i < sizeof(wk_key_names) / sizeof(wk_key_names[0]); ++i)
        if (!strcmp(wk_key_names[i].name, lowered)) return wk_key_names[i].code;
    return -1;
}

#endif
