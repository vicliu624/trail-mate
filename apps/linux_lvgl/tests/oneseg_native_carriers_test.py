"""Compare native carrier snapshots with actual stream output; offline IQ only."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import time


def run(args):
    import numpy as np
    import pmt
    from gnuradio import gr, blocks, isdbt
    spec = importlib.util.spec_from_file_location('carrier_decoder',args.decoder)
    decoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(decoder)
    os.environ['TRAIL_MATE_OFDM_DIAGNOSTICS']='1'
    os.environ['TRAIL_MATE_OFDM_EQUALIZER_FLOOR']='0.04'
    if args.compact:
        os.environ['TRAIL_MATE_ONESEG_COMPACT_FFT']='576'
    else:
        os.environ.pop('TRAIL_MATE_ONESEG_COMPACT_FFT',None)
    raw = np.fromfile(args.recorded_iq,dtype=np.int8,count=args.input_rate*2*3).reshape(-1,2)
    iq=(raw[:,0].astype(np.float32)+1j*raw[:,1].astype(np.float32))/128

    class Collector(gr.basic_block):
        def __init__(self):
            gr.basic_block.__init__(self,'Coherent carrier test',in_sig=None,out_sig=None)
            self.rows=[]
            self.message_port_register_in(pmt.mp('in'))
            self.set_msg_handler(pmt.mp('in'),self.accept)
        def accept(self,message):
            self.rows.append(pmt.to_python(message))

    graph=gr.top_block()
    source=decoder._complex_capture_source(iq)
    resampler=decoder.input_resampler(args.input_rate,4000000,7) if args.compact else decoder.input_resampler(args.input_rate)
    sync=decoder.synchronization_block(isdbt,3,16,compact=args.compact)
    sink=blocks.vector_sink_c(432)
    collector=Collector()
    tracking=Collector()
    graph.connect(source,resampler,sync,sink)
    graph.msg_connect(sync,'ofdm_carriers',collector,'in')
    graph.msg_connect(sync,'ofdm_tracking',tracking,'in')
    graph.run()
    stream=np.asarray(sink.data(),dtype=np.complex64).reshape(-1,432)
    expected=sum((i%512)<4 for i in range(len(stream)))
    deadline=time.monotonic()+2
    while len(collector.rows)<expected and time.monotonic()<deadline:
        time.sleep(.005)
    assert len(collector.rows)==expected and expected>=16, (len(collector.rows),expected)
    previous=0
    valid=0
    needed={int(row['sequence']) for row in collector.rows if row['phase_valid'] and not row['cp_moved']}
    while not needed.issubset({int(row['symbol']) for row in tracking.rows}) and time.monotonic()<deadline:
        time.sleep(.005)
    phase_rows={int(row['symbol']):row for row in tracking.rows}
    for row in collector.rows:
        sequence=int(row['sequence'])
        assert row['schema']==1 and sequence>previous
        previous=sequence
        assert row['burst']==(sequence-1)//512+1 and row['slot']==(sequence-1)%512<4
        assert row['phase'] in range(4) and isinstance(row['phase_valid'],bool)
        assert row['fft_length']==(576 if args.compact else 1024)
        assert row['cp_length']==row['fft_length']//16
        arrays=[np.asarray(row[key]) for key in ('raw','equalized','channel','pilot_reference')]
        assert all(value.shape==(432,) for value in arrays)
        y,x,h,pilots=arrays
        np.testing.assert_allclose(x,stream[sequence-1],rtol=0,atol=0,equal_nan=True)
        calculated=y*np.conj(h)/(np.abs(h)**2+row['equalizer_floor'])
        np.testing.assert_allclose(x,calculated,rtol=5e-6,atol=5e-6,equal_nan=True)
        assert np.isfinite(y).all() and np.isfinite(h).all() and np.isfinite(pilots).all()
        if row['phase_valid'] and not row['cp_moved']:
            valid+=1
            assert sequence in phase_rows and phase_rows[sequence]['schema']==2, sequence
            selected=np.arange(432)%12==3*row['phase']
            assert selected.sum()==36
            np.testing.assert_allclose(h[selected],y[selected]/pilots[selected],rtol=2e-5,atol=2e-5)
    assert valid>=4, valid
    print(f'Native coherent carriers: {len(collector.rows)} complete symbols, {valid} acquired; stream equality, phase linkage and pre-equalizer/pilot algebra passed; no RF')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix',type=Path,required=True)
    parser.add_argument('--decoder',type=Path,required=True)
    parser.add_argument('--recorded-iq',type=Path,required=True)
    parser.add_argument('--input-rate',type=int,default=576000,choices=(450000,500000,576000))
    parser.add_argument('--compact',action='store_true')
    parser.add_argument('--child',action='store_true')
    args=parser.parse_args()
    if args.child:
        run(args)
    else:
        env=os.environ.copy()
        env['PYTHONPATH']=':'.join(str(p) for p in args.prefix.rglob('*') if p.is_dir() and p.name in ('site-packages','dist-packages'))+':'+env.get('PYTHONPATH','')
        env['LD_LIBRARY_PATH']=':'.join(sorted({str(p.parent) for p in args.prefix.rglob('libgnuradio-isdbt.so*')}))+':'+env.get('LD_LIBRARY_PATH','')
        subprocess.run([sys.executable,__file__,*sys.argv[1:],'--child'],env=env,check=True)
