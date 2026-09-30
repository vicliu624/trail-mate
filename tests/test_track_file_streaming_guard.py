"""Regression cases for file-size reads versus bounded array capacity."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "track_guard", Path(__file__).resolve().parents[1] / "scripts/check_track_file_streaming.py")
guard = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = guard
spec.loader.exec_module(guard)


class StreamingGuardTests(unittest.TestCase):
    def check_source(self, source):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_file = root / "gpx.cpp"
            source_file.write_text("// GPX reader\n" + source, encoding="utf-8")
            with patch.object(guard, "REPO_ROOT", root), patch.object(
                    guard, "walk_source_files", return_value=[source_file]):
                return guard.collect_violations()

    def test_fixed_chunk_and_digest_capacity(self):
        self.assertEqual([], self.check_source(
            "std::array<uint8_t, 512> buffer_{};\n"
            "std::array<uint8_t, 32> hash_{};\n"
            "digest.finalize(hash_.data(), hash_.size());\n"
            "auto count = std::min(buffer_.size(), remaining);\n"
            "file.read(buffer_.data(), count);\n"))

    def test_file_size_remains_forbidden(self):
        self.assertTrue(self.check_source(
            "std::array<uint8_t, 512> buffer_{};\n"
            "auto count = file.size();\n"
            "file.read(buffer_.data(), count);\n"))

    def test_dynamic_capacity_is_not_exempt(self):
        self.assertTrue(self.check_source(
            "std::vector<uint8_t> buffer_;\n"
            "auto count = buffer_.size();\n"
            "file.read(buffer_.data(), count);\n"))

    def test_available_and_slurp_remain_forbidden(self):
        self.assertTrue(self.check_source("auto count = file.available();\nfile.read(data, count);"))
        self.assertTrue(self.check_source("auto text = file.readString();"))


if __name__ == "__main__":
    unittest.main()
