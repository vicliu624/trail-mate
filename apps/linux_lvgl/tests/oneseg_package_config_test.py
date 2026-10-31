"""Check native SONAME dependencies and CM0 package preflight without building/RF."""
import argparse
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile
from types import SimpleNamespace


def run(args):
    repo = Path(__file__).resolve().parents[3]
    prefix = args.ffmpeg_prefix.resolve()
    with tempfile.TemporaryDirectory(prefix="trailmate-oneseg-dependencies-") as temporary:
        root = Path(temporary)
        lines = [f'include("{repo}/cmake/TrailMateOneSegMedia.cmake")']
        expected = ["trailmate-oneseg-backend (>= 1.1.0)", "trailmate-oneseg-backend-cm0-abi-v1", "trailmate-oneseg-backend-auto-v1", "libaribb24-0t64"]
        arib = list(args.arib_prefix.resolve().glob("usr/lib/*/libaribb24.so"))
        assert len(arib) == 1, arib
        lines.append(f'set(TRAIL_MATE_ONESEG_ARIB_LIBRARY "{arib[0]}")')
        for part in ("avformat", "avcodec", "avutil", "swresample", "swscale"):
            matches = list(prefix.glob(f"usr/lib/*/lib{part}.so"))
            assert len(matches) == 1, matches
            library = matches[0].resolve()
            major = re.fullmatch(r"lib" + part + r"\.so\.(\d+)(?:\.\d+)*", library.name)
            assert major, library
            expected.append("lib" + part + major[1])
            lines.append(f'set(TRAIL_MATE_ONESEG_{part}_LIBRARY "{library}")')
        lines += ['trailmate_oneseg_debian_dependencies(result)',
                  'file(WRITE "' + str(root / 'dependencies.txt') + '" "${result}")']
        script = root / "check.cmake"
        script.write_text("\n".join(lines))
        subprocess.run(["cmake", "-P", str(script)], check=True, capture_output=True)
        assert (root / "dependencies.txt").read_text() == ", ".join(expected)
        # Reject old or altered native manifests before creating package output.
        tools = repo / "apps/linux_lvgl/tools"
        pinned = runpy.run_path(str(tools / "build_oneseg_backend.py"))
        packager = runpy.run_path(str(tools / "package_oneseg_backend.py"))["package"]
        manifest = dict(revision=pinned["REVISION"], archive_sha256=pinned["ARCHIVE_SHA"],
                        adapted_source_sha256=pinned["PATCHED_SHA"], tmcc_message_schema=1,
                        tmcc_source_sha256=pinned["TMCC_PATCHED_SHA"],
                        viterbi_source_sha256=pinned["VITERBI_PATCHED_SHA"])
        backend_work = root / "manifest-fixture"
        backend_work.mkdir()
        for key in ("tmcc_source_sha256", "tmcc_message_schema", "viterbi_source_sha256"):
            for missing in (False, True):
                changed = dict(manifest)
                if missing:
                    del changed[key]
                else:
                    changed[key] = "incorrect"
                (backend_work / "backend-manifest.json").write_text(json.dumps(changed))
                output = root / "rejected-output"
                try:
                    packager(SimpleNamespace(backend_work=backend_work, output=output))
                except RuntimeError as error:
                    assert str(error) == "Backend provenance mismatch: " + key, str(error)
                else:
                    raise AssertionError("Altered backend manifest accepted: " + key)
                assert not output.exists()
        # Evaluate the real device package recipe in an isolated metadata-only
        # project. The interface target is a fixture, not an application build.
        source = root / "metadata"
        source.mkdir()
        metadata = ["cmake_minimum_required(VERSION 3.20)", "project(metadata NONE)", *lines[:-2],
                    "add_library(trailmate_linux_oneseg_media INTERFACE)",
                    "set(TRAIL_MATE_LINUX_ENABLE_ONESEG_MEDIA ON)",
                    'set(TRAIL_MATE_LINUX_UX_PACK "t_display_p4_touch")',
                    f'set(TRAIL_MATE_REPO_ROOT "{repo}")',
                    "set(CMAKE_INSTALL_LIBEXECDIR libexec)", "set(CMAKE_INSTALL_DATADIR share)",
                    "set(TRAIL_MATE_DATA_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)",
                    f'include("{repo}/boards/t_display_cm0/package.cmake")',
                    f'file(WRITE "{root}/device-dependencies.txt" "${{TRAIL_MATE_DEVICE_DEBIAN_DEPENDS}}")']
        (source / "CMakeLists.txt").write_text("\n".join(metadata))
        subprocess.run(["cmake", "-S", str(source), "-B", str(root / "metadata-build")],
                       check=True, capture_output=True)
        assert ", ".join(expected) in (root / "device-dependencies.txt").read_text()
        for name in ("postinst", "prerm", "postrm"):
            generated = root / "metadata-build/packaging" / name
            content = generated.read_bytes()
            assert content.startswith(b"#!/bin/sh\n") and b"\r" not in content, generated
            original = (repo / "boards/t_display_cm0/packaging" / name).read_bytes()
            assert content == original.replace(b"\r\n", b"\n"), generated
            subprocess.run(["sh", "-n", str(generated)], check=True, capture_output=True)
        broken = root / "not-an-elf.so"
        broken.write_text("not a linked library")
        lines.insert(-2, f'set(TRAIL_MATE_ONESEG_avformat_LIBRARY "{broken}")')
        script.write_text("\n".join(lines))
        result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
        assert result.returncode and "Cannot determine Debian 1seg dependency" in result.stderr, result.stderr
        environment = os.environ.copy()
        environment["TRAIL_MATE_PACKAGE_ONESEG"] = "1"
        environment["TRAIL_MATE_ONESEG_BACKEND_DEB"] = str(args.host_backend_package.resolve())
        result = subprocess.run(["bash", str(repo / "apps/linux_lvgl/tools/build_cm0_deb.sh")],
                                env=environment, capture_output=True, text=True, timeout=10)
        assert result.returncode and "requires a trailmate-oneseg-backend armhf package" in result.stderr, result.stderr
        compatible = runpy.run_path(str(repo / "apps/linux_lvgl/tools/package_oneseg_backend.py"))["cm0_abi_compatible"]
        abi = dict(glibcxx_release=14, default_lock_policy="mutex", cxx11_abi=1)
        assert compatible("armhf", abi)
        assert not compatible("amd64", abi)
        for key, value in (("glibcxx_release", 13), ("default_lock_policy", "atomic"), ("cxx11_abi", 0)):
            assert not compatible("armhf", dict(abi, **{key: value}))
        # Explicitly empty metadata fixture: correct architecture, wrong ABI.
        stage = root / "wrong-abi/DEBIAN"
        stage.mkdir(parents=True)
        (stage / "control").write_text("Package: trailmate-oneseg-backend\nVersion: 1.0.0\nArchitecture: armhf\nMaintainer: Test Fixture\nDescription: TEST METADATA ONLY - WRONG ABI\n")
        wrong = root / "wrong-abi.deb"
        subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(stage.parent), str(wrong)], check=True, capture_output=True)
        environment["TRAIL_MATE_ONESEG_BACKEND_DEB"] = str(wrong)
        result = subprocess.run(["bash", str(repo / "apps/linux_lvgl/tools/build_cm0_deb.sh")],
                                env=environment, capture_output=True, text=True, timeout=10)
        assert result.returncode and "does not provide the required Raspbian C++ ABI" in result.stderr, result.stderr
        for version, capabilities in (("1.1.0", "trailmate-oneseg-backend-cm0-abi-v1"),
                                      ("1.0.0", "trailmate-oneseg-backend-cm0-abi-v1, trailmate-oneseg-backend-auto-v1")):
            (stage / "control").write_text(f"Package: trailmate-oneseg-backend\nVersion: {version}\nArchitecture: armhf\nMaintainer: Test Fixture\nProvides: {capabilities}\nDescription: TEST METADATA ONLY - NO AUTO BACKEND\n")
            subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(stage.parent), str(wrong)], check=True, capture_output=True)
            result = subprocess.run(["bash", str(repo / "apps/linux_lvgl/tools/build_cm0_deb.sh")],
                                    env=environment, capture_output=True, text=True, timeout=10)
            assert result.returncode and "automatic TMCC acquisition capability" in result.stderr, result.stderr
        print("SONAME dependencies:", ", ".join(expected))
        print("Real CM0 metadata contains dependencies and LF shell control scripts; non-ELF, wrong-architecture and wrong-ABI inputs rejected")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg-prefix", type=Path, required=True)
    parser.add_argument("--arib-prefix", type=Path, default=Path("/"))
    parser.add_argument("--host-backend-package", type=Path, required=True,
                        help="A real non-armhf backend package for the rejection check")
    run(parser.parse_args())
