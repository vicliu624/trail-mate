# ui_legacy_adapters

Bounded compatibility adapters and anti-corruption bridges.

May contain:

- legacy source/sink adapters
- compatibility bridges
- anti-corruption wrappers around existing runtime APIs

Must not contain:

- new product behavior
- renderer widgets
- app composition roots
- build entrypoints
- board facts

This module was split from `modules/ui_shared` during the Phase 8 structural
migration batch. The old `ui/presentation_sources/legacy_*` headers remain
forwarding headers for migrated adapters.
