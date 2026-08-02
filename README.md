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

## State Machine

```
SHELL_FOREGROUND → GAME_STARTING → GAME_FOREGROUND
    ↑                                      ↓ (System button)
    └────── TERMINATING_GAME ←── PLAYOS_UI_FOREGROUND_WITH_GAME_BACKGROUND
```

See [`playos-spec/playos-compositor-spec.md`](https://github.com/your-org/playos-spec/blob/main/playos-compositor-spec.md) for the full specification.

## Building

```bash
cmake -S . -B build -DPLAYOS_BACKEND=headless  # for QEMU/CI
cmake --build build
```

Requires: `wlroots`, `wayland-server`, `libdrm`, `libinput`, `pixman`
