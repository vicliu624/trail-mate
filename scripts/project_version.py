"""Resolve the project version using the same tag/Changelog precedence as firmware builds."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--changelog", type=Path, default=Path(__file__).resolve().parents[1] / "CHANGELOG.md")
    parser.add_argument("--debian", action="store_true")
    args = parser.parse_args()
    ref = os.environ.get("GITHUB_REF", "")
    tag = ref.rsplit("/", 1)[-1] if ref.startswith("refs/tags/") else ""
    if not tag and os.environ.get("GITHUB_REF_TYPE") == "tag":
        tag = os.environ.get("GITHUB_REF_NAME", "")
    version = tag.removeprefix("v") if tag else subprocess.check_output(
        [sys.executable, str(Path(__file__).with_name("read_changelog.py")), str(args.changelog), "--version"], text=True).strip()
    number = r"(?:0|[1-9][0-9]*)"
    prerelease = r"(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
    identifier = r"[0-9A-Za-z-]+"
    pattern = rf"{number}\.{number}\.{number}(?:-{prerelease}(?:\.{prerelease})*)?(?:\+{identifier}(?:\.{identifier})*)?"
    if len(version) > 256 or not re.fullmatch(pattern, version):
        parser.error("project version must have a numeric major.minor.patch and optional prerelease/build identifiers")
    print(version.replace("-", "~", 1) if args.debian else version)


if __name__ == "__main__":
    main()
