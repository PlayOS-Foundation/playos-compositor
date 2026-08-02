# Contributing to PlayOS

## Architecture First

Before writing code, check [`playos-spec`](https://github.com/your-org/playos-spec) for the relevant specification document. Every component has a spec. If your change affects the public `libplayos` API, read `platform-api.md` first.

## Branching

- `main` — always releasable; requires PR + review
- `sprint/N` — sprint work branch; merged to main when the sprint exit gate passes
- `fix/short-description` — bug fixes

## Pull Requests

- One logical change per PR
- Link the relevant sprint or ADR
- All CI layers 1–5 must pass before merge
- Cross-repo changes must be coordinated (link related PRs)

## Commit Messages

```
component: short description in imperative mood

Longer explanation if needed. Reference sprint numbers and ADRs:
  Implements: Sprint-1 exit criterion 3
  See: playos-spec/runtime-ipc.md
```

## Code Style

- C: C99, 4-space indent, snake_case, prefix all public symbols with `playos_`
- No compiler warnings (`-Wall -Wextra -Werror` in CI)
- All public API functions must have Doxygen comments

## ADRs

Architecture changes that affect more than one repo require an ADR in `playos-spec/adr/`. File an issue in `playos-spec` first.