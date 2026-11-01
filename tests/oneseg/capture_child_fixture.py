#!/usr/bin/python3
"""Process/pipe protocol fixture. Never opens a radio or generates RF."""
import os
import signal
import struct
import sys
import time

assert os.environ['TRAIL_MATE_ONESEG_CAPTURE_CHILD'] == '1'
mode = os.environ.get('ONESEG_CAPTURE_FIXTURE', 'normal')
expected = ['--profile', '/fixture.conf', '--channel', '13', '--capture-result-fd', '3']
if mode == 'tuning':
    expected += ['--sample-rate', '450000', '--gain-step', '13', '--spi-clock', '16000000']
assert sys.argv[1:] == expected
stopped = False
signal.signal(signal.SIGTERM, lambda *_: globals().__setitem__('stopped', True))
if mode == 'hung':
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    while True:
        time.sleep(.01)
data = bytes(range(256)) * 8
if mode == 'odd':
    data += b'x'
position = 0
for count in (1, 3, 9, 257, 511, 2048):
    chunk = data[position:position+count]
    if chunk:
        os.write(1, chunk)
        position += len(chunk)
if mode == 'cancel':
    while not stopped:
        time.sleep(.005)
if mode in ('missing', 'reject'):
    raise SystemExit(0)
wire = struct.pack('<8I3Q', 0x43514d54 if mode != 'magic' else 0, 1,
                   0xfffffffc if mode == 'error' else 0, 0,
                   576000 if mode == 'rate' else 450000, 600,
                   0x100 if stopped else 0, 0, len(data), 1234567, 234)
if mode == 'short':
    wire = wire[:16]
if mode == 'extra':
    wire += b'x'
os.write(3, wire)
raise SystemExit(3 if mode == 'error' else 0)
