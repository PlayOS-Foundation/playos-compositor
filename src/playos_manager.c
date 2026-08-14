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

/* Promotes a client's already-bound playos_overlay_v1 resource to the
 * compositor's overlay_resource once that client has claimed the overlay
 * role via register_overlay. Defined below with the overlay global. */
static void playos_overlay_resolve_resource(struct playos_compositor *c,
                                            struct wl_client *client);

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

	/* Promote the overlay's already-bound playos_overlay_v1 resource so
	 * about_to_show/about_to_hide/output_info events target the correct
	 * client (the overlay binds the global before it registers). */
	playos_overlay_resolve_resource(c, client);

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

/* ─────────────────────────────────────────────────────────────────── */
/* playos_overlay_v1 — second global for the trusted overlay client    */
/*                                                                     */
/* Mirrors playos_manager_v1's global pattern: each bound resource     */
/* carries a small per-global data block pointing at the compositor.   */
/* The compositor already tracks the overlay toplevel by role, so the  */
/* overlay's set_surface/surface_ready requests are informational only; */
/* request_dismiss maps directly onto overlay_manager_hide().          */
/* ─────────────────────────────────────────────────────────────────── */

struct playos_overlay_data {
	struct playos_compositor *compositor;
};

/* One entry per client that has bound playos_overlay_v1. The overlay
 * binds this global *before* it claims the overlay role via
 * playos_manager_v1.register_overlay, and other raylib clients (shell,
 * games) bind it too through the shared backend — so a binding cannot be
 * promoted to overlay_resource at bind time. We remember every binding
 * and promote the matching one once the role is claimed. */
struct playos_overlay_bound {
	struct wl_client          *client;
	struct wl_resource        *resource;
	struct playos_compositor  *compositor;
	struct wl_list             link;   /* playos_compositor::overlay_resources */
};

static void
playos_overlay_resolve_resource(struct playos_compositor *c,
                                struct wl_client *client)
{
	struct playos_overlay_bound *bound;

	wl_list_for_each(bound, &c->overlay_resources, link) {
		if (bound->client == client) {
			c->overlay_resource = bound->resource;
			/* Deliver output geometry now that this is the canonical
			 * overlay resource. */
			playos_overlay_send_output_info(c);
			return;
		}
	}
}

static void
playos_overlay_set_surface(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *surface);
static void
playos_overlay_surface_ready(struct wl_client *client,
                             struct wl_resource *resource);
static void
playos_overlay_request_dismiss(struct wl_client *client,
                               struct wl_resource *resource);

static const struct playos_overlay_v1_interface
playos_overlay_implementation = {
	.set_surface     = playos_overlay_set_surface,
	.surface_ready   = playos_overlay_surface_ready,
	.request_dismiss = playos_overlay_request_dismiss,
};

static void
playos_overlay_resource_destroy(struct wl_resource *resource)
{
	struct playos_overlay_bound *bound = wl_resource_get_user_data(resource);
	if (!bound)
		return;

	if (bound->compositor->overlay_resource == resource)
		bound->compositor->overlay_resource = NULL;

	wl_list_remove(&bound->link);
	free(bound);
}

static void
playos_overlay_bind(struct wl_client *client, void *data,
                    uint32_t version, uint32_t id)
{
	struct playos_overlay_data *d = data;

	struct playos_overlay_bound *bound = calloc(1, sizeof(*bound));
	if (!bound) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wl_resource *resource =
		wl_resource_create(client, &playos_overlay_v1_interface,
		                   version, id);
	if (!resource) {
		free(bound);
		wl_client_post_no_memory(client);
		return;
	}

	bound->client     = client;
	bound->resource   = resource;
	bound->compositor = d->compositor;
	wl_list_insert(&d->compositor->overlay_resources, &bound->link);

	wl_resource_set_implementation(resource,
	                               &playos_overlay_implementation,
	                               bound, playos_overlay_resource_destroy);

	/* Re-bind / restart path: if this client already holds the overlay
	 * role, promote its fresh binding immediately. The normal first-bind
	 * path is handled by register_overlay → resolve_resource(). */
	if (d->compositor->overlay_client == client)
		playos_overlay_resolve_resource(d->compositor, client);
}

static void
playos_overlay_set_surface(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *surface)
{
	(void)surface;
	struct playos_overlay_bound *bound = wl_resource_get_user_data(resource);
	struct playos_compositor *c = bound->compositor;

	if (client != c->overlay_client) {
		wlr_log(WLR_ERROR, "playos_overlay: set_surface from non-overlay client");
		return;
	}

	wlr_log(WLR_INFO, "playos_overlay: surface set");
}

static void
playos_overlay_surface_ready(struct wl_client *client,
                             struct wl_resource *resource)
{
	struct playos_overlay_bound *bound = wl_resource_get_user_data(resource);
	struct playos_compositor *c = bound->compositor;

	if (client != c->overlay_client) {
		wlr_log(WLR_ERROR, "playos_overlay: surface_ready from non-overlay client");
		return;
	}

	wlr_log(WLR_INFO, "playos_overlay: surface ready");
}

static void
playos_overlay_request_dismiss(struct wl_client *client,
                               struct wl_resource *resource)
{
	struct playos_overlay_bound *bound = wl_resource_get_user_data(resource);
	struct playos_compositor *c = bound->compositor;

	if (client != c->overlay_client) {
		wlr_log(WLR_ERROR, "playos_overlay: request_dismiss from non-overlay client");
		return;
	}

	playos_overlay_manager_hide(c);
}

/* ── Event helpers ──────────────────────────────────────────────── */

void
playos_overlay_send_about_to_show(struct playos_compositor *c)
{
	if (c->overlay_resource)
		playos_overlay_v1_send_about_to_show(c->overlay_resource);
}

void
playos_overlay_send_about_to_hide(struct playos_compositor *c)
{
	if (c->overlay_resource)
		playos_overlay_v1_send_about_to_hide(c->overlay_resource);
}

void
playos_overlay_send_output_info(struct playos_compositor *c)
{
	if (c->overlay_resource)
		playos_overlay_v1_send_output_info(c->overlay_resource,
		                                   c->output_width,
		                                   c->output_height,
		                                   (uint32_t)c->output_refresh_mhz,
		                                   (uint32_t)c->output_scale_100);
}

/* ── Public: create the global ──────────────────────────────────── */

int
playos_overlay_create(struct playos_compositor *c)
{
	struct playos_overlay_data *data = calloc(1, sizeof(*data));
	if (!data)
		return -1;

	data->compositor = c;

	struct wl_global *global = wl_global_create(
		c->display, &playos_overlay_v1_interface, 1, data,
		playos_overlay_bind);

	if (!global) {
		free(data);
		wlr_log(WLR_ERROR, "playos_overlay: failed to create global");
		return -1;
	}

	wlr_log(WLR_INFO, "playos_overlay: global created");
	return 0;
}
