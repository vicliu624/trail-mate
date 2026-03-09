# Shared Modules

This directory will contain platform-independent business and reusable logic.

Examples of intended module types:

- chat domain and use cases
- GPS logic and filtering
- team/domain/protocol logic
- hostlink protocol logic
- shared system abstractions
- non-visual UI state/presenter logic

Code placed here should not directly depend on Arduino, ESP-IDF, FreeRTOS, LVGL, board headers, or hardware drivers.
