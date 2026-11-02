"""Bounded atomic chunks of a coherent full-carrier observation, never TS data."""
import math
import numbers
import base64
import numpy as np


def records(frame, layout=0):
    def integer(name, low, high):
        value = frame[name]
        if isinstance(value,bool) or not isinstance(value,numbers.Integral) or not low <= value <= high:
            raise ValueError('Invalid carrier '+name)
        return int(value)
    if type(layout) is not int or layout not in (0,1) or type(frame.get('schema')) is not int or frame['schema'] != 1:
        raise ValueError('Invalid carrier layout/schema')
    sequence=integer('sequence',1,2**64-1)
    burst=integer('burst',1,2**64-1)
    slot=integer('slot',0,3)
    phase=integer('phase',0,3)
    n=integer('fft_length',256,1024)
    cp=integer('cp_length',1,256)
    arrays=[np.asarray(frame[key],dtype=np.complex64) for key in ('raw','equalized','channel','pilot_reference')]
    count=arrays[0].size
    if count not in (108,216,432) or any(a.shape!=(count,) or not np.isfinite(a).all() or np.max(np.abs(a))>1e9 for a in arrays):
        raise ValueError('Invalid coherent carrier vectors')
    mode={108:1,216:2,432:3}[count]
    expected_n=2**(7+mode)
    if n != expected_n and not (mode==3 and n==576):
        raise ValueError('Invalid carrier FFT geometry')
    if n%cp or n//cp not in (4,8,16,32):
        raise ValueError('Invalid carrier guard geometry')
    stride=12 if layout==1 else 2048//(count//108)
    if burst!=(sequence-1)//stride+1 or slot!=(sequence-1)%stride:
        raise ValueError('Invalid carrier burst identity')
    fs=float(frame['sample_rate'])
    floor=float(frame['equalizer_floor'])
    expected_fs=4000000/7 if n==576 else 64000000/63
    if not math.isfinite(fs) or abs(fs/expected_fs-1)>1e-6 or not math.isfinite(floor) or not 0<=floor<=1:
        raise ValueError('Invalid carrier clock/equalizer')
    if type(frame['phase_valid']) is not bool or type(frame['cp_moved']) is not bool:
        raise ValueError('Invalid carrier phase flags')
    flags=int(frame['phase_valid'])+2*int(frame['cp_moved'])
    prefix=f'\x1eTM_CARR2 {layout} {sequence} {burst} {slot} {mode} {n} {cp} {phase} {flags} {fs:.9g} {floor:.9g} {count} '
    values=np.column_stack([part for a in arrays for part in (a.real,a.imag)])
    result=[]
    for first in range(0,count,64):
        chunk=values[first:first+64]
        payload=base64.b64encode(chunk.astype('<f4',copy=False).tobytes())
        data=(prefix+f'{first} {len(chunk)} ').encode('ascii')+payload+b'\x1f'
        if len(data)>4096:
            raise ValueError('Carrier chunk exceeds atomic pipe budget')
        result.append(data)
    return result
