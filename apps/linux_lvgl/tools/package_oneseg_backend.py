#!/usr/bin/env python3
"""Package a native pinned gr-isdbt build without installing or accessing RF."""
import argparse
import json
import os
from pathlib import Path
import platform
import runpy
import shutil
import subprocess
import sys
import sysconfig
import tempfile


def command(arguments, **kwargs):
    result = subprocess.run(arguments, text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {arguments[0]}\n{result.stderr[-4000:]}")
    return result.stdout.strip()


def cm0_abi_compatible(architecture, abi):
    return (architecture == "armhf" and abi.get("glibcxx_release") == 14 and
            abi.get("default_lock_policy") == "mutex" and abi.get("cxx11_abi") == 1)


def package(args):
    if platform.system() != "Linux":
        raise RuntimeError("Build the backend package natively on its target Linux distribution")
    work = args.backend_work.resolve()
    manifest = json.loads((work / "backend-manifest.json").read_text())
    pinned = runpy.run_path(str(Path(__file__).with_name("build_oneseg_backend.py")))
    for key, expected in (("revision", pinned["REVISION"]), ("archive_sha256", pinned["ARCHIVE_SHA"]),
                          ("adapted_source_sha256", pinned["PATCHED_SHA"]),
                          ("tmcc_source_sha256", pinned["TMCC_PATCHED_SHA"]),
                          ("tmcc_message_schema", 1),
                          ("viterbi_source_sha256", pinned["VITERBI_PATCHED_SHA"])):
        if manifest.get(key) != expected:
            raise RuntimeError("Backend provenance mismatch: " + key)
    if manifest["machine"] != platform.machine() or manifest["python"].split(".")[:2] != list(map(str, sys.version_info[:2])):
        raise RuntimeError("Backend machine/Python ABI differs from the packaging interpreter")
    architecture = command(["dpkg", "--print-architecture"])
    abi = manifest.get("cxx_abi", {})
    if (abi.get("default_lock_policy") not in ("single", "mutex", "atomic") or
            type(abi.get("glibcxx_release")) is not int or abi.get("cxx11_abi") not in (0, 1)):
        raise RuntimeError("Backend C++ ABI is missing; rerun the native backend builder")
    expected_machine = {"amd64": 62, "armhf": 40, "arm64": 183}.get(architecture)
    if expected_machine is None:
        raise RuntimeError("Unsupported native Debian architecture: " + architecture)
    prefix = work / "prefix"
    modules = list(prefix.glob("lib/python*/site-packages/gnuradio/isdbt"))
    modules += list(prefix.glob("lib/python*/dist-packages/gnuradio/isdbt"))
    if len(modules) != 1 or not list(modules[0].glob("*" + sysconfig.get_config_var("EXT_SUFFIX"))):
        raise RuntimeError("Missing or ambiguous native Python binding")
    libraries = list(prefix.rglob("libgnuradio-isdbt.so*"))
    if not libraries or len({path.parent for path in libraries}) != 1:
        raise RuntimeError("Missing or ambiguous native shared library")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    # Debian control permissions must be real Linux modes, including on WSL.
    with tempfile.TemporaryDirectory(prefix="trailmate-oneseg-package-") as temporary:
        root = Path(temporary)
        stage = root / "stage"
        private = stage / "usr/libexec/trailmate/oneseg-backend"
        shutil.copytree(modules[0], private / "python/gnuradio/isdbt",
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        (private / "lib").mkdir()
        for library in libraries:
            if library.is_symlink():
                target = os.readlink(library)
                if Path(target).name != target:
                    raise RuntimeError("Library symlink must remain in its private directory")
                (private / "lib" / library.name).symlink_to(target)
            else:
                shutil.copy2(library, private / "lib" / library.name)
        binaries = [p for p in private.rglob("*.so*") if not p.is_symlink()]
        for binary in binaries:
            header = binary.read_bytes()[:20]
            if header[:4] != b"\x7fELF" or header[5] != 1 or int.from_bytes(header[18:20], "little") != expected_machine:
                raise RuntimeError("ELF architecture differs from native Debian package: " + str(binary))
        docs = stage / "usr/share/doc/trailmate-oneseg-backend"
        docs.mkdir(parents=True)
        shutil.copy2(work / ("gr-isdbt-" + pinned["REVISION"]) / "COPYING", docs / "copyright")
        (docs / "backend-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        # Check the actual decoder from a relocated path with private-path overrides removed.
        decoder = private.parent / "trailmate-linux-oneseg-decode"
        shutil.copy2(Path(__file__).with_name("oneseg_decode.py"), decoder)
        environment = os.environ.copy()
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        for key in ("PYTHONPATH", "LD_LIBRARY_PATH", "TRAIL_MATE_ONESEG_BACKEND_ACTIVE"):
            environment.pop(key, None)
        check = subprocess.run([sys.executable, str(decoder), "--profile", "auto",
                                "--check-backend"], env=environment, capture_output=True, timeout=30)
        if check.returncode or check.stdout or b'"event": "backend_ready"' not in check.stderr:
            raise RuntimeError("Relocated backend check failed: " + check.stderr.decode(errors="replace")[-3000:])
        events = [json.loads(line) for line in check.stderr.splitlines() if line.startswith(b"{")]
        ready = next(event for event in events if event.get("event") == "backend_ready")
        if ready.get("parameter_autodetection") is not True or ready.get("parameter_observation") is not True:
            raise RuntimeError("Relocated backend lacks automatic TMCC acquisition capability")
        decoder.unlink()  # The application owns this path; the backend package owns only its sidecar.
        (root / "debian").mkdir()
        (root / "debian/control").write_text("Source: trailmate-oneseg-backend\nSection: libs\nPriority: optional\nMaintainer: Trail Mate contributors\n\nPackage: trailmate-oneseg-backend\nArchitecture: any\nDescription: Native Trail Mate 1seg decoder backend\n")
        (root / "debian/shlibs.local").write_text("libgnuradio-isdbt 1.0.0 trailmate-oneseg-backend\n")
        dependencies = command(["dpkg-shlibdeps", "-O", "-xtrailmate-oneseg-backend",
                                "-l" + str(private / "lib"), *["-e" + str(p) for p in binaries]], cwd=root)
        if not dependencies.startswith("shlibs:Depends="):
            raise RuntimeError("Missing native shared-library dependency result")
        version = "1.1.0+git" + pinned["REVISION"][:12] + "-1"
        minor = sys.version_info.minor
        depends = f"{dependencies.split('=', 1)[1]}, gnuradio, python3-numpy, python3 (>= 3.{minor}), python3 (<< 3.{minor + 1})"
        control = stage / "DEBIAN"
        control.mkdir()
        capabilities = ["trailmate-oneseg-backend-auto-v1"]
        if cm0_abi_compatible(architecture, abi):
            capabilities.append("trailmate-oneseg-backend-cm0-abi-v1")
        provides = "Provides: " + ", ".join(capabilities) + "\n"
        (control / "control").write_text(f"Package: trailmate-oneseg-backend\nVersion: {version}\nArchitecture: {architecture}\nMaintainer: Trail Mate contributors\nSection: libs\nPriority: optional\nDepends: {depends}\n{provides}X-Trail-Mate-Cxx-Lock-Policy: {abi['default_lock_policy']}\nX-Trail-Mate-Glibcxx-Release: {abi['glibcxx_release']}\nDescription: Native pinned GNU Radio 1seg backend for Trail Mate\n")
        for path in stage.rglob("*"):
            if not path.is_symlink():
                path.chmod(0o755 if path.is_dir() or path in binaries else 0o644)
        output = args.output / f"trailmate-oneseg-backend_{version}_{architecture}.deb"
        command(["dpkg-deb", "--root-owner-group", "--build", str(stage), str(output)])
        print(json.dumps(dict(package=str(output), architecture=architecture,
                              relocated_backend_check=True, physical_reception_verified=False), indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend-work", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    package(parser.parse_args())
