#!/usr/bin/env python3
"""Protect the shared-SPI display transaction invariants structurally.

This cannot replace a device contention trace, but it makes the important
mechanism regressions fail in CI: releasing an LVGL buffer after Busy, turning
a T-Deck Pro EPD acquisition miss into unconditional success, bypassing the
startup mount gate, or accidentally adding payload-sized diagnostic storage.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent


def source(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


def between(text: str, start: str, end: str) -> str:
    start_at = text.find(start)
    if start_at < 0:
        return ""
    end_at = text.find(end, start_at + len(start))
    if end_at < 0:
        return ""
    return text[start_at:end_at]


def require(violations: list[str], condition: bool, message: str) -> None:
    if not condition:
        violations.append(message)


def main() -> int:
    violations: list[str] = []
    display_header = source("platform/esp/boards/include/display/DisplayInterface.h")
    display_impl = source("platform/esp/boards/src/display/DisplayInterface.cpp")
    lvgl_bridge = source("platform/esp/arduino_common/src/LV_Helper_v9.cpp")
    coordinator_header = source(
        "platform/esp/common/include/platform/esp/common/shared_spi_coordinator.h"
    )
    board_runtime = source("platform/esp/boards/src/board_runtime.cpp")
    sd_runtime_header = source(
        "platform/esp/arduino_common/include/platform/esp/arduino_common/storage/sd_card_runtime.h"
    )
    sd_runtime = source("platform/esp/arduino_common/src/storage/sd_card_runtime.cpp")
    sd_utils = source("platform/shared/include/board/sd_utils.h")
    pager = source("boards/tlora_pager/src/tlora_pager_board.cpp")
    tdeck = source("boards/tdeck/src/tdeck_board.cpp")
    tdeck_pro = source("boards/tdeck_pro/src/tdeck_pro_board.cpp")
    diagnostics_header = source(
        "modules/core_sys/include/platform/ui/spi_diagnostics_runtime.h"
    )
    diagnostics_runtime = source(
        "platform/esp/arduino_common/src/platform_ui_spi_diagnostics_runtime.cpp"
    )
    settings = source("modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp")
    sd_preflight = between(
        sd_runtime, "bool sd_preflight_go_idle", "void record_sdfat_info"
    )

    require(
        violations,
        "enum class DisplayTransferResult" in display_header
        and "virtual DisplayTransferResult transferPixels" in display_header,
        "display abstraction must require an explicit DisplayTransferResult",
    )
    require(
        violations,
        "DisplayTransferResult LilyGoDispArduinoSPI::transferPixels" in display_impl,
        "Arduino SPI display adapter must return the typed result",
    )
    display_init = between(
        display_impl,
        "bool LilyGoDispArduinoSPI::init(",
        "void LilyGoDispArduinoSPI::end()",
    )
    require(
        violations,
        "ScopedBusAccessToken begin_bus" in display_init
        and "log_display_lock_timeout(\"spi_begin\"" in display_init
        and display_init.find("ScopedBusAccessToken begin_bus")
        < display_init.find("_spi->begin(sck, miso, mosi);"),
        "display SPI.begin must be fenced by the coordinator",
    )
    require(
        violations,
        "std::vector<uint16_t> draw_buf" not in display_impl
        and "writePattern(kBlackRgb565Pattern" in display_impl,
        "display initialization must clear through SPI's repeated pattern, not a full-frame RGB buffer",
    )
    require(
        violations,
        "struct PendingDisplayFlush" in lvgl_bridge
        and "static_assert(sizeof(PendingDisplayFlush) <= 64U" in lvgl_bridge
        and "void disp_flush_wait" in lvgl_bridge
        and "lv_display_set_flush_wait_cb(disp_drv, disp_flush_wait);" in lvgl_bridge,
        "LVGL bridge must retain pending flush state and register flush_wait_cb",
    )

    busy_branch = between(
        lvgl_bridge,
        "if (result == DisplayTransferResult::Busy || result == DisplayTransferResult::Unavailable)",
        "if (result == DisplayTransferResult::Failed)",
    )
    require(
        violations,
        bool(busy_branch)
        and "s_pending_display_flush.active = true;" in busy_branch
        and not re.search(r"^\s*lv_display_flush_ready\s*\(", busy_branch, re.MULTILINE)
        and "invalidate_after_display_failure" not in busy_branch,
        "Busy/Unavailable must retain the LVGL buffer without completing or invalidating the flush",
    )

    wait_callback = between(lvgl_bridge, "void disp_flush_wait", "static void disp_flush")
    require(
        violations,
        bool(wait_callback)
        and "DisplayTransferResult::Completed" in wait_callback
        and not re.search(r"^\s*lv_display_flush_ready\s*\(", wait_callback, re.MULTILINE),
        "LVGL wait callback must return only after its transfer outcome, without direct flush_ready",
    )
    require(
        violations,
        "displayFrameDeferrals" in coordinator_header
        and "maximumDisplayFrameWaitMs" in coordinator_header
        and "struct RuntimeSnapshot" in coordinator_header
        and "static_assert(sizeof(RuntimeSnapshot) <= 64U" in coordinator_header,
        "coordinator must expose deferred-frame and maximum-frame-wait metrics",
    )
    tdeck_pro_render_epd = between(
        tdeck_pro,
        "DisplayTransferResult TDeckProBoard::renderEpd(",
        "DisplayTransferResult TDeckProBoard::servicePendingEpd(",
    )
    tdeck_pro_epd_service = between(
        tdeck_pro,
        "DisplayTransferResult TDeckProBoard::servicePendingEpd(",
        "void TDeckProBoard::serviceDisplay(",
    )
    require(
        violations,
        bool(tdeck_pro_render_epd)
        and "return DisplayTransferResult::Busy;" in tdeck_pro_render_epd
        and "const DisplayTransferResult result = renderEpd(full_refresh);" in tdeck_pro_epd_service
        and "if (result != DisplayTransferResult::Completed)" in tdeck_pro_epd_service
        and "return result;" in tdeck_pro_epd_service,
        "T-Deck Pro must propagate a missed EPD transaction as Busy",
    )
    require(
        violations,
        "shared_spi_coordinator().release(g_shared_spi_token);" in tdeck_pro,
        "T-Deck Pro compatibility unlock must report invalid/cross-task releases to the coordinator",
    )
    tdeck_pro_display_init = between(
        tdeck_pro, "bool TDeckProBoard::initDisplay()", "bool TDeckProBoard::initTouch()"
    )
    tdeck_pro_radio_init = between(
        tdeck_pro, "bool TDeckProBoard::initRadio()", "bool TDeckProBoard::initStorage()"
    )
    require(
        violations,
        tdeck_pro_display_init.find("sharedSpiLock(")
        < tdeck_pro_display_init.find("SPI.begin(")
        and tdeck_pro_radio_init.find("sharedSpiLock(")
        < tdeck_pro_radio_init.find("SPI.begin("),
        "T-Deck Pro SPI.begin calls must execute only after its coordinator token is held",
    )
    tdeck_bootstrap = between(tdeck, "uint32_t TDeckBoard::begin(", "bool TDeckBoard::initPMU()")
    require(
        violations,
        "SPI.begin(SCK, MISO, MOSI);" not in tdeck_bootstrap
        and "withSharedSpiRadioAccess(\n        \"radio_init\"" in tdeck_bootstrap
        and "radio init deferred: shared SPI unavailable" in tdeck_bootstrap,
        "T-Deck bootstrap must route RadioLib initialization through the shared coordinator",
    )
    require(
        violations,
        "display_ready_ = LilyGoDispArduinoSPI::init(" in tdeck
        and "[TDeckBoard] display init failed" in tdeck
        and "display_ready_ = LilyGoDispArduinoSPI::init(" in source(
            "boards/twatchs3/src/twatchs3_board.cpp"
        ),
        "display adapters must propagate a failed coordinator-fenced initialization",
    )

    pager_install = between(pager, "bool TLoRaPagerBoard::installSD()", "bool TLoRaPagerBoard::ensureSDReady()")
    require(
        violations,
        "shared_spi_chip_select_pins" in pager
        and "DISP_CS" in between(pager, "void TLoRaPagerBoard::initShareSPIPins()", "uint32_t TLoRaPagerBoard::beginDisplayHardware")
        and "power_cycle_peer_cs_pins" in pager_install
        and "recovery_peer_cs_pins" in pager_install
        and "DISP_CS" in between(pager_install, "recovery_peer_cs_pins", "// Check SD card detection pin"),
        "Pager must initialize display CS and keep power-cycle peers separate from recovery peers",
    )
    pager_shutdown = between(
        pager, "void TLoRaPagerBoard::shutdownImpl(", "void TLoRaPagerBoard::softwareShutdown()"
    )
    require(
        violations,
        "spi_end_request.policy = sys::runtime::BusAccessPolicy::RecoveryExclusive;" in pager_shutdown
        and "ScopedBusAccessToken spi_end_token" in pager_shutdown
        and pager_shutdown.find("ScopedBusAccessToken spi_end_token")
        < pager_shutdown.find("SPI.end();")
        and pager_shutdown.find("SPI.end();")
        < pager_shutdown.find("spi_end_token.release();"),
        "Pager shared SPI teardown must be coordinator-fenced or skipped before deep sleep",
    )

    require(
        violations,
        "storageStartupGateSatisfied()" in board_runtime
        and "storage mount deferred: first display transaction incomplete" in board_runtime
        and ".displayFrameCompletions() > 0U" in board_runtime,
        "common board runtime must reject shared-SPI storage before a completed display transaction",
    )
    require(
        violations,
        all(
            "storageStartupGateSatisfied()" in board
            for board in (pager_install, between(tdeck, "bool TDeckBoard::installSD()", "bool TDeckBoard::ensureSDReady()"),
                          between(tdeck_pro, "bool TDeckProBoard::installSD()", "bool TDeckProBoard::ensureSDReady()"))
        ),
        "every shared-SPI board installSD entry must also reject a pre-display mount",
    )

    require(
        violations,
        "struct SdSpiBusConfig" in sd_runtime_header
        and "static_assert(sizeof(SdCardInfo) <= 64U" in sd_runtime_header
        and "const SdSpiBusConfig& spi_bus" in sd_runtime_header
        and "s_active_spi_bus.spi->begin" in sd_runtime
        and "s_sdfat_mounted = sdfat_ok;" in sd_runtime
        and "bool clear_sdfat(" in sd_runtime
        and "SdFat cleanup deferred: shared SPI unavailable" in sd_runtime
        and "sd_spi_bus_acquire(bus_token)" in between(
            sd_runtime, "bool clear_sdfat(", "void reset_info()"
        )
        and "if (!clear_sdfat())" in between(
            sd_runtime, "void unmount_sd_card()", "bool sd_card_ready()"
        )
        and "sd_spi_bus_acquire(bus_token)" in sd_preflight
        and sd_preflight.find("sd_spi_bus_acquire(bus_token)")
        < sd_preflight.find("spi.beginTransaction(")
        and sd_preflight.find("spi.endTransaction();")
        < sd_preflight.find("sd_spi_bus_release(bus_token);")
        and "mount deferred: first shared-SPI display transaction incomplete" in sd_runtime,
        "SD runtime must fence direct SdFat cleanup/preflight SPI traffic, preserve explicit board SPI wiring, and enforce the mount gate",
    )
    require(
        violations,
        "SdSpiBusConfig& spi_bus" in sd_utils
        and "resetSharedSpiForSd(spi_bus" in sd_utils
        and "record_sd_card_mount_success" in sd_utils
        and "template <typename Lockable>" not in sd_utils
        and "use_lock" not in sd_utils
        and "shared SPI recovery token unavailable" in sd_utils
        and sd_utils.find("shared_spi_coordinator().release(bus_token);")
        < sd_utils.find("storage::sd_card_info()")
        and "kSharedSpiBus" in tdeck_pro
        and "sdutil::installSpiSd(" in tdeck_pro,
        "all shared-SPI mounts must use the common configured transport/fallback path with a mandatory recovery token and release it before reading/publishing metadata",
    )

    require(
        violations,
        "struct Snapshot" in diagnostics_header
        and "static_assert(sizeof(Snapshot) <= 64U" in diagnostics_header
        and "no SPI handles, no coordinator tokens, and no display/storage payloads" in diagnostics_header
        and "std::vector" not in diagnostics_runtime
        and "new " not in diagnostics_runtime
        and "malloc" not in diagnostics_runtime
        and "s_spi_diagnostics_text[384]" in settings
        and "static spi_diagnostics_runtime::Snapshot snapshot" in settings,
        "Settings diagnostics must use bounded scalar/static storage only",
    )

    if violations:
        print("Shared SPI display contract check failed.")
        for violation in violations:
            print(f"- {violation}")
        return 1

    print("Shared SPI display contract check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
