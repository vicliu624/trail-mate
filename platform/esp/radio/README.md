# ESP Radio Runtime

Final owner for ESP radio/protocol adapter code that is platform-bound but not
an app shell responsibility.

Current migrated source:

- `meshtastic_radio_adapter.h`
- `meshtastic_radio_adapter.cpp`

Evidence:

- migrated from `removed root esp_idf/include/apps/esp_idf/meshtastic_radio_adapter.h`
- migrated from `removed root esp_idf/src/meshtastic_radio_adapter.cpp`

Rules:

- do not select UX packs here
- do not own app shell startup or loop sequencing here
- do not include `removed legacy app implementation roots`
- keep protocol behavior unchanged unless a protocol-specific change is made
