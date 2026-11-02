"""Check native backend provenance and rejection of altered build receipts/source."""
import argparse
import copy
import json
from pathlib import Path
import runpy
import shutil
import tempfile


def run(args):
    tools = Path(__file__).resolve().parents[1] / "tools"
    packaging = runpy.run_path(str(tools / "package_oneseg_backend.py"))
    builder = runpy.run_path(str(tools / "build_oneseg_backend.py"))
    verify = packaging["verify_provenance"]
    manifest = json.loads((args.backend_work / "backend-manifest.json").read_text())
    verify(args.backend_work, manifest, builder)
    for key in ("adapted_source_sha256", "ofdm_tracking", "fec_statistics", "cm0_optimization"):
        changed = copy.deepcopy(manifest)
        changed[key] = "invalid"
        try:
            verify(args.backend_work, changed, builder)
        except RuntimeError as error:
            assert "provenance mismatch" in str(error), error
        else:
            raise AssertionError("Accepted altered receipt: " + key)
    with tempfile.TemporaryDirectory(prefix="trailmate-source-tamper-") as temporary:
        root = Path(temporary)
        shutil.copy2(args.backend_work / "source.tar.gz", root / "source.tar.gz")
        relative = Path("gr-isdbt-" + builder["REVISION"]) / "lib"
        shutil.copytree(args.backend_work / relative, root / relative)
        source = root / relative / "ofdm_synchronization_1seg_impl.cc"
        source.write_bytes(source.read_bytes() + b"\n// unexpected source modification\n")
        try:
            verify(root, manifest, builder)
        except RuntimeError as error:
            assert "build source differs" in str(error), error
        else:
            raise AssertionError("Accepted altered build source")
    print("Native backend provenance: exact receipts and source verified; tampering rejected")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend-work", type=Path, required=True)
    run(parser.parse_args())
