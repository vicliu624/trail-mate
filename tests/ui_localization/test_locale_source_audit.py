import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("locale_validation", ROOT / "tools/validate_locale_packs.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class TranslationSourceAudit(unittest.TestCase):
    def keys(self, source):
        return [key for key, _ in AUDIT.translated_cpp_literals(source)]

    def test_comments_strings_and_raw_example_code_are_not_calls(self):
        source = '''// tr("Comment")
        /* ui::i18n::tr("Block") */
        auto example = R"sample(ui::i18n::tr("Raw example"))sample";
        auto text = "tr(\\\"Quoted example\\\")";
        ui::i18n::tr("Actual");'''
        self.assertEqual(self.keys(source), ["Actual"])

    def test_multiline_concatenation_and_nested_label_factory(self):
        source = '''ui::i18n::set_label_text(make_label(parent, 3),
                     u8"Route " /* description */ "details");'''
        self.assertEqual(self.keys(source), ["Route details"])

    def test_escaped_quotes_and_newlines_match_runtime_keys(self):
        self.assertEqual(self.keys(r'ui::i18n::tr("Save \\"route\\"\\nnow");'.replace('\\\\', '\\')),
                         ['Save "route"\nnow'])

    def test_raw_literal_argument_preserves_backslashes(self):
        self.assertEqual(self.keys(r'ui::i18n::tr(R"path(Map\folder)path");'), [r'Map\folder'])

    def test_user_content_and_dynamic_expressions_are_not_translation_keys(self):
        source = '''set_label_text_raw(label, "User data");
        set_content_label_text_raw(label, "Chat text");
        set_label_text(label, user.name());
        set_label_text(label, enabled ? "On" : "Off");
        format("Not the i18n formatter");'''
        self.assertEqual(self.keys(source), [])

    def test_format_placeholders_and_source_line_are_retained(self):
        source = '\n\nui::i18n::format("In %d days", days);'
        self.assertEqual(self.keys(source), ["In %d days"])
        offset = AUDIT.translated_cpp_literals(source)[0][1]
        self.assertEqual(source.count('\n', 0, offset) + 1, 3)

    def test_url_slashes_do_not_start_a_comment_inside_a_literal(self):
        self.assertEqual(self.keys('tr("https://example.com/help");'), ["https://example.com/help"])

    def test_declarative_screen_names_are_translated_dynamic_menu_keys(self):
        source = '''out.add({ScreenId::Calculator, "Calculator", true});
        registry.add({ui::menu::ScreenId::Geocaching, "Geocaching"});
        auto example = R"(out.add({ScreenId::Fake, "Example", true}))";'''
        self.assertEqual(self.keys(source), ["Calculator", "Geocaching"])


if __name__ == "__main__":
    unittest.main()
