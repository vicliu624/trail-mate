# Legacy Governance

`legacy/` is not a feature area.

It exists to contain historical implementation roots and compatibility
surfaces while final app shells, build wrappers, and reusable modules converge.
New product work must not treat this directory as a place to add features.

## Scope

`legacy/app_implementations` contains historical implementation roots only.
These roots may keep source files, build files, scripts, and compatibility
adapters that are needed to preserve existing behavior while newer app shells
delegate to them.

`legacy/` does not automatically absorb every `Legacy*` class. Code-level
legacy adapters remain governed by
`docs/audits/LEGACY_BURNDOWN_REGISTER.md`, including their owner, callers,
exit condition, target phase, and checker status.

## Rules

- New final product app shells belong under `apps/`, not under `legacy/`.
- New build entrypoints belong under `builds/`, not under `legacy/`.
- New reusable runtime, presentation, or adapter modules belong under
  `modules/`, not under `legacy/`.
- New code must not be added under `legacy/` unless it is compatibility
  containment for an existing historical root.
- stable adapters must be renamed and moved back to official modules.
- compatibility containment must name the final app shell and the
  authoritative build wrapper that now own product startup semantics.

## Exit Direction

Each legacy implementation root must either:

- shrink until the final app shell and official modules own the behavior, or
- be split into stable modules and app-shell-specific runtime code.

Once a compatibility adapter stops being transitional, it must leave `legacy/`
or be renamed so it no longer carries a misleading `Legacy*` identity.
