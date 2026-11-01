"""Exercise the production IQ source without replacing its queue/work logic."""
import ast
from collections import deque
from pathlib import Path
import os
import queue
import select
import sys
import threading
import time
from types import SimpleNamespace
from unittest.mock import patch

try:
    import numpy as np
except ImportError:
    print('IQ source regression requires NumPy')
    raise SystemExit(77)


class Block:
    def __init__(self, *args, **kwargs):
        pass


source = Path(sys.argv[1])
tree = ast.parse(source.read_text(encoding="utf-8"))
definition = next(node for node in ast.walk(tree) if isinstance(node, ast.ClassDef) and node.name == "IqInput")


def fixture():
    stop = threading.Event()
    failures = []

    def fail(message):
        failures.append(message)
        stop.set()

    context = dict(gr=SimpleNamespace(sync_block=Block), np=np, queue=queue, deque=deque,
                   threading=threading, stop=stop, fail=fail, automatic=False,
                   os=os, select=select, sys=sys, time=time)
    exec(compile(ast.Module(body=[definition], type_ignores=[]), str(source), "exec"), context)
    return context["IqInput"](), stop, failures


input_block, stop, failures = fixture()
assert input_block.queue.maxsize*input_block.block_bytes == 9216000
raw = ((np.arange(10240) % 63) - 31).astype(np.int8)
for start in range(0, len(raw), 1280):
    input_block.queue.put(raw[start:start + 1280].tobytes())
output = np.empty(4096, dtype=np.complex64)
assert input_block.work([], [output]) == 4096, "Available IQ must fill the scheduler buffer in batches"
expected = (raw[0::2].astype(np.float32) + 1j * raw[1::2].astype(np.float32)) / 128
np.testing.assert_array_equal(output, expected[:4096])
count = input_block.work([], [output])
assert count == 1024
np.testing.assert_array_equal(output[:count], expected[4096:])
assert input_block.processed_pairs == 5120 and not failures

# Replay is older than newly queued IQ; the split signed pair stays contiguous.
input_block, stop, failures = fixture()
input_block.replay.extend([bytes([1]), bytes([2, 3])])
input_block.queue.put(bytes([4, 5, 6]))
assert input_block.work([], [output]) == 3
np.testing.assert_array_equal(output[:3], np.array([1 + 2j, 3 + 4j, 5 + 6j]) / 128)

input_block, stop, failures = fixture()
input_block.pending = bytes([1])
input_block.eof.set()
assert input_block.work([], [output]) == -1 and failures == ["Incomplete signed IQ8 pair at end of input"]

input_block, stop, failures = fixture()
input_block.queue.put(bytes([1, 2]))
stop.set()
assert input_block.work([], [output]) == -1 and input_block.processed_pairs == 0 and input_block.queue.qsize() == 1

# Exercise the actual reader against many small live pipe writes, including an
# odd final byte. Aggregation must preserve every byte and flush at EOF.
input_block, stop, failures = fixture()
read_fd, write_fd = os.pipe()
raw_bytes = ((np.arange(20001) % 63)-31).astype(np.int8).tobytes()
with patch.object(sys, 'stdin', SimpleNamespace(fileno=lambda:read_fd)):
    input_block.reader.start()
    for offset in range(0,len(raw_bytes),400):
        assert os.write(write_fd,raw_bytes[offset:offset+400]) == len(raw_bytes[offset:offset+400])
        time.sleep(.0005)
    os.close(write_fd)
    input_block.reader.join(1)
    assert not input_block.reader.is_alive()
os.close(read_fd)
batches = []
while not input_block.queue.empty():
    batches.append(input_block.queue.get_nowait())
assert b''.join(batches) == raw_bytes and input_block.read_bytes == len(raw_bytes)
assert max(map(len,batches)) <= input_block.block_bytes and any(len(data)>1280 for data in batches)
assert input_block.eof.is_set() and not failures

# Sparse input must become available while the writer is still open; stopping
# the reader must not wait for a full batch or for EOF from the producer.
input_block, stop, failures = fixture()
read_fd, write_fd = os.pipe()
with patch.object(sys, 'stdin', SimpleNamespace(fileno=lambda:read_fd)):
    input_block.reader.start()
    os.write(write_fd,b'\x01\x02')
    assert input_block.queue.get(timeout=.2) == b'\x01\x02'
    stop.set()
    input_block.reader.join(.3)
    assert not input_block.reader.is_alive() and not failures
os.close(write_fd)
os.close(read_fd)
print("Production IQ batching, live pipe aggregation, bounded sparse latency, remainder, replay order, split pair, EOF and cancellation passed")
