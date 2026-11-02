"""Check adaptation against pinned native source, including rollback integrity."""
import argparse
import importlib.util
from pathlib import Path
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--adapter', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--header', type=Path, required=True)
parser.add_argument('--work-dir', type=Path, required=True)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('sync_lab', args.adapter)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
args.work_dir.mkdir(parents=True, exist_ok=True)
source, header = args.work_dir/'sync.cc', args.work_dir/'sync.h'
shutil.copyfile(args.source, source)
shutil.copyfile(args.header, header)
baseline_source, baseline_header = source.read_bytes(), header.read_bytes()
receipt = module.adapt(source, header)
assert receipt['schema'] == 2 and receipt['phase_unit'] == 'radians_per_useful_symbol'
assert module.is_instrumented_source(source.read_bytes())
assert module.verified_baseline(source.read_bytes(), 'source').encode() == baseline_source
assert module.verified_baseline(header.read_bytes(), 'header').encode() == baseline_header
adapted = source.read_bytes(), header.read_bytes()
assert module.adapt(source, header) == receipt
assert (source.read_bytes(), header.read_bytes()) == adapted
for legacy in (False, True):
    source.write_text(module.transform(baseline_source.decode(), module.source_replacements(legacy, forecast=False)))
    header.write_text(module.transform(baseline_header.decode(), module.header_replacements(legacy, readback=False)))
    assert module.adapt(source, header) == receipt
    assert (source.read_bytes(), header.read_bytes()) == adapted
source.write_text(module.transform(baseline_source.decode(), module.source_replacements(False, forecast=True, readback=False)))
header.write_text(module.transform(baseline_header.decode(), module.header_replacements(False)))
assert module.adapt(source, header) == receipt
source.write_text(module.transform(baseline_source.decode(), module.source_replacements(False, forecast=True, readback=True, reliable_ack=False)))
header.write_text(module.transform(baseline_header.decode(), module.header_replacements(False, readback=False)))
assert module.adapt(source, header) == receipt
header.write_bytes(adapted[1]+b'\n// local modification\n')
before = source.read_bytes(), header.read_bytes()
try:
    module.adapt(source, header)
except RuntimeError:
    pass
else:
    raise AssertionError('Modified native source must not be overwritten')
assert (source.read_bytes(), header.read_bytes()) == before
header.write_bytes(adapted[1])
assert module.adapt(source, header) == receipt
print('Pinned native OFDM tracking adaptation, inverse validation and idempotence passed')
