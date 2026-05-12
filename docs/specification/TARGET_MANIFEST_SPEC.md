# Target Manifest Specification

Target manifests describe product composition. They are review artifacts and
migration guides in Phase 1; they are not runtime configuration files.

## File Location

Target manifests live under:

```text
docs/targets/*.capabilities.yaml
```

The template lives at:

```text
docs/targets/TARGET_MANIFEST_TEMPLATE.yaml
```

## Required Top-Level Fields

```text
target
status
product
platform
board
runtime
ui
execution
authorities
capabilities
capability_bindings
protocols
storage
race_policy
build
```

## Semantics

`target` is the product target id, such as `esp-t-lora-pager-lvgl`.

`status` is `draft`, `reviewed`, or `active`. Phase 1 manifests are drafts.

`product` describes product family, form factor, and interaction class.

`platform` describes the selected platform kind, SDK, memory tier, filesystem,
and allocation policy.

`board` identifies the board variant and board package. It may summarize board
facts for review, but detailed hardware truth belongs in the board manifest.

`runtime` declares scheduler, owner tasks/threads, ISR policy, and lock policy.

`ui` declares shell, renderer, presentation model host, layout profile, and
input model.

`execution` declares where major cores execute.

`authorities` declares where mutable truth lives.

`capabilities` declares capability state, endpoint host, stack, role, and link
mode.

`capability_bindings` connects target capabilities to board providers, platform
drivers, runtime owners, and protocol consumers.

`protocols` declares enabled protocol cores and where protocol semantics are
implemented.

`storage` declares backend and authority-backed stores.

`race_policy` declares event, command, snapshot, ISR, storage, and UI-thread
rules.

`build` points to the current build entrypoint and intended modules for this
target. It must reflect the current repo state rather than inventing a future
layout.

## Review Requirements

Every target manifest must answer:

```text
Which platform is used?
Which board is used?
Which UI shell is used?
Where does mesh core run?
Where does phone core run?
Where does GPS core run?
Where is identity authority?
Where is peer key authority?
Where are location/time/config authorities?
Is LoRa local_radio, packet_proxy, command_proxy, or none?
Is UI LVGL, ASCII, GTK, CLI, or headless?
What is the concurrency policy?
```

## Not Runtime Configuration

Phase 1 target manifests are deliberately not loaded by firmware or Linux
processes. Later phases may generate constants or build metadata from them only
after a separate design decision.

## Change Rule

If a PR changes product composition, capability binding, authority ownership,
runtime ownership, or UI shell choice, update the relevant target manifest in
the same PR.

