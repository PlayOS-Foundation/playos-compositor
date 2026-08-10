/*
 * playos-compositor/src/playos_manager.c — Server-side playos-v1 protocol
 *
 * Implements the playos_manager_v1 global, handling trusted client
 * registration requests (register_shell, register_overlay).
 */

#include "compositor.h"
#include "playos-v1-protocol.h"
#include <stdlib.h>
#include <wlr/util/log.h>

/* Per-global data: a single compositor pointer for all bound clients */
struct playos_manager_data {
	struct playos_compositor *compositor;
};

/* ── Forward declarations ───────────────────────────────────────── */

static void
playos_manager_register_shell(struct wl_client *client,
                              struct wl_resource *resource);
static void
playos_manager_register_overlay(struct wl_client *client,
                                struct wl_resource *resource);

/* ── vtable ─────────────────────────────────────────────────────── */

static const struct playos_manager_v1_interface
playos_manager_implementation = {
	.register_shell   = playos_manager_register_shell,
	.register_overlay = playos_manager_register_overlay,
};

/* ── Bind handler — called when a client binds playos_manager_v1 ─── */

static void
playos_manager_bind(struct wl_client *client, void *data,
                    uint32_t version, uint32_t id)
{
	struct playos_manager_data *d = data;
	struct wl_resource *resource =
		wl_resource_create(client, &playos_manager_v1_interface,
		                   version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource,
	                               &playos_manager_implementation,
	                               d, NULL);
}

/* ── Request: register_shell ────────────────────────────────────── */

static void
playos_manager_register_shell(struct wl_client *client,
                              struct wl_resource *resource)
{
	struct playos_manager_data *d = wl_resource_get_user_data(resource);
	struct playos_compositor *c = d->compositor;

	if (!playos_trusted_client_claim(c, client, PLAYOS_ROLE_SHELL)) {
		wl_resource_post_error(resource,
		                       PLAYOS_MANAGER_V1_ERROR_ROLE_ALREADY_TAKEN,
		                       "shell role already taken");
		return;
	}

	wlr_log(WLR_INFO, "playos_manager: shell client registered");
}

/* ── Request: register_overlay ──────────────────────────────────── */

static void
playos_manager_register_overlay(struct wl_client *client,
                                struct wl_resource *resource)
{
	struct playos_manager_data *d = wl_resource_get_user_data(resource);
	struct playos_compositor *c = d->compositor;

	if (!playos_trusted_client_claim(c, client, PLAYOS_ROLE_OVERLAY)) {
		wl_resource_post_error(resource,
		                       PLAYOS_MANAGER_V1_ERROR_ROLE_ALREADY_TAKEN,
		                       "overlay role already taken");
		return;
	}

	wlr_log(WLR_INFO, "playos_manager: overlay client registered");
}

/* ── Public: create the global ──────────────────────────────────── */

int
playos_manager_create(struct playos_compositor *c)
{
	struct playos_manager_data *data = calloc(1, sizeof(*data));
	if (!data)
		return -1;

	data->compositor = c;

	struct wl_global *global = wl_global_create(
		c->display, &playos_manager_v1_interface, 1, data,
		playos_manager_bind);

	if (!global) {
		free(data);
		wlr_log(WLR_ERROR, "playos_manager: failed to create global");
		return -1;
	}

	wlr_log(WLR_INFO, "playos_manager: global created");
	return 0;
}
