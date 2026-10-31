#!/usr/bin/env python3
"""Build a pinned GPL-3.0-or-later gr-isdbt backend in a private prefix.

Uses existing GNU Radio 3.10 development dependencies. No sudo, package manager,
system install, cross-ABI assumption or hardware access. Requires Python 3.12+.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import tarfile
import urllib.request

REVISION = "b576783021fb5c18f1903c534075146c31b21e90"
ARCHIVE_SHA = "4e79ddb4d65de27ab6dd3a59cc6e73af68744ccee8d57b687cfa1f21878e4f31"
ORIGINAL_SHA = "179821cc4047e5409d894020deaedaa7e9c744647d71eb57db80ca7559a3e936"
PATCHED_SHA = "854af19ab5004fa0d8ee728fa9caf3429974e4957a18288797bac83e1e902cfe"
TMCC_ORIGINAL_SHA = "be9dc6009b8314a128d5bbac198ff0fee443d140ea8fcf4d6537c0b4728600aa"
TMCC_PATCHED_SHA = "fef34ebae499429096ff3f647c9e8ad88bb92ee04c27f7032eca3dd5bb799f54"
VITERBI_ORIGINAL_SHA = "07d7ae689b2e731645c2440348812ba700a7f88905c61e4ff374282be1a614eb"
VITERBI_PATCHED_SHA = "18e2b20024daa00397597ec383980524b7f773e2e70d42a8522c83da9d958ed8"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def repair_source(path):
    data = path.read_bytes()
    if sha(data) == PATCHED_SHA:
        return
    if sha(data) != ORIGINAL_SHA:
        raise RuntimeError("Preserving modified upstream source; hash differs: " + str(path))
    text = data.decode()
    replacements = [
        ("#include <complex>\n", "#include <complex>\n#include <algorithm>\n"),
        ("            volk_free(d_known_phase_diff_ac); \n",
         "            delete[] d_known_phase_diff_ac;\n            volk_free(d_corr_sp);\n"),
        ("\n                assert(lookup_start >= lookup_stop);",
         "                // Failed acquisition must not publish uninitialized estimates.\n"
         "                *cp_pos = 0;\n                *peak_epsilon = 0.0f;\n"
         "                // The tracking window may cross the allocated norm workspace.\n"
         "                lookup_start = std::min(lookup_start, 2*d_fft_length+d_cp_length-1);\n"
         "                if (lookup_stop < d_cp_length+d_fft_length-1 || lookup_start <= lookup_stop)\n"
         "                    return false;\n                assert(lookup_start >= lookup_stop);")]
    for before, after in replacements:
        if text.count(before) != 1:
            raise RuntimeError("Upstream safety adaptation anchor differs")
        text = text.replace(before, after, 1)
    patched = text.encode()
    if sha(patched) != PATCHED_SHA:
        raise RuntimeError("Safety adaptation hash differs")
    path.write_bytes(patched)


def adapt_tmcc_source(path):
    """Expose parity-checked TMCC frames through a versioned GNU Radio port.

    The raw bits preserve current/future layer information. Consumers must not
    infer station discovery from this event: valid transport services are still
    required. No stdout parsing or additional RF access is involved.
    """
    data = path.read_bytes()
    if sha(data) == TMCC_PATCHED_SHA:
        return
    if sha(data) != TMCC_ORIGINAL_SHA:
        raise RuntimeError("Preserving modified TMCC source; hash differs: " + str(path))
    text = data.decode()
    replacements = [
        ('#include <cmath>\n', '#include <cmath>\n#include <pmt/pmt.h>\n'),
        ('            d_print_params = print_params; \n',
         '            d_print_params = print_params;\n'
         '            message_port_register_out(pmt::mp("tmcc_parameters"));\n'),
        ('                            end_frame = 1;\n',
         '                            // Publish only after the native BCH parity check succeeds.\n'
         '                            pmt::pmt_t bits = pmt::make_u8vector(204, 0);\n'
         '                            for (size_t bit = 0; bit < 204; ++bit)\n'
         '                                pmt::u8vector_set(bits, bit, d_rcv_tmcc_data[bit]);\n'
         '                            pmt::pmt_t parameters = pmt::make_dict();\n'
         '                            parameters = pmt::dict_add(parameters, pmt::mp("schema"), pmt::from_long(1));\n'
         '                            const int mode = d_active_carriers == 108 ? 1 : d_active_carriers == 216 ? 2 : 3;\n'
         '                            parameters = pmt::dict_add(parameters, pmt::mp("mode"), pmt::from_long(mode));\n'
         '                            parameters = pmt::dict_add(parameters, pmt::mp("bits"), bits);\n'
         '                            message_port_pub(pmt::mp("tmcc_parameters"), parameters);\n'
         '                            end_frame = 1;\n')]
    for before, after in replacements:
        if text.count(before) != 1:
            raise RuntimeError("Upstream TMCC adaptation anchor differs")
        text = text.replace(before, after, 1)
    patched = text.encode()
    if sha(patched) != TMCC_PATCHED_SHA:
        raise RuntimeError("TMCC adaptation hash differs")
    path.write_bytes(patched)


def repair_viterbi_source(path):
    """Initialize traceback position on construction and every decoder reset.

    The original uninitialized signed position can produce a negative circular
    buffer index, writing before ppresult into native library global data.
    """
    data = path.read_bytes()
    if sha(data) == VITERBI_PATCHED_SHA:
        return
    if sha(data) != VITERBI_ORIGINAL_SHA:
        raise RuntimeError("Preserving modified Viterbi source; hash differs: " + str(path))
    before = b"                // Initialize starting metrics to prefer 0 state\n"
    if data.count(before) != 1:
        raise RuntimeError("Upstream Viterbi reset anchor differs")
    patched = data.replace(before, before + b"                store_pos = 0;\n", 1)
    if sha(patched) != VITERBI_PATCHED_SHA:
        raise RuntimeError("Viterbi safety adaptation hash differs")
    path.write_bytes(patched)


def probe_cxx_abi(build_dir):
    commands = json.loads((build_dir / "compile_commands.json").read_text())
    entry = next(item for item in commands if Path(item["file"]).name == "ofdm_synchronization_1seg_impl.cc")
    original = entry.get("arguments") or shlex.split(entry["command"])
    arguments, skip, removed_source = [], False, False
    source = Path(entry["file"]).resolve()
    for token in original:
        if skip:
            skip = False
            continue
        if token in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
        elif token in ("-c", "-MD", "-MMD", "-MP"):
            continue
        elif not token.startswith("-") and (Path(entry["directory"]) / token).resolve() == source:
            removed_source = True
        else:
            arguments.append(token)
    if not removed_source:
        raise RuntimeError("Cannot isolate the backend's actual C++ compile command")
    result = subprocess.run(arguments + ["-dM", "-E", "-x", "c++", "-"],
                            input="#include <ext/concurrence.h>\n", cwd=entry["directory"],
                            check=True, text=True, capture_output=True)
    macros = {}
    for line in result.stdout.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) == 3 and fields[0] == "#define":
            macros[fields[1]] = fields[2]
    if "_GLIBCXX_RELEASE" not in macros or "_GLIBCXX_USE_CXX11_ABI" not in macros:
        raise RuntimeError("Backend package requires an identified libstdc++ ABI")
    # Mirrors ext/concurrence.h without changing any private library macro.
    policy = "single" if "__GTHREADS" not in macros else (
        "atomic" if "_GLIBCXX_HAVE_ATOMIC_LOCK_POLICY" in macros else "mutex")
    return dict(glibcxx_release=int(macros["_GLIBCXX_RELEASE"]), default_lock_policy=policy,
                cxx11_abi=int(macros["_GLIBCXX_USE_CXX11_ABI"]), compiler_major=int(macros["__GNUC__"]),
                atomic_bool_lock_free=int(macros["__GCC_ATOMIC_BOOL_LOCK_FREE"]))


def build(arguments):
    if sys.version_info < (3, 12) or platform.system() != "Linux":
        raise RuntimeError("Build natively on Linux with Python 3.12 or newer")
    cm0 = getattr(arguments, "cm0", False)
    if cm0 and subprocess.check_output(["dpkg", "--print-architecture"], text=True).strip() != "armhf":
        raise RuntimeError("CM0 mode requires a native Raspbian armhf build environment")
    if cm0:
        release = platform.freedesktop_os_release()
        if release.get("ID") != "raspbian" or release.get("VERSION_CODENAME") != "trixie":
            raise RuntimeError("CM0 mode requires the matching Raspbian Trixie userspace")
    root = arguments.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    archive = root / "source.tar.gz"
    if not archive.exists():
        with urllib.request.urlopen("https://codeload.github.com/dragoon2014/gr-isdbt/tar.gz/" + REVISION,
                                    timeout=60) as response:
            data = response.read(10000001)
        if len(data) > 10000000 or sha(data) != ARCHIVE_SHA:
            raise RuntimeError("Source archive verification failed")
        archive.write_bytes(data)
    if sha(archive.read_bytes()) != ARCHIVE_SHA:
        raise RuntimeError("Cached source archive verification failed")
    source = root / ("gr-isdbt-" + REVISION)
    if not source.exists():
        with tarfile.open(archive) as stream:
            stream.extractall(root, filter="data")
    repair_source(source / "lib/ofdm_synchronization_1seg_impl.cc")
    adapt_tmcc_source(source / "lib/tmcc_decoder_1seg_impl.cc")
    repair_viterbi_source(source / "lib/viterbi_decoder_impl.cc")
    prefix = root / "prefix"
    environment = os.environ.copy()
    # The upstream project otherwise overrides CMAKE_INSTALL_PREFIX from this.
    environment.pop("PYBOMBS_PREFIX", None)
    cpu_flags = ["-march=armv7-a", "-mfpu=vfpv3-d16", "-mfloat-abi=hard"] if cm0 else []
    target_configuration = []
    if cm0:
        for language in ("C", "CXX"):
            inherited = environment.get("CFLAGS" if language == "C" else "CXXFLAGS", "")
            target_configuration.append("-DCMAKE_" + language + "_FLAGS=" +
                                        " ".join([inherited, *cpu_flags]).strip())
    commands = [
        ["cmake", "-S", str(source), "-B", str(root / "build"), "-G", "Unix Makefiles",
         "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
         "-DENABLE_PYTHON=ON", "-DENABLE_DOXYGEN=OFF", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
         "-DPYTHON_EXECUTABLE=" + sys.executable, *target_configuration],
        ["cmake", "--build", str(root / "build"), "--parallel", str(arguments.jobs)],
        ["cmake", "--install", str(root / "build")]]
    for command in commands:
        subprocess.run(command, env=environment, check=True)
    abi = probe_cxx_abi(root / "build")
    if cm0 and (abi["glibcxx_release"] != 14 or abi["default_lock_policy"] != "mutex" or
                abi["cxx11_abi"] != 1 or abi["atomic_bool_lock_free"] != 2):
        raise RuntimeError("CM0 C++ ABI/cancellation contract differs: " + json.dumps(abi))
    manifest = dict(upstream="https://github.com/dragoon2014/gr-isdbt", revision=REVISION,
                    archive_sha256=ARCHIVE_SHA, adapted_source_sha256=PATCHED_SHA,
                    tmcc_source_sha256=TMCC_PATCHED_SHA, tmcc_message_schema=1,
                    viterbi_source_sha256=VITERBI_PATCHED_SHA,
                    license="GPL-3.0-or-later", machine=platform.machine(),
                    python=sys.version.split()[0], cxx_abi=abi,
                    target="cm0-armv7" if cm0 else "native", cpu_flags=cpu_flags,
                    native_device_qualification=False,
                    python_paths=[str(p) for pattern in ("lib/python*/site-packages", "lib/python*/dist-packages")
                                  for p in prefix.glob(pattern)],
                    library_paths=sorted({str(p.parent) for p in prefix.rglob("libgnuradio-isdbt.so*")}))
    (root / "backend-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--cm0", action="store_true", help="Select and qualify the native Raspbian CM0 ARMv7 target")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error("--jobs must be between one and 32")
    build(args)
