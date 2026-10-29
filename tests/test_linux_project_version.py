import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LinuxProjectVersionTest(unittest.TestCase):
    def version(self, changelog, extra=None, debian=False):
        env = {key: value for key, value in os.environ.items() if not key.startswith("GITHUB_REF")}
        env.update(extra or {})
        command = [sys.executable, str(ROOT / "scripts/project_version.py"), "--changelog", str(changelog)]
        return subprocess.check_output(command + (["--debian"] if debian else []), env=env, text=True).strip()

    def test_encoding_and_tag_precedence(self):
        with tempfile.TemporaryDirectory() as directory:
            changelog = Path(directory) / "CHANGELOG.md"
            for encoding in ("utf-8", "utf-8-sig", "utf-16"):
                changelog.write_text("# Changelog\n\n## [1.2.3-rc.2+build.4]\n", encoding=encoding)
                self.assertEqual(self.version(changelog), "1.2.3-rc.2+build.4")
                self.assertEqual(self.version(changelog, debian=True), "1.2.3~rc.2+build.4")
                self.assertEqual(self.version(changelog, {"GITHUB_REF": "refs/heads/v9.0.0", "GITHUB_REF_NAME": "v9.0.0"}), "1.2.3-rc.2+build.4")
                self.assertEqual(self.version(changelog, {"GITHUB_REF": "refs/tags/v9.8.7"}), "9.8.7")
                self.assertEqual(self.version(changelog, {"GITHUB_REF_TYPE": "tag", "GITHUB_REF_NAME": "v9.8.7"}), "9.8.7")


if __name__ == "__main__":
    unittest.main()
