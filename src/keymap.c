#include "keymap.h"

#include "class/hid/hid.h"
#include "settings.h"

#define MACRO(arr) {.actions = (arr), .count = sizeof(arr) / sizeof((arr)[0])}

/* Apps: Cmd on macOS, Ctrl on Linux for the edit shortcuts. */
#define APP_MAC(name, ...) static const hid_action_t name##_mac[] = {__VA_ARGS__}
#define APP_LINUX(name, ...) static const hid_action_t name##_linux[] = {__VA_ARGS__}

APP_MAC(copy, HID_COMBO(HID_MOD_GUI, HID_KEY_C));
APP_LINUX(copy, HID_COMBO(HID_MOD_CTRL, HID_KEY_C));
APP_MAC(paste, HID_COMBO(HID_MOD_GUI, HID_KEY_V));
APP_LINUX(paste, HID_COMBO(HID_MOD_CTRL, HID_KEY_V));
APP_MAC(undo, HID_COMBO(HID_MOD_GUI, HID_KEY_Z));
APP_LINUX(undo, HID_COMBO(HID_MOD_CTRL, HID_KEY_Z));
APP_MAC(redo, HID_COMBO(HID_MOD_GUI | HID_MOD_SHIFT, HID_KEY_Z));
APP_LINUX(redo, HID_COMBO(HID_MOD_CTRL | HID_MOD_SHIFT, HID_KEY_Z));
/* Mac: Mission Control. Linux: Alt+Tab. */
APP_MAC(sw, HID_COMBO(HID_MOD_CTRL, HID_KEY_ARROW_UP));
APP_LINUX(sw, HID_COMBO(HID_MOD_ALT, HID_KEY_TAB));
/* Mac: region screenshot. Linux: Print Screen (KDE Spectacle default). */
APP_MAC(shot, HID_COMBO(HID_MOD_GUI | HID_MOD_SHIFT, HID_KEY_4));
APP_LINUX(shot, HID_COMBO(0, HID_KEY_PRINT_SCREEN));

static const hid_macro_t s_apps[APP_COUNT][2] = {
    [APP_COPY] = {MACRO(copy_linux), MACRO(copy_mac)},
    [APP_PASTE] = {MACRO(paste_linux), MACRO(paste_mac)},
    [APP_UNDO] = {MACRO(undo_linux), MACRO(undo_mac)},
    [APP_REDO] = {MACRO(redo_linux), MACRO(redo_mac)},
    [APP_SWITCH] = {MACRO(sw_linux), MACRO(sw_mac)},
    [APP_SCREENSHOT] = {MACRO(shot_linux), MACRO(shot_mac)},
};

/* AI Mode. The terminal launch differs per OS; the rest is shared. */
#define AI_TAIL                                              \
    HID_DELAY(900), /* wait for the terminal */              \
    HID_TYPE("claude -p \"" AI_PROMPT "\""),                 \
    HID_COMBO(0, HID_KEY_ENTER)

static const hid_action_t s_ai_linux[] = {
    HID_COMBO(HID_MOD_CTRL | HID_MOD_ALT, HID_KEY_T), /* open terminal */
    AI_TAIL,
};
static const hid_action_t s_ai_mac[] = {
    HID_COMBO(HID_MOD_GUI, HID_KEY_SPACE), /* Spotlight */
    HID_DELAY(500),
    HID_TYPE("terminal"),
    HID_COMBO(0, HID_KEY_ENTER),
    AI_TAIL,
};
static const hid_macro_t s_ai[2] = {MACRO(s_ai_linux), MACRO(s_ai_mac)};

void keymap_set_os(host_os_t os) { settings_set_host_os(os == OS_MAC); }
host_os_t keymap_get_os(void) { return settings_get_host_os() ? OS_MAC : OS_LINUX; }

const hid_macro_t *keymap_app(app_button_t button)
{
    if ((int)button < 0 || button >= APP_COUNT) {
        return NULL;
    }
    return &s_apps[button][keymap_get_os() == OS_MAC];
}

const hid_macro_t *keymap_ai_mode(void)
{
    return &s_ai[keymap_get_os() == OS_MAC];
}
