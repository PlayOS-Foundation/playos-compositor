#include "compositor.h"
#include <wlr/util/log.h>
#include <string.h>

/**
 * Temporary trusted-shell identity mechanism for Sprint 2.
 * The compositor identifies the trusted client via the PLAYOS_TRUSTED_ROLE
 * environment variable set by playos-init.
 *
 * This mechanism will be replaced by a proper IPC-based handshake
 * in Sprint 6/7.
 */

void
playos_trusted_client_init(struct playos_compositor *c)
{
    c->shell_client   = NULL;
    c->overlay_client = NULL;
    c->pending_role   = PLAYOS_ROLE_NONE;
    c->shell_client_destroy.notify   = playos_trusted_shell_client_gone;
    c->overlay_client_destroy.notify = playos_trusted_overlay_client_gone;
}

/* A trusted client can die at any time (the shell crashed on hardware and the
 * supervisor restarted it 250 ms later). The role has to be released with the
 * connection or the replacement process is rejected with "role already taken"
 * and runs untrusted for the rest of the session. */
void
playos_trusted_shell_client_gone(struct wl_listener *listener, void *data)
{
    struct playos_compositor *c =
        wl_container_of(listener, c, shell_client_destroy);
    (void)data;
    wlr_log(WLR_INFO, "trusted: shell client disconnected - releasing role");
    c->shell_client = NULL;
    wl_list_remove(&listener->link);
    wl_list_init(&listener->link);
}

void
playos_trusted_overlay_client_gone(struct wl_listener *listener, void *data)
{
    struct playos_compositor *c =
        wl_container_of(listener, c, overlay_client_destroy);
    (void)data;
    wlr_log(WLR_INFO, "trusted: overlay client disconnected - releasing role");
    c->overlay_client = NULL;
    wl_list_remove(&listener->link);
    wl_list_init(&listener->link);
}

bool
playos_trusted_client_claim(struct playos_compositor *c,
                            struct wl_client *client,
                            enum playos_trusted_role role)
{
    /* Check if role already taken */
    if (role == PLAYOS_ROLE_SHELL && c->shell_client) {
        /* Log who still holds it: after an installer handoff this should never
         * be reachable (the shell has exited and its client was destroyed), so
         * a hit here means the old client outlived its process. */
        pid_t holder_pid = 0;
        uid_t holder_uid = 0;
        gid_t holder_gid = 0;
        wl_client_get_credentials(c->shell_client, &holder_pid, &holder_uid,
                                  &holder_gid);
        wlr_log(WLR_ERROR, "trusted: shell role already taken (holder pid %d)",
                (int)holder_pid);
        return false;
    }
    if (role == PLAYOS_ROLE_OVERLAY && c->overlay_client) {
        wlr_log(WLR_ERROR, "trusted: overlay role already taken");
        return false;
    }

    /* Assign role */
    if (role == PLAYOS_ROLE_SHELL) {
        c->shell_client = client;
        wl_client_add_destroy_listener(client, &c->shell_client_destroy);
        wlr_log(WLR_INFO, "trusted: shell client registered (pid %d)",
                wl_client_get_fd(client));
    } else if (role == PLAYOS_ROLE_OVERLAY) {
        c->overlay_client = client;
        wl_client_add_destroy_listener(client, &c->overlay_client_destroy);
        wlr_log(WLR_INFO, "trusted: overlay client registered (pid %d)",
                wl_client_get_fd(client));
    }

    /* The client may have created its Wayland surface (and been classified
     * as a game) before claiming its role — e.g. the shell maps its window
     * before calling register_shell. Re-run classification now. */
    playos_compositor_reclassify_toplevels(c);

    return true;
}

bool
playos_trusted_client_is_trusted(struct playos_compositor *c,
                                 struct wl_client *client)
{
    return (client == c->shell_client || client == c->overlay_client);
}
