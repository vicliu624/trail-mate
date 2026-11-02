"""Generated RS codewords through the actual GNU Radio decoder; no RF/media."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

def run():
    import ctypes
    import ctypes.util
    import time
    from gnuradio import gr, blocks, isdbt
    import pmt
    codec = ctypes.CDLL(ctypes.util.find_library('gnuradio-fec'))
    byte = ctypes.c_ubyte
    codec.init_rs_char.argtypes = [ctypes.c_uint]*5
    codec.init_rs_char.restype = ctypes.c_void_p
    codec.encode_rs_char.argtypes = [ctypes.c_void_p, ctypes.POINTER(byte), ctypes.POINTER(byte)]
    codec.decode_rs_char.argtypes = [ctypes.c_void_p, ctypes.POINTER(byte), ctypes.c_void_p, ctypes.c_int]
    codec.decode_rs_char.restype = ctypes.c_int
    codec.free_rs_char.argtypes = [ctypes.c_void_p]
    rs = codec.init_rs_char(8, 0x11d, 0, 1, 16)
    assert rs
    payload = bytes([0x47]+[(index*37+11)%256 for index in range(187)])
    padded = (byte*239).from_buffer_copy(bytes(51)+payload)
    parity = (byte*16)()
    codec.encode_rs_char(rs, padded, parity)
    encoded = payload+bytes(parity)
    words = []
    for errors in (0, 1, 8, 9):
        corrupted = bytearray(encoded)
        for index in range(errors): corrupted[index*11] ^= 0x5a
        proof = (byte*255).from_buffer_copy(bytes(51)+corrupted)
        assert codec.decode_rs_char(rs, proof, None, 0) == (errors if errors <= 8 else -1)
        words.append(bytes(corrupted))
    codec.free_rs_char(rs)
    os.environ['TRAIL_MATE_OFDM_DIAGNOSTICS'] = '1'
    class Collector(gr.basic_block):
        def __init__(self):
            gr.basic_block.__init__(self, name='native-rs-outcome-test', in_sig=None, out_sig=None)
            self.rows = []
            self.message_port_register_in(pmt.mp('in'))
            self.set_msg_handler(pmt.mp('in'), self.accept)
        def accept(self, message):
            self.rows.append(pmt.to_python(message))
    graph = gr.top_block()
    source = blocks.vector_source_b(list(b''.join(words)*16), False, 204)
    decoder = isdbt.reed_solomon_dec_isdbt()
    sink = blocks.vector_sink_b(188)
    collector = Collector()
    graph.connect(source, decoder, sink)
    graph.msg_connect(decoder, 'fec_statistics', collector, 'in')
    graph.run()
    deadline = time.monotonic()+2
    while (not collector.rows or collector.rows[-1]['words'] < 64) and time.monotonic() < deadline: time.sleep(.005)
    assert bytes(sink.data()) == payload*48
    stats = collector.rows[-1]
    assert stats['words'] == 64 and stats['clean'] == 16 and stats['corrected'] == 32
    assert stats['uncorrectable'] == 16 and stats['symbols'] == 144 and stats['maximum'] == 8
    print('Actual RS decoder: 64 words, clean16/corrected32/uncorrectable16, symbols144/max8; 48 exact output packets')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--child', action='store_true')
    args = parser.parse_args()
    if args.child:
        run()
    else:
        env = os.environ.copy()
        python = [str(p) for p in args.prefix.rglob('*') if p.is_dir() and p.name in ('site-packages', 'dist-packages')]
        libraries = sorted({str(p.parent) for p in args.prefix.rglob('libgnuradio-isdbt.so*')})
        env['PYTHONPATH'] = ':'.join(python+[env.get('PYTHONPATH', '')])
        env['LD_LIBRARY_PATH'] = ':'.join(libraries+[env.get('LD_LIBRARY_PATH', '')])
        subprocess.run([sys.executable, __file__, '--prefix', str(args.prefix), '--child'], env=env, check=True)
