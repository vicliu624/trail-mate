import copy
import base64
import importlib.util
from pathlib import Path
import unittest
import numpy as np

path=Path(__file__).resolve().parents[2]/'apps/linux_lvgl/tools/oneseg_carrier_protocol.py'
spec=importlib.util.spec_from_file_location('carrier_protocol',path)
protocol=importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)


class CarrierProtocol(unittest.TestCase):
    def fixture(self):
        values=(np.arange(432)/1000+1j*np.arange(432)[::-1]/2000).astype(np.complex64)
        return dict(schema=1,sequence=513,burst=2,slot=0,phase=3,phase_valid=True,cp_moved=False,
                    fft_length=576,cp_length=36,sample_rate=4000000/7,equalizer_floor=.04,
                    raw=values,equalized=values*.8,channel=values+1,pilot_reference=np.ones(432,np.complex64))

    def test_complete_atomic_chunks_and_float32_roundtrip(self):
        frame=self.fixture()
        chunks=protocol.records(frame)
        self.assertEqual(len(chunks),7)
        rows=[]
        for index,chunk in enumerate(chunks):
            self.assertLessEqual(len(chunk),4096)
            self.assertTrue(chunk.startswith(b'\x1eTM_CARR2 ') and chunk.endswith(b'\x1f'))
            fields=chunk[1:-1].decode().split()
            self.assertEqual([int(v) for v in fields[13:15]],[index*64,min(64,432-index*64)])
            rows.extend(np.frombuffer(base64.b64decode(fields[15],validate=True),dtype='<f4').reshape(-1,8))
        rows=np.asarray(rows,dtype=np.float32)
        np.testing.assert_array_equal(rows[:,0]+1j*rows[:,1],frame['raw'])
        np.testing.assert_array_equal(rows[:,2]+1j*rows[:,3],frame['equalized'])

    def test_rejects_incoherent_metadata_and_vectors(self):
        for key,value in (('sequence',True),('slot',1),('burst',3),('fft_length',512),('cp_length',35),
                          ('sample_rate',450000),('phase_valid',1),('equalizer_floor',float('nan')),
                          ('channel',np.ones(431)),('raw',np.full(432,np.nan))):
            changed=copy.deepcopy(self.fixture())
            changed[key]=value
            with self.assertRaises(ValueError,msg=key):
                protocol.records(changed)

    def test_generic_source_uses_its_own_burst_cadence(self):
        frame=self.fixture()
        frame.update(sequence=13,burst=2,slot=0)
        self.assertEqual(len(protocol.records(frame,layout=1)),7)
        with self.assertRaises(ValueError):
            protocol.records(frame,layout=0)


if __name__=='__main__':
    unittest.main()
