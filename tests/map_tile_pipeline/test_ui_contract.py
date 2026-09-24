"""Structural guards for the real UI integration; complements runtime C++ tests."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    start = source.index("{", source.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start + 1:end - 1]


class UiContract(unittest.TestCase):
    def test_diagnostics_use_platform_contract(self):
        source = (ROOT / "modules/ui_shared/include/ui/widgets/map/map_diagnostics.h").read_text(encoding="utf-8")
        self.assertIn('"platform/ui/map_diagnostics.h"', source)
        self.assertNotRegex(source, r"Arduino\.h|\bARDUINO\b|\bESP_PLATFORM\b|Serial\.|std::printf")

    def setUp(self):
        self.tiles = (ROOT / "platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp").read_text(encoding="utf-8")

    def test_gesture_still_services_maintenance(self):
        source = (ROOT / "modules/ui_shared/src/ui/widgets/map/map_viewport.cpp").read_text(encoding="utf-8")
        body = function_body(source, "void loader_timer_cb(")
        self.assertLess(body.index("tile_loader_maintenance"), body.index("tile_loader_step"))
        # Diagnostic reads of gesture state do not pause work; check the actual gate.
        self.assertLess(body.index("tile_loader_maintenance"), body.index("if (impl->gesture_pressed"))

    def test_maintenance_never_starts_io_decode_or_lvgl_updates(self):
        body = function_body(self.tiles, "void tile_loader_maintenance(")
        self.assertIn("popEventIf", body)
        self.assertIn("current_tile_request", body)
        self.assertIn("kMapTileUiDrainBudgetMs", body)
        self.assertIn("release_tile_payload", body)
        self.assertNotRegex(body, r"\b(?:lv_\w+|sd_\w+|decode_\w+|render_\w+)\s*\(")
        self.assertNotIn(".request(", body)
        self.assertNotIn("reportMetrics", body)

    def test_map_pipeline_has_no_transport_or_board_bypass(self):
        paths = list((ROOT / "modules/ui_map_runtime/include/ui_map_runtime/map_tiles").glob("*.h"))
        paths += list((ROOT / "platform/esp/arduino_common/include/platform/esp/arduino_common/map_tiles").glob("*.h"))
        paths += [ROOT / "platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp"]
        for path in paths:
            source = path.read_text(encoding="utf-8")
            # Comments may explain the boundary; executable configuration may not.
            source = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
            self.assertNotRegex(source, r"\b(?:TRAIL_MATE_SDFAT_SDMMC|TRAIL_MATE_SDFAT_SHARED_SPI|WIO_TRACKER_L2)\b", str(path))
            self.assertNotRegex(source, r"\bsdmmc_\w+\s*\(", str(path))

    def test_shutdown_rejects_new_work_before_started_check(self):
        body = function_body(self.tiles, "bool ensureStarted(")
        self.assertLess(body.index("if (stopping)"), body.index("if (started)"))


if __name__ == "__main__":
    unittest.main()
