/*
 * playos-compositor/src/system_button.c — Sprint 7 system button intercept
 *
 * Registers a seat-level input listener that consumes the reserved system
 * buttons (PLAYOS_BUTTON_SYSTEM: BTN_MODE/KEY_PROG1/BTN_TRIGGER_HAPPY1;
 * PLAYOS_BUTTON_QUICK_MENU: KEY_PROG2/BTN_TRIGGER_HAPPY2) before they can
 * ever reach a Wayland client. The SYSTEM button drives the overlay
 * show/hide transition:
 *
 *   GAME_FOREGROUND                        -> PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND
 *   PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND -> GAME_FOREGROUND
 *
 * Only the raw evdev keycode is compared here — no keymap/xkb translation is
 * needed to intercept a reserved button. The game's normal input path is
 * delivered by playos-platform-api over evdev, which already strips the
 * reserved buttons; this seat listener is the Wayland-side enforcement that
 * guarantees the key never reaches a surface even if a client bound a
 * wl_seat keyboard directly.
 */

#include "compositor.h"

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

/* Per-keyboard tracking. A dedicated struct is allocated for every keyboard
 * so the key listener can be removed when the device is destroyed. */
struct playos_keyboard {
    struct playos_compositor *c;
    struct wlr_keyboard      *keyboard;
    struct wl_listener        key;
    struct wl_listener        destroy;
};

static void handle_keyboard_key(struct wl_listener *listener, void *data);
static void handle_keyboard_destroy(struct wl_listener *listener, void *data);
static void handle_new_input(struct wl_listener *listener, void *data);

/* Reserved button keycodes (Sprint 12 T5). SYSTEM and QUICK_MENU each
 * arrive on the Ally through several evdev nodes; every alias must be
 * consumed at the seat layer. Keep in sync with
 * playos-platform-api/src/backends/backend_evdev.c. */
static int
is_reserved_keycode(uint32_t keycode)
{
    switch (keycode) {
    case BTN_MODE:            /* Xbox/Guide button            */
    case KEY_PROG1:           /* Ally Armoury Crate / Home    */
    case BTN_TRIGGER_HAPPY1:  /* Ally Armoury Crate alt       */
    case KEY_PROG2:           /* Ally Command Center          */
    case BTN_TRIGGER_HAPPY2:  /* Ally Command Center alt      */
        return 1;
    default:
        return 0;
    }
}

static void
handle_keyboard_key(struct wl_listener *listener, void *data)
{
    struct playos_keyboard *pk = wl_container_of(listener, pk, key);
    struct playos_compositor *c = pk->c;
    struct wlr_keyboard_key_event *event = data;

    /* Consume every reserved button on press. It must never be forwarded
     * to a client — returning here is the whole intercept. (The compositor
     * currently forwards no keyboard keys to clients at all, so this is
     * defense at the seat layer; Landlock + group permissions deny the
     * raw /dev/input/event* devices from game processes.) */
    if (is_reserved_keycode(event->keycode) &&
        event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        wlr_log(WLR_INFO, "playos-compositor: reserved button 0x%x pressed "
                "(fg_state=%d)", event->keycode, (int)c->fg_state);

        /* SYSTEM toggles the overlay; QUICK_MENU is consumed for now
         * (a dedicated quick-menu UI is a later sprint). */
        if (event->keycode == BTN_MODE || event->keycode == KEY_PROG1 ||
            event->keycode == BTN_TRIGGER_HAPPY1) {
            if (c->fg_state == PLAYOS_FG_GAME_FOREGROUND) {
                playos_overlay_manager_show(c);
            } else if (c->fg_state ==
                       PLAYOS_FG_PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND) {
                playos_overlay_manager_hide(c);
            }
        }
        return;
    }

    /* Non-reserved keys are intentionally not forwarded yet. Full keyboard
     * focus routing is a later feature; T3 only requires that the system
     * button be consumed before any client can receive it. */
}

static void
handle_keyboard_destroy(struct wl_listener *listener, void *data)
{
    struct playos_keyboard *pk = wl_container_of(listener, pk, destroy);

    (void)data;

    wl_list_remove(&pk->key.link);
    wl_list_remove(&pk->destroy.link);
    free(pk);
}

static void
handle_new_input(struct wl_listener *listener, void *data)
{
    struct playos_compositor *c = wl_container_of(listener, c, new_input);
    struct wlr_input_device *device = data;

    if (device->type != WLR_INPUT_DEVICE_KEYBOARD)
        return;

    struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(device);

    struct playos_keyboard *pk = calloc(1, sizeof(*pk));
    if (!pk) {
        wlr_log(WLR_ERROR, "playos-compositor: out of memory "
                "tracking keyboard '%s'",
                device->name ? device->name : "(unnamed)");
        return;
    }

    pk->c        = c;
    pk->keyboard = keyboard;

    pk->key.notify = handle_keyboard_key;
    wl_signal_add(&keyboard->events.key, &pk->key);

    pk->destroy.notify = handle_keyboard_destroy;
    wl_signal_add(&device->events.destroy, &pk->destroy);

    wlr_log(WLR_INFO, "playos-compositor: keyboard '%s' attached",
            device->name ? device->name : "(unnamed)");
}

void
playos_system_button_init(struct playos_compositor *c)
{
    if (!c->backend)
        return;

    c->new_input.notify = handle_new_input;
    wl_signal_add(&c->backend->events.new_input, &c->new_input);
}
