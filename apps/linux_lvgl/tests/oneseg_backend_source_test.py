"""Check safety adaptation against the pinned archive without building or RF."""
import argparse
import hashlib
from pathlib import Path
import runpy
import subprocess
import tarfile
import tempfile


def check_viterbi_reset(original, adapted, compiler, directory):
    """Compile the exact shared reset body with poisoned counter ownership."""
    prefix = '''#include <cassert>
#include <cstdlib>
#include <sys/resource.h>
#define TRACEBACK_MAX 24
#define POLYA 79
#define POLYB 109
struct Counter {
    int store_pos;
    unsigned char d_Partab[256]{}, mmresult[64]{}, ppresult[24][64]{};
    struct Branch { unsigned char c[32]; } Branchtab27_generic[2]{};
    void reset(unsigned char* mm0, unsigned char* pp0) {
'''
    suffix = '''};
int main() {
    const rlimit no_core{0, 0}; setrlimit(RLIMIT_CORE, &no_core);
    Counter state{}; unsigned char metrics[64]{}, paths[64]{};
    state.store_pos = -1010580541;
    state.reset(metrics, paths); assert(state.store_pos == 0);
    state.store_pos = 17;
    state.reset(metrics, paths); assert(state.store_pos == 0);
}
'''
    for name, source in (("original", original), ("adapted", adapted)):
        text = source.decode()
        begin = text.index("                // Initialize starting metrics to prefer 0 state")
        end = text.index("\n#ifdef DTV_SSE2\n            void viterbi_decoder_impl::viterbi_butterfly", begin)
        path = directory / ("viterbi-" + name + ".cpp")
        path.write_text(prefix + text[begin:end] + suffix)
        executable = directory / ("viterbi-" + name)
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", str(path), "-o", str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True)
        if name == "original":
            assert result.returncode != 0 and b"state.store_pos == 0" in result.stderr
        else:
            assert result.returncode == 0, result.stderr
    print("Exact-source Viterbi reset: original fails; adapted initializes poisoned and reused counters")


def run(args):
    backend = runpy.run_path(str(args.builder))
    archive = args.archive.read_bytes()
    assert hashlib.sha256(archive).hexdigest() == backend["ARCHIVE_SHA"]
    with tarfile.open(args.archive) as stream:
        source = stream.extractfile("gr-isdbt-" + backend["REVISION"] + "/lib/ofdm_synchronization_1seg_impl.cc").read()
        tmcc = stream.extractfile("gr-isdbt-" + backend["REVISION"] + "/lib/tmcc_decoder_1seg_impl.cc").read()
        viterbi = stream.extractfile("gr-isdbt-" + backend["REVISION"] + "/lib/viterbi_decoder_impl.cc").read()
    with tempfile.TemporaryDirectory(prefix="backend-source-", dir=args.work_dir) as temporary:
        path = Path(temporary) / "source.cc"
        path.write_bytes(source)
        backend["repair_source"](path)
        patched = path.read_bytes()
        assert hashlib.sha256(patched).hexdigest() == backend["PATCHED_SHA"]
        backend["repair_source"](path)
        assert path.read_bytes() == patched
        changed = patched + b"\n// Local change must be preserved.\n"
        path.write_bytes(changed)
        try:
            backend["repair_source"](path)
        except RuntimeError:
            pass
        else:
            raise AssertionError("Modified source was not rejected")
        assert path.read_bytes() == changed
        path.write_bytes(tmcc)
        assert hashlib.sha256(tmcc).hexdigest() == backend["TMCC_ORIGINAL_SHA"]
        backend["adapt_tmcc_source"](path)
        adapted_tmcc = path.read_bytes()
        assert hashlib.sha256(adapted_tmcc).hexdigest() == backend["TMCC_PATCHED_SHA"]
        backend["adapt_tmcc_source"](path)
        assert path.read_bytes() == adapted_tmcc
        changed_tmcc = adapted_tmcc + b"\n// Preserve local TMCC changes.\n"
        path.write_bytes(changed_tmcc)
        try:
            backend["adapt_tmcc_source"](path)
        except RuntimeError:
            pass
        else:
            raise AssertionError("Modified TMCC source was not rejected")
        assert path.read_bytes() == changed_tmcc
        path.write_bytes(viterbi)
        assert hashlib.sha256(viterbi).hexdigest() == backend["VITERBI_ORIGINAL_SHA"]
        backend["repair_viterbi_source"](path)
        adapted_viterbi = path.read_bytes()
        assert hashlib.sha256(adapted_viterbi).hexdigest() == backend["VITERBI_PATCHED_SHA"]
        backend["repair_viterbi_source"](path)
        assert path.read_bytes() == adapted_viterbi
        changed_viterbi = adapted_viterbi + b"\n// Preserve local Viterbi changes.\n"
        path.write_bytes(changed_viterbi)
        try:
            backend["repair_viterbi_source"](path)
        except RuntimeError:
            pass
        else:
            raise AssertionError("Modified Viterbi source was not rejected")
        assert path.read_bytes() == changed_viterbi
        if getattr(args, "compiler", None):
            check_viterbi_reset(viterbi, adapted_viterbi, args.compiler, Path(temporary))
    print("Pinned OFDM/TMCC/Viterbi adaptations: exact sources, idempotence, local-change preservation passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--builder", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--compiler", help="Optional native POSIX C++ compiler for exact-source reset regression")
    run(parser.parse_args())
