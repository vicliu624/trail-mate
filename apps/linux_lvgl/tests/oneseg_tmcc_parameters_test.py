"""Layer decoding contract for native, parity-validated TMCC messages; no RF."""
from pathlib import Path
import runpy

decoder = runpy.run_path(str(Path(__file__).parents[1] / "tools/oneseg_decode.py"))
parse = decoder["tmcc_parameters"]


def frame(modulation=1, rate=1, interleaving=3, segments=1, partial=1):
    # This is an explicit semantic-parser fixture, not a parity-valid RF frame.
    bits = [0] * 204
    bits[1:17] = (0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0)
    bits[27] = partial
    for start, width, value in ((28, 3, modulation), (31, 3, rate), (34, 3, interleaving), (37, 4, segments)):
        bits[start:start + width] = [(value >> shift) & 1 for shift in reversed(range(width))]
    return bits


def rejects(bits, mode=3, guard=16):
    try:
        parse(bits, mode, guard)
    except ValueError:
        return
    raise AssertionError("Unsupported or malformed TMCC was accepted")


checks = 0
for mode in (1, 2, 3):
    for modulation, constellation in ((1, 4), (2, 16), (3, 64)):
        for rate in range(5):
            for interleaving, depth in enumerate((0, 1, 2, 4)):
                for guard in (4, 8, 16, 32):
                    parsed = parse(frame(modulation, rate, interleaving), mode, guard)
                    assert parsed["constellation"] == constellation
                    assert parsed["time_interleaving"] == depth * 2 ** (3 - mode)
                    assert parsed["code_rate_index"] == rate and parsed["guard_denominator"] == guard
                    checks += 1
odd = frame()
odd[1:17] = [1 - bit for bit in odd[1:17]]
assert parse(odd, 3, 16)["modulation"] == "QPSK"
bad_sync = frame()
bad_sync[16] ^= 1  # Check all 16 sync bits, including the final bit.
rejects(bad_sync)
rejects(frame()[:-1])
rejects(frame() + [0])
bad_bit = frame()
bad_bit[100] = 2
rejects(bad_bit)
rejects(frame(), mode=0)
rejects(frame(), guard=64)
rejects(frame(modulation=0))
rejects(frame(modulation=7))
rejects(frame(rate=7))
rejects(frame(interleaving=7))
rejects(frame(segments=13))
rejects(frame(partial=0))
sound = frame()
sound[21] = 1
rejects(sound)
print(f"TMCC current-layer semantics: {checks} supported combinations and malformed/unsupported rejection passed")
