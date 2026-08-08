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
}

bool
playos_trusted_client_claim(struct playos_compositor *c,
                            struct wl_client *client,
                            enum playos_trusted_role role)
{
    /* Check if role already taken */
    if (role == PLAYOS_ROLE_SHELL && c->shell_client) {
        wlr_log(WLR_ERROR, "trusted: shell role already taken");
        return false;
    }
    if (role == PLAYOS_ROLE_OVERLAY && c->overlay_client) {
        wlr_log(WLR_ERROR, "trusted: overlay role already taken");
        return false;
    }

    /* Assign role */
    if (role == PLAYOS_ROLE_SHELL) {
        c->shell_client = client;
        wlr_log(WLR_INFO, "trusted: shell client registered (pid %d)",
                wl_client_get_fd(client));
    } else if (role == PLAYOS_ROLE_OVERLAY) {
        c->overlay_client = client;
        wlr_log(WLR_INFO, "trusted: overlay client registered (pid %d)",
                wl_client_get_fd(client));
    }

    return true;
}

bool
playos_trusted_client_is_trusted(struct playos_compositor *c,
                                 struct wl_client *client)
{
    return (client == c->shell_client || client == c->overlay_client);
}
