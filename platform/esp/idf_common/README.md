# `platform/esp/idf_common`

Shared ESP-IDF platform glue.

Current responsibilities:

- common IDF startup logging helpers
- common IDF runtime/bootstrap helpers as the IDF shells mature

Longer-term role:

- provide shared ESP-IDF glue for multiple IDF targets, not only Tab5
- sit below `apps/esp_idf` and above `platform/esp/boards/<board>` in the dependency graph
