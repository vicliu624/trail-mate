#!/usr/bin/python3
"""Contract responder for native ownership/cancellation tests, not a TMAP writer."""
import json
import os
import pathlib
import sys
import time

if sys.argv[1] == 'verify':
    assert pathlib.Path(sys.argv[2]).read_bytes().startswith(b'TMAP-test')
    sys.exit(0)
plan = json.loads(pathlib.Path(sys.argv[2]).read_text())
assert os.read(198, 32), 'PBF descriptor was not inherited'
cache = pathlib.Path(plan['Tmap']['SourceCacheDirectory'])
assert cache.parent == pathlib.Path(sys.argv[2]).parent, 'Cache must belong to this job'
cache.mkdir()
(cache / 'downloaded-tile.bin').write_bytes(b'temporary source raster')
(cache.parent / 'producer.pid').write_text(str(os.getpid()))
if plan['Name'] == 'Cancel':
    while True:
        time.sleep(.05)
if plan['Name'] == 'Fail':
    print('Contract producer failure', file=sys.stderr)
    sys.exit(1)
output = pathlib.Path(plan['OutputDirectory']) / 'maps/tmap/osm'
output.mkdir(parents=True)
filename = pathlib.Path(plan['Tmap']['FileName']).stem + '-osm.tmap'
(output / filename).write_bytes(b'TMAP-test' + bytes(256))
(output / (filename + '.font-resources.json')).write_text('{}')
(output.parent / 'test.source-geometry.json').write_text('{}')
print('tiles: 1/1 z13', file=sys.stderr)
