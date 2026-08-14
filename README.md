# PlayOS Compositor

> wlroots-based Wayland compositor. Permanent DRM/KMS owner, surface z-order, focus, input routing, and console lifecycle state machine.

**Dependency position:** `playos-compositor` depends on `playos-runtime` (private Wayland protocol, compositor control IPC). It does not depend on `playos-platform-api` or application-level code.

## What This Repository Owns

- wlroots backend, renderer, allocator, and scene
- DRM/KMS device enumeration and permanent ownership
- Wayland display socket (`playos-0`)
- Surface roles, z-order, visibility, and focus
- Trusted shell and overlay identity
- Expected game launch identity and first-frame activation rule
- Reserved system input interception (`PLAYOS_BUTTON_SYSTEM`)
- Input routing: shell ↔ game ↔ overlay
- Console lifecycle state machine
- Crash recovery (surface cleanup, return to shell)

## What It Does NOT Own

- Process spawning → `playos-init`
- Game installation, save management → `playos-refdistro` / `playos-platform-api`
- Public application API → `playos-platform-api`

## Direct Process Interactions

### `playos-init` (PID 1, supervisor)

- Spawns the compositor via `fork`/`execl("/usr/bin/playos-compositor")` and sets its environment: `XDG_RUNTIME_DIR=/run/playos`, `WAYLAND_DISPLAY=wayland-0`, `PLAYOS_BACKEND=drm`.
- Redirects the compositor's stderr to `/data/log/compositor-stderr.log` for on-device debugging.
- Readiness handshake: `playos-init` polls for `/run/playos/compositor-ready` for up to 5s after spawn. The compositor writes that file at the end of startup via `playos_readiness_signal()` (`src/readiness.c`), containing `pid`, `socket`, and `backend`.

Note: `playos-init` also defines `PLAYOS_SOCK_COMPOSITOR = /run/playos/compositor.sock`, but the compositor does **not** use that socket — the live handshake is the readiness *file*.

### `playos-shell` (trusted Wayland client)

- Connects to the compositor's Wayland socket.
- Binds the private global `playos_manager_v1` and calls `register_shell`; the compositor claims the `PLAYOS_ROLE_SHELL` role in `src/trusted_client.c`.
- Uses the standard globals the compositor serves — `wl_compositor` and `xdg_wm_base`/`xdg_shell` — for its fullscreen surface.

### `playos-overlay` (trusted role — designed, not yet wired)

- Protocol supports `register_overlay` + `playos_overlay_v1`, and the compositor tracks an `overlay_client`.
- The manager server handles `register_overlay`, but `playos_overlay_v1` is declared in the protocol XML only, not implemented server-side yet.

### Games (untrusted Wayland clients)

- Get GPU access only through EGL/Wayland via the standard `wl_compositor` + `xdg_shell` globals; they never get DRM/KMS.
- `playos_game_launch_v1` is declared in the protocol XML but not implemented server-side yet.

### System / hardware

- **GPU / DRM**: `src/gpu_discovery.c` enumerates `/dev/dri/card*` via `drmGetDevices2()`; `src/drm_backend.c`, `src/output_modes.c`, and `src/renderer_gbm_egl.c` drive GBM/EGL/GLES2 for hardware-accelerated scanout.
- **Input**: the compositor creates only a minimal empty seat; the libinput/evdev path is owned by `playos-platform-api`. Surface-level event wiring is not present in the current compositor code.
- **No IPC to `playos-init`'s `control.sock`**: the compositor has no `playos_ipc` connection; it signals readiness via the file and communicates with clients over Wayland only.

### Communication channels it owns

- Wayland socket: `/run/playos/wayland-0` (created under `XDG_RUNTIME_DIR=/run/playos`).
- Readiness file: `/run/playos/compositor-ready`.
- Diagnostics: `/run/playos/log/compositor.log` plus stderr redirected by `playos-init` to `/data/log/compositor-stderr.log`.

## State Machine

```
SHELL_FOREGROUND → GAME_STARTING → GAME_FOREGROUND
    ↑                                      ↓ (System button)
    └────── TERMINATING_GAME ←── PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND
```

See [`playos-spec/playos-compositor-spec.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/playos-compositor-spec.md) for the full specification.

## Building

```bash
cmake -S . -B build -DPLAYOS_BACKEND=headless  # for QEMU/CI
cmake --build build
```

Requires: `wlroots`, `wayland-server`, `libdrm`, `libinput`, `pixman`
