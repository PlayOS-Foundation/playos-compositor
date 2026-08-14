/*
 * playos-compositor/src/state_machine.c — Sprint 7 foreground state machine
 *
 * Tracks which surface is foreground: shell, game, or PlayOS overlay UI.
 * Emits CompositorStateChanged over compositor.sock IPC to playos-init.
 */

#include "compositor.h"
#include <stdio.h>
#include <string.h>
#include <wlr/util/log.h>

/* ── State name mapping (must match the IPC wire spec) ──────────── */

static const char *
foreground_state_name(enum playos_foreground_state state)
{
    switch (state) {
    case PLAYOS_FG_SHELL_FOREGROUND:
        return "SHELL_FOREGROUND";
    case PLAYOS_FG_GAME_STARTING:
        return "GAME_STARTING";
    case PLAYOS_FG_GAME_FOREGROUND:
        return "GAME_FOREGROUND";
    case PLAYOS_FG_PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND:
        return "PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND";
    case PLAYOS_FG_TERMINATING_GAME:
        return "TERMINATING_GAME";
    }
    return "UNKNOWN";
}

static void
set_tree_enabled(struct wlr_scene_tree *tree, bool enabled)
{
    if (!tree)
        return;
    wlr_scene_node_set_enabled(&tree->node, enabled);
}

void
playos_state_refresh(struct playos_compositor *c)
{
    bool shell_visible =
        c->fg_state == PLAYOS_FG_SHELL_FOREGROUND ||
        c->fg_state == PLAYOS_FG_GAME_STARTING ||
        c->fg_state == PLAYOS_FG_TERMINATING_GAME;

    bool game_visible =
        c->fg_state == PLAYOS_FG_GAME_FOREGROUND ||
        c->fg_state == PLAYOS_FG_PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND;

    bool overlay_visible =
        c->overlay_visible &&
        c->fg_state == PLAYOS_FG_PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND;

    set_tree_enabled(c->shell_tree, shell_visible);
    set_tree_enabled(c->game_tree, game_visible);
    set_tree_enabled(c->overlay_tree, overlay_visible);
}

void
playos_state_init(struct playos_compositor *c)
{
    c->fg_state                 = PLAYOS_FG_SHELL_FOREGROUND;
    c->expected_launch_token[0] = '\0';
    c->expected_game_id[0]      = '\0';
    c->shell_tree               = NULL;
    c->game_tree                = NULL;
    c->overlay_tree             = NULL;
    c->game_surface_ready       = false;
    c->overlay_visible          = false;
}

void
playos_state_transition(struct playos_compositor *c,
                        enum playos_foreground_state state)
{
    if (c->fg_state == state)
        return;

    const char *old_name = foreground_state_name(c->fg_state);
    const char *new_name = foreground_state_name(state);

    c->fg_state = state;
    wlr_log(WLR_INFO, "playos-compositor: foreground %s -> %s",
            old_name, new_name);

    playos_state_refresh(c);

    char extra[160];
    snprintf(extra, sizeof(extra), "\"state\":\"%s\"", new_name);
    playos_compositor_ipc_send(c, PLAYOS_COMPOSITOR_MSG_STATE_CHANGED, extra);
}

void
playos_state_handle_set_expected_game(struct playos_compositor *c,
                                      const char *launch_token,
                                      const char *game_id)
{
    snprintf(c->expected_launch_token, sizeof(c->expected_launch_token),
             "%s", launch_token ? launch_token : "");
    snprintf(c->expected_game_id, sizeof(c->expected_game_id),
             "%s", game_id ? game_id : "");
    c->game_surface_ready = false;

    wlr_log(WLR_INFO, "playos-compositor: expected game '%s' (token %s)",
            c->expected_game_id, c->expected_launch_token);

    playos_state_transition(c, PLAYOS_FG_GAME_STARTING);
}

void
playos_state_handle_clear_expected(struct playos_compositor *c)
{
    c->expected_launch_token[0] = '\0';
    c->expected_game_id[0]      = '\0';
    c->game_surface_ready       = false;

    if (c->fg_state == PLAYOS_FG_GAME_STARTING ||
        c->fg_state == PLAYOS_FG_GAME_FOREGROUND ||
        c->fg_state == PLAYOS_FG_PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND) {
        playos_state_transition(c, PLAYOS_FG_SHELL_FOREGROUND);
    }
}

void
playos_state_handle_force_terminate(struct playos_compositor *c)
{
    wlr_log(WLR_INFO, "playos-compositor: force-terminating game");

    playos_state_transition(c, PLAYOS_FG_TERMINATING_GAME);
    c->expected_launch_token[0] = '\0';
    c->expected_game_id[0]      = '\0';
    c->game_surface_ready       = false;
}

void
playos_state_game_surface_ready(struct playos_compositor *c)
{
    if (c->game_surface_ready)
        return;
    if (c->fg_state != PLAYOS_FG_GAME_STARTING)
        return;

    c->game_surface_ready = true;

    char extra[192];
    snprintf(extra, sizeof(extra), "\"launch_token\":\"%s\"",
             c->expected_launch_token);
    playos_compositor_ipc_send(c, PLAYOS_COMPOSITOR_MSG_GAME_SURFACE_READY,
                               extra);

    playos_state_transition(c, PLAYOS_FG_GAME_FOREGROUND);
}

void
playos_state_game_exited(struct playos_compositor *c, bool crashed)
{
    /* GameExited is emitted by playos-init (it reaps the child). The
     * compositor's job is to fall back to the shell and clear state. */
    (void)crashed;

    if (c->fg_state == PLAYOS_FG_GAME_STARTING ||
        c->fg_state == PLAYOS_FG_GAME_FOREGROUND ||
        c->fg_state == PLAYOS_FG_PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND ||
        c->fg_state == PLAYOS_FG_TERMINATING_GAME) {
        playos_state_transition(c, PLAYOS_FG_SHELL_FOREGROUND);
    }

    c->expected_launch_token[0] = '\0';
    c->expected_game_id[0]      = '\0';
    c->game_surface_ready       = false;
}
