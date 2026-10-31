# CM0 television page review

Captured on 2026-10-29 from the actual Linux LVGL application at 1232 x 568,
using the CM0 LR2021 device profile and the optional native-media build.
SDL video/audio dummy drivers and disabled physical radio were explicit capture
conditions. The screenshots contain no received television and no replacement
programme imagery.

- `oneseg-tv-idle.png`: production page opened through the application catalog,
  stopped, with zero IQ/video/PCM progress and no fresh picture.
- `oneseg-tv-help.png`: production Help opened through the keyboard input route.
  The separate touch Help route also passed its application test.

The page calls the real asynchronous TV session. These screenshots validate
idle/Help rendering only. A separate production-page harness now verifies
recorded-IQ decoding into LVGL, 320 x 240 geometry, volume/channel controls,
same-programme selection and stop/exit cleanup. Its screenshots remain in the
local test artifact directory and visibly state `TEST FIXTURE - NO RF`.
Neither test validates UI controls during physical RF, multi-programme switching,
AV synchronization, speaker output, ARM deployment or onboard LR2021 reception.
