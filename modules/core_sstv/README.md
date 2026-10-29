# Shared SSTV receive core

This module owns the portable SSTV DSP previously located in
`platform/esp/arduino_common`. The existing decoder, half-band filter and CORDIC
retain their public names and include paths, with independent per-decoder
demodulator smoothing state and immutable CORDIC tables. PlatformIO and ESP-IDF
now select this owner;
Linux does not include ESP platform headers or compile its board service.

The DSP originates from [PicoSSTV](https://github.com/dawsonjon/PicoSSTV) by
Jonathan P Dawson; its MIT notices and license are preserved in `LICENSE-DSP`.
The new Trail Mate receive/projection wrapper follows the repository license.

`sstv::core::Receiver` consumes signed mono PCM at 15 kHz. It acquires a mode
from repeated scan-sync timing, then reconstructs RGB/YCbCr/grayscale lines into
the existing 288 x 192 RGB565 presentation, including the side padding. It does
not implement VIS acquisition. Acquiring sync consumes initial scan lines, so
even a recording containing a header does not establish first-row alignment.
Native adapters must not describe this acquisition as
VIS-based, nor treat host fixtures as on-air validation of every mode.

There are no audio-device, filesystem, timer, UI or worker-thread dependencies
in this module. The image-bearing object belongs in long-lived/heap storage,
not on an ESP task stack. One owner feeds PCM; adapters publish coherent frame
copies and decide persistence and lifecycle policy.
