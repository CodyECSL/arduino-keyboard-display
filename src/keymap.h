#pragma once

#include "hid.h"

typedef enum { OS_LINUX = 0, OS_MAC = 1 } host_os_t;

typedef enum {
    APP_COPY,
    APP_PASTE,
    APP_UNDO,
    APP_REDO,
    APP_SWITCH,
    APP_SCREENSHOT,
    APP_COUNT,
} app_button_t;

/** Prompt run by the AI Mode key. Also shown on the key. */
#define AI_PROMPT "Create my website and make no mistakes"

/** Host OS the shortcuts are built for. Stored in flash (settings). Default: Linux. */
void keymap_set_os(host_os_t os);
host_os_t keymap_get_os(void);

/** Shortcut macro for an Apps page button, for the current host OS. */
const hid_macro_t *keymap_app(app_button_t button);

/** AI Mode: open a terminal and run `claude -p "<AI_PROMPT>"`. */
const hid_macro_t *keymap_ai_mode(void);
