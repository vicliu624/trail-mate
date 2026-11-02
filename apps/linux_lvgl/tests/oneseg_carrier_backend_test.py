"""Verify exact reversible coherent-carrier instrumentation without RF."""
import argparse
import runpy
import tarfile
import tempfile
from pathlib import Path


def run(args):
    builder = runpy.run_path(str(args.builder.resolve()))
    adapter = builder['carrier_adapter']()
    with tempfile.TemporaryDirectory(prefix='trailmate-carrier-adapter-') as temporary:
        root = Path(temporary)
        with tarfile.open(args.archive) as stream:
            stream.extractall(root, filter='data')
        lib = root/('gr-isdbt-'+builder['REVISION'])/'lib'
        source, header = lib/'ofdm_synchronization_1seg_impl.cc', lib/'ofdm_synchronization_1seg_impl.h'
        builder['repair_source'](source)
        builder['lab_adapter']().adapt(source,header)
        originals = source.read_bytes(), header.read_bytes()
        for compact in (False,True):
            if compact:
                builder['cm0_adapter']().adapt(source,lib/'viterbi_decoder_impl.h',lib/'CMakeLists.txt')
            before = source.read_bytes(),header.read_bytes()
            receipt = adapter.adapt(source,header)
            adapted = source.read_bytes(),header.read_bytes()
            assert receipt['schema']==1 and receipt['consecutive_symbols']==4
            assert adapter.adapt(source,header)==receipt
            assert (source.read_bytes(),header.read_bytes())==adapted
            header.write_bytes(header.read_bytes()+b'\n// unknown header edit\n')
            try:
                adapter.restore(source,header)
            except RuntimeError:
                assert source.read_bytes()==adapted[0]
            else:
                raise AssertionError('Unknown header modification overwritten')
            header.write_bytes(adapted[1])
            adapter.restore(source,header)
            assert (source.read_bytes(),header.read_bytes())==before
            if compact:
                builder['cm0_adapter']().restore(source,lib/'viterbi_decoder_impl.h',lib/'CMakeLists.txt')
        assert (source.read_bytes(),header.read_bytes())==originals
    print('Coherent carrier adapter: full/compact roundtrip, idempotence and atomic tamper rejection passed')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--builder',type=Path,required=True)
    parser.add_argument('--archive',type=Path,required=True)
    run(parser.parse_args())
