# Radio and compact UI remediation acceptance

Updated 2026-10-30. This ledger preserves the requested scope: Cardputer Zero and
CM0 interface/function parity, Protocol Probe and Walkie fixes, Help/shortcuts,
LR2021 integration on CM0/P4, independent FM, and CM0 onboard 1seg playback with
a 320 x 240 picture. It is not a release-completion claim.

## Requirements and evidence

| Requirement | Authoritative software evidence | Remaining acceptance |
| --- | --- | --- |
| Cardputer Zero uses the product Menu and exposes its applicable app routes | [Current screenshots](../targets/cardputerzero-screenshots.md), actual shell captures, [catalog builder](../../modules/ui_shared/src/ui/app_catalog_builder.cpp), app-shell and page-Help harnesses | Exercise the live device menu, keyboard and focus transitions. A source-text smoke test alone is insufficient. |
| Remove the obsolete persistent two-panel presentation | Current menu-open/menu-closed shell captures and shared navigation implementation; [Cardputer evidence](../targets/cardputerzero-screenshots.md) | Host captures establish the reviewed states, not every modal or live interaction. Review newly observed clipping separately. |
| Protocol Probe has real commands and errors | Linux [receive worker](../../platform/linux/common/src/platform/ui/lora_receive_session.cpp), [LoRa port](../../platform/linux/common/src/platform/ui/lora_runtime.cpp), actual Probe page tests; missing hardware disables actions/reports failure | Real RF receive/transmit, device latency and recovery on both target compositions. |
| Walkie Talkie has real controls, service and error states | [Shared page](../../modules/ui_shared/src/ui/screens/walkie_talkie/walkie_talkie_page_runtime.cpp), native Walkie service, current compact/touch captures; compact Help test checks level-meter bounds in the actual error state | Physical PTT, microphone/speaker, monitor, busy radio and return-to-mesh behavior. |
| Contextual Help and shortcuts work without clipping | [Cardputer page-Help test](../../apps/linux_cardputer_zero/tests/linux_cardputer_zero_page_help_test.cpp), [shortcut contract](linux-keyboard-shortcuts.md), P4 FM direct Help test | Live keyboard mapping, touch/focus behavior and unavailable-device flows remain hardware checks. |
| FM and 1seg have distinct icons matching the existing menu style | Original 64 x 64 transparent pixel icons, reproducible [generator](../../scripts/generate_radio_icons.py), RGB565A8 resources and actual Linux/P4 icon methods. Included in the rebuilt and isolated-install-verified ARM package. | Physical device presentation remains pending. |
| T-Display app overflow scrolls vertically and widgets page horizontally | [Actual LVGL pointer regression](../../apps/linux_lvgl/tests/menu_scroll_test.cpp) passed with 24 entries: vertical drag, last-row visibility, widget forward/back paging and unchanged app-grid position. Catalog/menu now share capacity 32 instead of separate 18-entry limits; rebuilt into the ARM package. | Physical deployment and touch acceptance. The test's initially missing LVGL tick callback was corrected to match the real main program. |
| Failures do not masquerade as successful radio state | Linux RX-restart fault injection reproduces old false success; fixed port quarantines stale receive context. Exact-source P4 TX/RX tests reject restart failure and damaged IRQ packets. Latest native suite: 18/18 | Physical error injection and restoration under actual mesh/Walkie/Probe traffic. |
| Borrow pinned LR2021 research for CM0 and P4 Probe | [IQ integration](../../cmake/TrailMateLr2021IqOverlay.cmake), checked FIFO/configuration transactions, shared ownership and bounded buffers; [P4 audit](t-display-p4-hardware-audit.md) | Measure hardware throughput, FIFO service gaps/flags, loss and recovery. Host transactions do not establish a performance improvement on the boards. |
| FM is an independent feature with a Menu entry and actual IQ-to-audio chain | [FM integration](broadcast-fm-integration.md), target-owned Linux app, shared P4 page, worker/queue/DSP/PCM and checked cleanup tests; actual TFT LR2021 firmware build | Native CM0 deployment and audible device output. Experimental receiver is restricted to 100–108 MHz; physical rate/passband is not qualified. |
| Test FM at 100.8 MHz on the confirmed TFT LR2021 P4 | Default frequency and actual page rendering verified; production runtime and codec symbols linked into the TFT LR2021 firmware | Actual station reception and listening test are outstanding. Current Windows serial inventory exposes only COM1; no P4 port was identified. |
| CM0 has an independent 1seg TV feature prioritizing onboard LR2021 | [TV integration](t-display-cm0-oneseg-tv.md), actual radio/decoder/media/session composition and Menu app; real GNU Radio/FFmpeg tested with explicitly labeled offline input | Onboard RF capture, required throughput and native ARM application/backend deployment. General broadcast parameter detection remains unqualified; current named waveform is a research profile. |
| The 1seg picture stays 320 x 240 without stretching across the display | [Production page](../../platform/linux/common/src/ui/oneseg_tv_app.cpp), [page harness](../../apps/linux_lvgl/tests/oneseg_tv_page_test.cpp) checks both normal and fullscreen viewport geometry | Verify physical screen presentation and changing received video. Fullscreen hides chrome and does not enlarge the viewport. |
| Received programme changes control actual decoding | Complete-PAT removal, selected-PMT mapping changes, media epochs, stale-frame rejection, synthetic programme/color/PID/PTS transition tests through real FFmpeg | Partial-PMT ageing, actual signal loss/recovery and physical A/V timing are pending. Silence alone never establishes service removal. |
| Delivery contains real decoder dependencies | Native armhf/Raspbian backend and TV-enabled application packages built; joint metadata/private import verified. Fixed native dependency omission and CRLF maintainer scripts. Actual isolated ARM installation reached installed status for both packages with empty dpkg audit; installed decoder import and four installed-app touch-Help runs passed. LF/shebang/shell-syntax regression passed. | Physical CM0 installation, services and complete device runtime remain pending. Initial concurrent-QEMU handshake failure at the 15-second bound remains recorded; idle-emulator success does not prove device timing or physical decoding. |

## Device boundaries

On 2026-10-30 the authorized physical CM0 deployment completed with installer
exit 0 and independent postflight exit 0. `trailmate-cm0` 1.0.1 and
`trailmate-oneseg-backend` 1.0.0+gitb576783021fb-1 are both installed as armhf
packages. The installed executable matches the DEB payload; dpkg audit is empty,
dynamic libraries resolve, the private decoder backend check and application
`--help` pass, and the existing desktop launcher targets the updated executable.
The only device-profile change adds `radio_rf_switch_profile=cm0_16e8`; previous
settings are retained. The old 1.0.0 package was reconstructed before upgrading
and remains available for rollback alongside the original profile backup.
The app was left stopped. This supersedes installation-pending entries above,
but does not satisfy physical display, touch, reception or audible-output gates.
The local receipt and complete logs are retained under
`.codex-build/cm0/native-armhf/`.

The latest host verification after the checked LoRa-port changes completed the
normal Cardputer screenshot-capture and page-Help target builds and passed all
13 contextual Help regressions. The production LoRa port was explicitly
recompiled into the Cardputer Linux common library. CM0's normal application and
Probe target build passed four focused receive/Probe tests plus the compact IQ
page. After the saved-FSK handoff implementation, the native FM/TV suite passed
19/19 and IQ contracts passed 8/8; TFT LR2021 firmware compilation
completed. These results do not change the physical acceptance column above.

The user subsequently authorized replacing the CM0 application and supplied
device authentication. Package deployment and non-RF installation checks are
authorized; physical FM/TV reception remains assigned to the user. Deployment
must not initialize the LR2021 or interfere with the separate OneSegCast work.
A substitute receiver does not count as onboard LR2021 acceptance.

The P4 hardware is confirmed as TFT + LR2021. A usable current connection must
be identified before device testing; historical COM port names are not current
device identity. No firmware has been flashed as part of the work in this ledger.

## Evidence limits and completion gate

Actual production-page rendering proves layout for the captured state. Explicit
fixture IQ/TS can prove decoding and callback behavior. SDL dummy output proves
PCM submission/lifecycle, not audible sound. PCM submission is not station lock.
Successful firmware compilation proves linkable composition, not RF operation.
All fixture captures must retain their identifying labels.

P4 continuous FM now saves a successfully committed FSK configuration, applies a
real LoRa staging profile for the legacy IQ engine, then cold-resets and restores
the saved FSK PHY and RX/standby state. Exact-source software tests cover this
handoff, including failures; hardware acceptance remains outstanding. Finite
Probe IQ retains its separate paused-LoRa entry contract. No FSK profile is
relabelled to bypass the engine's LoRa snapshot requirements.

Completion still requires the requested visible behavior and real functionality
on the target hardware, including the 100.8 MHz listening test, onboard 1seg
changing video/audio, return to mesh/Walkie/Probe, and review of live compact UI
navigation/Help. The tests above establish useful software evidence but do not
satisfy those physical gates. Keep the remediation goal open until the missing
evidence is available and passes.
