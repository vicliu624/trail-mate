"""Run the production version helpers without importing the SCons build script."""

import ast
import os
from pathlib import Path
import re
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
HELPERS = {
    "read_text_best_effort", "extract_version_from_changelog",
    "extract_version_from_ci_tag", "resolve_project_version",
}
source = ast.parse((ROOT / "scripts/platformio-pre.py").read_text(encoding="utf-8"))
namespace = {"os": os, "re": re, "project_dir": str(ROOT)}
functions = [node for node in source.body
             if isinstance(node, ast.FunctionDef) and node.name in HELPERS]
assert len(functions) == len(HELPERS)
exec(compile(ast.Module(body=functions, type_ignores=[]), "version-helpers", "exec"), namespace)


class FirmwareVersionTest(unittest.TestCase):
    def test_local_build_uses_changelog(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertEqual(namespace["resolve_project_version"](),
                             namespace["extract_version_from_changelog"]())

    def test_branch_and_pull_request_names_are_not_versions(self):
        for ref, name in (("refs/heads/perf/wio-l2-sdmmc-ram", "perf/wio-l2-sdmmc-ram"),
                          ("refs/pull/123/merge", "123/merge")):
            with patch.dict(os.environ, {"GITHUB_REF": ref, "GITHUB_REF_NAME": name,
                                         "GITHUB_REF_TYPE": "branch"}, clear=True):
                self.assertIsNone(namespace["extract_version_from_ci_tag"]())
                self.assertEqual(namespace["resolve_project_version"](),
                                 namespace["extract_version_from_changelog"]())

    def test_tag_overrides_changelog(self):
        with patch.dict(os.environ, {"GITHUB_REF": "refs/tags/v9.8.7-alpha",
                                     "GITHUB_REF_NAME": "v9.8.7-alpha"}, clear=True):
            self.assertEqual(namespace["resolve_project_version"](), "9.8.7-alpha")

    def test_explicit_tag_type_supports_ref_name(self):
        with patch.dict(os.environ, {"GITHUB_REF_TYPE": "tag",
                                     "GITHUB_REF_NAME": "v9.8.7-alpha"}, clear=True):
            self.assertEqual(namespace["resolve_project_version"](), "9.8.7-alpha")


if __name__ == "__main__":
    unittest.main()
