# AGENTS.md — playos-compositor

> **Implementation status:** 🔴 Pre-implementation — state machine and architecture defined in `playos-spec`. No source code yet (`CONTRIBUTING.md` only). This AGENTS.md describes the **target** structure.

This repository implements the **PlayOS Wayland compositor** — a wlroots-based process that permanently owns the DRM/KMS device, manages all surfaces (shell, game, overlay), enforces the first-frame rule, and intercepts the system button at the libinput level.

## Specification Reference

Before touching any file here, read:
- [`playos-spec/src/playos-compositor-spec.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/playos-compositor-spec.md) — full state machine, surface policy, trust model
- [`playos-spec/src/architecture.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/architecture.md) — system context and compositor's role
- [`playos-spec/src/wayland-protocol.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/wayland-protocol.md) — private protocol the compositor implements (server side)
- [`playos-spec/src/security-model.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/security-model.md) — what clients are trusted, what are not

## Compositor State Machine — Core Contract

```
SHELL_FOREGROUND
    │  (shell sends playos_session_manager.launch_game)
    ▼
GAME_STARTING
    │  (game commits first wl_buffer — first-frame rule)
    ▼
GAME_FOREGROUND ──── (PLAYOS_BUTTON_SYSTEM intercepted at libinput)
    │                                          │
    │                                          ▼
    │                           PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND
    │                                          │
    ▼                                          │
TERMINATING_GAME ◄─────────────────────────────
    │
    ▼
SHELL_FOREGROUND
```

**Never** hand off DRM/KMS device ownership to any client. Games get GPU access via EGL/Wayland only.

## Repository Layout

```
src/
├── main.c              ← Entry point, wl_display setup, event loop
├── compositor.h/.c     ← State machine, surface management
├── input.c             ← libinput device handling, SYSTEM button intercept
├── drm.c               ← DRM/KMS backend (via wlroots)
├── shell_client.c      ← Trusted connection to playos-shell (IPC)
├── game_surface.c      ← playos_game_surface protocol implementation
├── overlay_surface.c   ← playos_overlay_surface protocol implementation
└── session_manager.c   ← playos_session_manager protocol implementation (server)

include/
└── compositor.h        ← Internal types and state enum

CMakeLists.txt
```

## Key Invariants — Do Not Break

- **`PLAYOS_BUTTON_SYSTEM` must be consumed at `libinput` level** — it must never be forwarded to any Wayland client as a key event.
- **First-frame rule**: do not make `GAME_FOREGROUND` visible until the game has committed at least one `wl_buffer`. Check `game_surface.first_frame_committed` before any scanout swap.
- **`/dev/dri/card*` selection**: use PCI enumeration (see ADR-0008), never hardcode `card0`.
- **Games never bind `playos_session_manager`** — enforce at protocol bind time by checking client credentials against the `playos-trusted` group.
- **The compositor must not exit** — if it does, the display goes dark. Treat all errors as recoverable where possible; only call `exit(1)` for unrecoverable DRM/KMS failures.

## Code Conventions

- C99. Link against wlroots, libwayland-server, libinput, libudev, libdrm.
- All compositor state lives in a single `struct playos_compositor` — no global mutable variables.
- Log via the IPC logging channel (`playos_ipc_log()`), not `fprintf(stderr)`, except during early startup before IPC is established.
- `wlr_*` functions are allowed freely; do not wrap them unnecessarily.

## Build Commands

```sh
cmake -B build
cmake --build build
# Run under a nested Wayland compositor for dev (see playos-spec/src/dev-environment.md):
PLAYOS_BACKEND=headless ./build/playos-compositor
```

## What NOT to Do

- Do not render any UI pixels in this process — UI is the shell's job (Raylib).
- Do not store game save data or handle storage — that is `playos-init`'s domain.
- Do not communicate with the game directly over IPC — use the Wayland protocol only.
- Do not add a dependency on Raylib or any UI toolkit.
- Do not fork or exec game processes — that is `playos-init`'s job via the IPC `launch_game` message.
