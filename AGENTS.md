# AGENTS.md — playos-compositor

> **Implementation status:** 🟢 Sprint 4 Complete — Native DRM/KMS backend, GPU discovery via `drmGetDevices2()`, EGL/GLES2 hardware-accelerated rendering, diagnostics logging. 14 source files across `src/`, protocol XML, headless/nested test suites, and a hardware test client.

This repository implements the **PlayOS Wayland compositor** — a wlroots-based process that permanently owns the DRM/KMS device, manages all surfaces (shell, game, overlay), enforces the first-frame rule, and intercepts the system button at the libinput level.

## Specification Reference

Before touching any file here, read:
- [`playos-spec/src/playos-compositor-spec.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/playos-compositor-spec.md) — full state machine, surface policy, trust model
- [`playos-spec/src/architecture.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/architecture.md) — system context and compositor's role
- [`playos-spec/src/wayland-protocol.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/wayland-protocol.md) — private protocol the compositor implements (server side)
- [`playos-spec/src/security-model.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/security-model.md) — what clients are trusted, what are not
- [`playos-spec/src/adr/ADR-0008-gpu-discovery.md`](https://github.com/PlayOS-Foundation/playos-spec/blob/main/src/adr/ADR-0008-gpu-discovery.md) — GPU discovery via `drmGetDevices2()`

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
├── main.c                  ← Entry point, CLI args, backend selection (headless/nested/drm)
├── compositor.c            ← Central state machine, surface management, event loop
├── compositor.h            ← Internal types, state enum, public compositor API
├── gpu_discovery.c/.h      ← GPU discovery via drmGetDevices2(), scoring system (ADR-0008)
├── drm_backend.c/.h        ← Native DRM/KMS backend (Sprint 4)
├── output_modes.c/.h       ← Connector enumeration, mode selection, refresh rate
├── renderer_gbm_egl.c/.h   ← GBM/EGL hardware-accelerated renderer
├── diagnostics.c/.h        ← 7-phase startup logging to /run/playos/log/compositor.log
├── readiness.c             ← Readiness signaling helper (notify init when compositor is up)
└── trusted_client.c        ← Trusted client connection management

include/
└── compositor.h            ← Public compositor types and function declarations

protocols/
└── playos-v1.xml           ← Private Wayland protocol XML (4 interfaces)

tools/test-client/src/
└── main.c                  ← EGL/GLES2 hardware-accelerated test client with animated bars

tests/
├── headless/test_headless.c  ← Headless backend test
└── nested/test_nested.c      ← Nested Wayland compositor test

CMakeLists.txt
```

## Key Invariants — Do Not Break

- **`PLAYOS_BUTTON_SYSTEM` is reserved** — it must be intercepted before reaching Wayland clients. The libinput/evdev input path is handled by `playos-platform-api`; the compositor receives surface-level events only.
- **First-frame rule**: do not make `GAME_FOREGROUND` visible until the game has committed at least one `wl_buffer`. Check `game_surface.first_frame_committed` before any scanout swap.
- **`/dev/dri/card*` selection**: use `gpu_discovery.c` (PCI enumeration via `drmGetDevices2()` per ADR-0008), never hardcode `card0`.
- **Games never bind `playos_session_manager`** — enforce at protocol bind time by checking client credentials against the `playos-trusted` group.
- **The compositor must not exit** — if it does, the display goes dark. Treat all errors as recoverable where possible; only call `exit(1)` for unrecoverable DRM/KMS failures.
- **Readiness signal**: call `playos_readiness_signal()` after successful backend init so `playos-init` knows the compositor is ready to accept clients.

## Code Conventions

- C99. Link against wlroots-0.20, libwayland-server, libdrm, libEGL, libGLESv2, libgbm.
- All compositor state lives in a single `struct playos_compositor` — no global mutable variables.
- Backend selection: `headless` (CI/QEMU), `nested` (dev on desktop Wayland), `drm` (real hardware). Use `PLAYOS_BACKEND` env var or CLI arg.
- Log via `diagnostics.c` to `/run/playos/log/compositor.log` — 7-phase startup logging with timestamps.
- `wlr_*` functions are allowed freely; do not wrap them unnecessarily.
- wlroots 0.20 specifics: `wlr_backend_autocreate` takes `wl_event_loop*`, pkg-config name is `wlroots-0.20`, `wlr_output_layout_create` takes `wl_display*`.

## Build Commands

```sh
cmake -B build
cmake --build build

# Run in headless mode (CI / QEMU — no GPU needed):
PLAYOS_BACKEND=headless ./build/playos-compositor

# Run in nested mode (dev on desktop Wayland):
PLAYOS_BACKEND=nested ./build/playos-compositor

# Run on real hardware (DRM/KMS):
PLAYOS_BACKEND=drm ./build/playos-compositor

# Run hardware test client:
./build/tools/test-client/playos-test-client
```

## What NOT to Do

- Do not render any UI pixels in this process — UI is the shell's job (Raylib).
- Do not store game save data or handle storage — that is `playos-init`'s domain.
- Do not communicate with the game directly over IPC — use the Wayland protocol only.
- Do not add a dependency on Raylib or any UI toolkit.
- Do not fork or exec game processes — that is `playos-init`'s job via the IPC `launch_game` message.
