/*
 * playos-compositor/src/compositor_ipc.c — Sprint 7 compositor.sock client
 *
 * The compositor is the CLIENT on /run/playos/compositor.sock; playos-init
 * is the server. Frames use the shared PlayOS framing: 4-byte magic "PLOS"
 * (0x504C4F53, little-endian), 4-byte little-endian body length, then a
 * JSON body. This file is intentionally self-contained so the compositor
 * does not need to link against playos-init's ipc library.
 */

#include "compositor.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wlr/util/log.h>

#define IPC_SOCK_PATH "/run/playos/compositor.sock"
#define IPC_MAGIC      0x504C4F53U
#define IPC_MAX_BODY   65536U
#define IPC_MAX_FRAME  (8 + IPC_MAX_BODY)

static int ipc_fd_handler(int fd, uint32_t mask, void *data);
static void ipc_schedule_reconnect(struct playos_compositor *c);

/* ── Little-endian helpers ──────────────────────────────────────── */

static uint32_t
le32_load(const unsigned char *p)
{
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void
le32_store(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

/* ── Minimal JSON string extraction ────────────────────────────── */

static int
json_get_string(const char *raw, size_t len, const char *key,
                char *out, size_t out_sz)
{
    const char *end     = raw + len;
    const char *p       = raw;
    size_t      key_len = strlen(key);

    if (out_sz == 0)
        return -1;

    while (p < end) {
        p = (const char *)memchr(p, '"', (size_t)(end - p));
        if (!p)
            return -1;

        if ((size_t)(end - p) >= key_len + 2 &&
            p[key_len + 1] == '"' &&
            strncmp(p + 1, key, key_len) == 0) {
            p += key_len + 2;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
                p++;
            if (p >= end || *p != ':')
                return -1;
            p++;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
                p++;
            if (p >= end || *p != '"')
                return -1;
            p++;

            const char *start = p;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end)
                    p++;
                p++;
            }
            if (p >= end)
                return -1;

            size_t vlen = (size_t)(p - start);
            if (vlen >= out_sz)
                vlen = out_sz - 1;
            memcpy(out, start, vlen);
            out[vlen] = '\0';
            return 0;
        }
        p++;
    }
    return -1;
}

/* ── Frame send ─────────────────────────────────────────────────── */

static int
frame_send(int fd, const char *type, const char *extra_json)
{
    unsigned char buf[IPC_MAX_FRAME];
    char         *body = (char *)buf + 8;
    int           body_len;

    if (extra_json && extra_json[0])
        body_len = snprintf(body, IPC_MAX_BODY,
                            "{\"v\":1,\"type\":\"%s\",%s}", type, extra_json);
    else
        body_len = snprintf(body, IPC_MAX_BODY,
                            "{\"v\":1,\"type\":\"%s\"}", type);

    if (body_len < 0 || body_len >= (int)IPC_MAX_BODY)
        return -1;

    le32_store(buf, IPC_MAGIC);
    le32_store(buf + 4, (uint32_t)body_len);

    size_t  total = 8 + (size_t)body_len;
    ssize_t rc    = send(fd, buf, total, 0);
    if (rc < 0 && errno == EINTR)
        rc = send(fd, buf, total, 0);
    if (rc < 0)
        return -1;
    if ((size_t)rc != total) {
        wlr_log(WLR_ERROR, "compositor ipc: short send (%zd/%zu)", rc, total);
        return -1;
    }
    return 0;
}

/* ── Dispatch ───────────────────────────────────────────────────── */

static void
ipc_dispatch(struct playos_compositor *c, const char *body, size_t len)
{
    char type[128];

    if (json_get_string(body, len, "type", type, sizeof(type)) != 0) {
        wlr_log(WLR_ERROR, "compositor ipc: message missing 'type'");
        return;
    }

    if (strcmp(type, PLAYOS_COMPOSITOR_MSG_SET_EXPECTED_GAME) == 0) {
        char launch_token[64];
        char game_id[256];
        json_get_string(body, len, "launch_token", launch_token,
                        sizeof(launch_token));
        json_get_string(body, len, "game_id", game_id, sizeof(game_id));
        playos_state_handle_set_expected_game(c, launch_token, game_id);
    } else if (strcmp(type, PLAYOS_COMPOSITOR_MSG_CLEAR_EXPECTED_GAME) == 0) {
        playos_state_handle_clear_expected(c);
    } else if (strcmp(type, PLAYOS_COMPOSITOR_MSG_FORCE_TERMINATE_GAME) == 0) {
        playos_state_handle_force_terminate(c);
    } else if (strcmp(type, PLAYOS_COMPOSITOR_MSG_SHOW_OVERLAY) == 0) {
        playos_overlay_manager_show(c);
    } else if (strcmp(type, PLAYOS_COMPOSITOR_MSG_HIDE_OVERLAY) == 0) {
        playos_overlay_manager_hide(c);
    } else {
        wlr_log(WLR_INFO, "compositor ipc: ignoring message type '%s'", type);
    }
}

/* ── Connect / reconnect ────────────────────────────────────────── */

static int
ipc_connect(void)
{
    struct sockaddr_un addr;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(IPC_SOCK_PATH) >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, IPC_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (const struct sockaddr *)&addr,
                (socklen_t)sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void
ipc_disconnect(struct playos_compositor *c)
{
    if (c->ipc_source) {
        wl_event_source_remove(c->ipc_source);
        c->ipc_source = NULL;
    }
    if (c->compositor_sock_fd >= 0) {
        close(c->compositor_sock_fd);
        c->compositor_sock_fd = -1;
    }
}

static int
ipc_reconnect_handler(void *data)
{
    struct playos_compositor *c = data;
    int fd = ipc_connect();

    if (fd >= 0) {
        c->compositor_sock_fd = fd;
        c->ipc_source = wl_event_loop_add_fd(c->event_loop, fd,
                                              WL_EVENT_READABLE,
                                              ipc_fd_handler, c);
        c->ipc_reconnect_delay_ms = 100;
        if (c->ipc_reconnect_timer) {
            wl_event_source_remove(c->ipc_reconnect_timer);
            c->ipc_reconnect_timer = NULL;
        }
        wlr_log(WLR_INFO, "compositor ipc: connected to %s", IPC_SOCK_PATH);
    } else {
        if (c->ipc_reconnect_delay_ms < 5000)
            c->ipc_reconnect_delay_ms *= 2;
        if (c->ipc_reconnect_delay_ms > 5000)
            c->ipc_reconnect_delay_ms = 5000;
        wl_event_source_timer_update(c->ipc_reconnect_timer,
                                     c->ipc_reconnect_delay_ms);
    }
    return 0;
}

static void
ipc_schedule_reconnect(struct playos_compositor *c)
{
    if (c->ipc_reconnect_timer)
        return;

    wlr_log(WLR_INFO, "compositor ipc: %s unavailable, retrying in %d ms",
            IPC_SOCK_PATH, c->ipc_reconnect_delay_ms);
    c->ipc_reconnect_timer = wl_event_loop_add_timer(c->event_loop,
                                                      ipc_reconnect_handler,
                                                      c);
    wl_event_source_timer_update(c->ipc_reconnect_timer,
                                 c->ipc_reconnect_delay_ms);
}

static int
ipc_fd_handler(int fd, uint32_t mask, void *data)
{
    struct playos_compositor *c = data;

    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        ipc_disconnect(c);
        ipc_schedule_reconnect(c);
        return 0;
    }
    if (!(mask & WL_EVENT_READABLE))
        return 0;

    unsigned char buf[IPC_MAX_FRAME];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 0) {
        ipc_disconnect(c);
        ipc_schedule_reconnect(c);
        return 0;
    }
    if (n < 8) {
        wlr_log(WLR_ERROR, "compositor ipc: short frame (%zd bytes)", n);
        return 0;
    }

    uint32_t magic  = le32_load(buf);
    uint32_t length = le32_load(buf + 4);

    if (magic != IPC_MAGIC || length > IPC_MAX_BODY ||
        (size_t)n < 8 + (size_t)length) {
        wlr_log(WLR_ERROR, "compositor ipc: invalid frame");
        return 0;
    }

    ipc_dispatch(c, (const char *)buf + 8, length);
    return 0;
}

/* ── Public API ─────────────────────────────────────────────────── */

int
playos_compositor_ipc_start(struct playos_compositor *c)
{
    c->compositor_sock_fd      = -1;
    c->ipc_source              = NULL;
    c->ipc_reconnect_timer     = NULL;
    c->ipc_reconnect_delay_ms  = 100;

    int fd = ipc_connect();
    if (fd >= 0) {
        c->compositor_sock_fd = fd;
        c->ipc_source = wl_event_loop_add_fd(c->event_loop, fd,
                                              WL_EVENT_READABLE,
                                              ipc_fd_handler, c);
        wlr_log(WLR_INFO, "compositor ipc: connected to %s", IPC_SOCK_PATH);
        return 0;
    }

    ipc_schedule_reconnect(c);
    return 0;
}

void
playos_compositor_ipc_stop(struct playos_compositor *c)
{
    ipc_disconnect(c);
    if (c->ipc_reconnect_timer) {
        wl_event_source_remove(c->ipc_reconnect_timer);
        c->ipc_reconnect_timer = NULL;
    }
}

int
playos_compositor_ipc_send(struct playos_compositor *c, const char *type,
                           const char *extra_json)
{
    if (c->compositor_sock_fd < 0)
        return -1;
    return frame_send(c->compositor_sock_fd, type, extra_json);
}
