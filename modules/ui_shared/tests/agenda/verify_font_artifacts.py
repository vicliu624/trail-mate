"""Opt-in glyph and RAM-metadata checks against locally generated font binaries.

First export cases with test_locale_coverage.py --export-font-cases DIRECTORY,
then build the corresponding binaries in DIRECTORY using their build.ini recipes.
No downloads or font generation are performed by this verifier.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--tester", type=Path, required=True)
    args = parser.parse_args()
    cases = json.loads(args.cases.read_text(encoding="utf-8"))
    if not cases:
        raise RuntimeError("No font artifact cases")
    for case in cases:
        directory = Path(case["directory"])
        manifest = dict(
            line.split("=", 1)
            for line in (directory / "manifest.ini").read_text(encoding="utf-8").splitlines()
            if "=" in line and not line.lstrip().startswith(("#", ";"))
        )
        artifact = args.cases.parent / f"{directory.name}.bin"
        result = subprocess.run(
            [str(args.tester.resolve()), str(artifact.resolve()), case["charset"]],
            check=True, capture_output=True, text=True,
        )
        print(result.stdout, end="")
        measured = re.search(r"retained=(\d+) allocations=\d+ load_peak=\d+ pointer_bits=(\d+)", result.stdout)
        if not measured or measured[2] != "32":
            raise RuntimeError("ESP font budget verification requires a 32-bit GNU memory-probe build")
        # Round retained payload upward to 1 KiB, then reserve a further 1 KiB
        # for allocator/ABI variance. This is an estimate, not a heap watermark.
        minimum = ((int(measured[1]) + 1023) // 1024 + 1) * 1024
        declared = int(manifest["estimated_ram_bytes"])
        if declared < minimum:
            raise RuntimeError(f"{directory.name}: declared {declared}, estimated minimum {minimum}")
        print(f"RAM manifest OK: {declared} B (estimated minimum {minimum} B)")


if __name__ == "__main__":
    main()
