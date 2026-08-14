/*
 * playos-compositor/src/overlay_manager.c — Sprint 7 overlay visibility
 *
 * The overlay is shown/hidden by playos-init over compositor.sock IPC
 * (ShowOverlay / HideOverlay). Visibility itself is applied by the
 * foreground state machine; this module only owns the requested-visibility
 * flag and the transitions in/out of the PlayOS UI state.
 */

#include "compositor.h"
#include <wlr/util/log.h>

void
playos_overlay_manager_show(struct playos_compositor *c)
{
    if (c->overlay_visible)
        return;

    c->overlay_visible = true;
    wlr_log(WLR_INFO, "playos-compositor: overlay shown");

    /* Push current output geometry + visibility to the overlay client so it
     * can lay out and render before the compositor maps its surface. */
    playos_overlay_send_output_info(c);
    playos_overlay_send_about_to_show(c);

    if (c->fg_state == PLAYOS_FG_GAME_FOREGROUND) {
        playos_state_transition(c,
            PLAYOS_FG_PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND);
    } else {
        playos_state_refresh(c);
    }
}

void
playos_overlay_manager_hide(struct playos_compositor *c)
{
    if (!c->overlay_visible)
        return;

    c->overlay_visible = false;
    wlr_log(WLR_INFO, "playos-compositor: overlay hidden");

    playos_overlay_send_about_to_hide(c);

    if (c->fg_state == PLAYOS_FG_PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND) {
        enum playos_foreground_state next =
            c->game_tree ? PLAYOS_FG_GAME_FOREGROUND
                         : PLAYOS_FG_SHELL_FOREGROUND;
        playos_state_transition(c, next);
    } else {
        playos_state_refresh(c);
    }
}
