"""Exercise the production TS sink against a real bounded pipe, without RF."""
import ast
import fcntl
import os
from pathlib import Path
import select
import sys
import threading
import time

import numpy as np
from gnuradio import gr

source = Path(sys.argv[1])
tree = ast.parse(source.read_text(encoding='utf-8'))
definition = next(node for node in ast.walk(tree) if isinstance(node, ast.ClassDef) and node.name == 'TsOutput')
code = compile(ast.Module(body=[definition], type_ignores=[]), str(source), 'exec')
for case in ('recover', 'timeout', 'cancel'):
    reader, writer = os.pipe2(os.O_CLOEXEC | os.O_NONBLOCK)
    timer = None
    try:
        capacity = fcntl.fcntl(writer, fcntl.F_SETPIPE_SZ, 4096)
        assert os.write(writer, bytes(capacity)) == capacity
        stop = threading.Event()
        failures = []
        context = dict(gr=gr, np=np, stop=stop, output=writer, os=os,
                       select=select, time=time,
                       fail=lambda message: (failures.append(message), stop.set()))
        exec(code, context)
        sink = context['TsOutput']()
        rows = np.zeros((4, 188), dtype=np.uint8)
        rows[:, 0] = 0x47
        if case == 'recover':
            timer = threading.Timer(.35, os.read, args=(reader, capacity))
        elif case == 'cancel':
            timer = threading.Timer(.03, stop.set)
        if timer:
            timer.start()
        started = time.monotonic()
        accepted = sink.work([rows], [])
        elapsed = time.monotonic() - started
        if timer:
            timer.join()
        if case == 'recover':
            assert accepted == len(rows) and not failures
            assert .3 <= elapsed < 1.5
            assert os.read(reader, capacity) == rows.tobytes()
            assert sink.bytes_written == rows.nbytes and sink.packets == len(rows)
        elif case == 'cancel':
            assert accepted == -1 and not failures and elapsed < .5
            assert sink.bytes_written == 0 and sink.packets == 0
        else:
            assert accepted == -1 and failures == ['TS consumer remained blocked for 4 seconds']
            assert 3.9 <= elapsed < 5
            assert sink.bytes_written == 0 and sink.packets == 0
        print(f'TS bounded pipe {case}: {elapsed:.3f}s')
    finally:
        if timer:
            timer.cancel()
            timer.join()
        os.close(reader)
        os.close(writer)
