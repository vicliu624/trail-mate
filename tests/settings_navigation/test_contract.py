"""Guard Settings integration not linked by the isolated LVGL lifecycle test."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPONENTS = (ROOT / "modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp").read_text(encoding="utf-8")
INPUT = (ROOT / "modules/ui_shared/src/ui/screens/settings/settings_page_input.cpp").read_text(encoding="utf-8")


class SettingsNavigationContract(unittest.TestCase):
    def test_top_back_reachable_from_both_columns(self):
        for column in ("filter", "list"):
            self.assertIn(f"adapter.{column}_top_back_placement = BackPlacement::Leading;", INPUT)

    def test_all_modal_surfaces_escape_page_flex_layout(self):
        surfaces = list(re.finditer(r"(?:lv_obj_t\* bg|g_state\.modal_root) = lv_obj_create\(g_state\.root\);", COMPONENTS))
        self.assertEqual(len(surfaces), 3)
        for surface in surfaces:
            statement = COMPONENTS[surface.end():].split(";", 1)[0]
            self.assertIn("LV_OBJ_FLAG_IGNORE_LAYOUT", statement)

    def test_page_teardown_does_not_restore_focus(self):
        destroy = COMPONENTS.split("void destroy()", 1)[1].split("bool activate_filter_button", 1)[0]
        self.assertIn("modal_close(false);", destroy)
        self.assertLess(destroy.index("modal_close(false);"), destroy.index("input::cleanup();"))


if __name__ == "__main__":
    unittest.main()
