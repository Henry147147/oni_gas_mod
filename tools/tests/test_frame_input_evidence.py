import hashlib
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).parents[1]))
from oni_frame_input_evidence import decode

class FrameInputEvidenceTests(unittest.TestCase):
    def journal(self):
        body=b'MAFRIN01'+struct.pack('<IIQQQdd',1,0,7,3,4,.6,.2)+struct.pack('<10I',40,2,1,64,10,10,3,2,0,0)
        body+=struct.pack('<II',2,0)+b'a'*64
        body+=struct.pack('<II',1,1)+struct.pack('<IIQQIIdd',48,1,7,3,650,9,12.,1000.)
        body+=struct.pack('<II',2,1)+struct.pack('<IIQQIIIIiIdd',64,2,7,3,650,1,2,769,31,0,.03125,0.)
        return body+hashlib.sha256(body).hexdigest().encode()
    def test_decodes_ordered_heat_and_actual_radius_callback_operands(self):
        decoded=decode(self.journal())
        self.assertEqual((decoded['session'],decoded['source_frame'],decoded['completed_frame']),(7,3,4))
        self.assertEqual((decoded['heat_commands'],decoded['transfer_commands']),(1,1))
        self.assertEqual([b['kind'] for b in decoded['batches']],[1,2])
        self.assertEqual(decoded['batches'][1]['commands'][0]['flags'],769)
    def test_rejects_truncation_and_structural_changes_even_with_recomputed_checksums(self):
        original=self.journal()
        for length in range(len(original)):
            with self.assertRaises(ValueError):decode(original[:length])
        for offset in (0,8,12,24,32,56,60,64,96,100,104,168,172,176,180,184,192,200,224,228,232,236,240,248,256,260,264,268,276):
            changed=bytearray(original[:-64]);changed[offset]^=128
            data=bytes(changed)+hashlib.sha256(changed).hexdigest().encode()
            with self.subTest(offset=offset),self.assertRaises((ValueError,UnicodeError)):decode(data)
