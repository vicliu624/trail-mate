"""Pinned optimization roundtrip/tamper tests; no compiler, installation or RF."""
import argparse
import importlib.util
from pathlib import Path
import tempfile
import tarfile

parser = argparse.ArgumentParser()
parser.add_argument('--builder', type=Path, required=True)
parser.add_argument('--archive', type=Path, required=True)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('builder', args.builder)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    with tarfile.open(args.archive) as archive:
        prefix = 'gr-isdbt-'+builder.REVISION+'/lib/'
        for name in ('ofdm_synchronization_1seg_impl.cc', 'ofdm_synchronization_1seg_impl.h', 'viterbi_decoder_impl.h', 'CMakeLists.txt'):
            (root/name).write_bytes(archive.extractfile(prefix+name).read())
    source, sync_header = root/'ofdm_synchronization_1seg_impl.cc', root/'ofdm_synchronization_1seg_impl.h'
    header, cmake = root/'viterbi_decoder_impl.h', root/'CMakeLists.txt'
    builder.repair_source(source)
    builder.lab_adapter().adapt(source, sync_header)
    original = [path.read_bytes() for path in (source, header, cmake)]
    adapter = builder.cm0_adapter()
    for simde in (False, True):
        receipt = adapter.adapt(source, header, cmake, simde=simde)
        assert adapter.adapt(source, header, cmake, simde=simde) == receipt
        assert 'mode == 3' in source.read_text()
        assert 'tmcc_and_ac_positions(static_cast<int>(std::pow(2.0, 7 + mode)))' in source.read_text()
        adapter.restore(source, header, cmake)
        assert [path.read_bytes() for path in (source, header, cmake)] == original
    adapter.adapt(source, header, cmake, simde=True)
    header.write_bytes(header.read_bytes()+b'\n// User change\n')
    before = [path.read_bytes() for path in (source, header, cmake)]
    try: adapter.restore(source, header, cmake)
    except RuntimeError: pass
    else: raise AssertionError('Unknown source change was not preserved')
    assert [path.read_bytes() for path in (source, header, cmake)] == before
print('CM0 compact/SIMDe exact roundtrip, pilot indexing and tamper preservation passed')
