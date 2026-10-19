import hashlib
import copy
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).parents[1]))
from oni_frame_input_evidence import decode,review,physical_inputs_digest

class FrameInputEvidenceTests(unittest.TestCase):
    def test_production_selection_is_retained_and_unknown_flags_reject(self):
        original=self.journal()
        for flags in range(8):
            body=bytearray(original[:-64]);struct.pack_into('<I',body,12,flags)
            encoded=bytes(body)+hashlib.sha256(body).hexdigest().encode()
            self.assertEqual(decode(encoded)['flags'],flags)
            if flags:self.assertNotEqual(physical_inputs_digest(encoded),physical_inputs_digest(original))
        body=bytearray(original[:-64]);struct.pack_into('<I',body,12,8)
        with self.assertRaises(ValueError):decode(bytes(body)+hashlib.sha256(body).hexdigest().encode())

    def test_native_solid_heat_has_its_own_binary_command_source_and_completed_account(self):
        source=b'opaque completed solid-control source';digest=hashlib.sha256(source).hexdigest()
        body=bytearray(self.journal()[:-64]);struct.pack_into('<QQdd',body,24,0,1,0.,.2);body[104:168]=digest.encode()
        struct.pack_into('<Q',body,192,0);struct.pack_into('<I',body,204,0x4d415348);struct.pack_into('<Q',body,248,0)
        journal=bytes(body)+hashlib.sha256(body).hexdigest().encode()
        row=dict(File='native-frame-inputs-7-1.bin',Bytes=len(journal),Sha256=hashlib.sha256(journal).hexdigest(),SourceSha256=digest,
            Session=7,SourceFrame=0,CompletedFrame=1,SourceTime=0.,Seconds=.2,Flags=0,ReplaySupported=True,
            HeatBatches=1,HeatCommands=1,TransferBatches=1,TransferCommands=1)
        value=dict(AbiVersion=1,PredictsFutureGameCommands=False,ActorReconfigurationReplayImplemented=False,ActorRefreshFramesCompared=0,
            ReplayedFrames=1,Journals=[row],HeatBatches=1,TransferBatches=1)
        report=dict(Status='passed',NativeDomain=dict(Session=7,TimeSeconds=.2,LastTimestepSeconds=.2),
            ColonyCheckpoint=dict(FrameInputReplay=value,ExactContinuationFrames=1,SourceTime=0.,RestoredInitialSha256=digest,ReplayedTime=.2),
            NativeSolidHeat=dict(Required=True,AbiVersion=1,Source='actual-native-UpdateTemperature-cell-pair',Status=dict(
                Size=112,Version=1,Session=7,CompletedFrame=1,Transfers=1,BodyHeatLossJoules=12.)))
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'native-frame-inputs-source-7.bin').write_bytes(source);(root/row['File']).write_bytes(journal)
            result=review(report,root);self.assertEqual(result['native_solid_heat_commands'],1);self.assertEqual(result['native_body_heat_commands'],0)
            report['NativeSolidHeat']['Status']['BodyHeatLossJoules']=11.
            with self.assertRaises(ValueError):review(report,root)

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
    def test_rectangular_journals_retain_both_dimensions_and_reject_cross_owner_footprints(self):
        def rectangle(cell,width,height,action=2,flag=1):
            body=bytearray(self.journal()[:-64]);struct.pack_into('<I',body,256,cell)
            struct.pack_into('<II',body,264,action,flag|(width<<8)|(height<<16))
            return bytes(body)+hashlib.sha256(body).hexdigest().encode()
        data=rectangle(650,3,2);decoded=decode(data)
        self.assertEqual(decoded['batches'][1]['commands'][0]['flags'],1|(3<<8)|(2<<16))
        self.assertNotEqual(physical_inputs_digest(data),physical_inputs_digest(rectangle(650,2,2)))
        self.assertEqual(decode(rectangle(716,0,255))['transfer_commands'],1)
        for data in [rectangle(652,2,1),rectangle(714,1,2),rectangle(650,3,2,action=1),rectangle(650,3,2,flag=0)]:
            with self.assertRaises(ValueError):decode(data)
    def test_rejects_truncation_and_structural_changes_even_with_recomputed_checksums(self):
        original=self.journal()
        for length in range(len(original)):
            with self.assertRaises(ValueError):decode(original[:length])
        for offset in (0,8,12,24,32,56,60,64,96,100,104,168,172,176,180,184,192,200,224,228,232,236,240,248,256,260,264,268,276):
            changed=bytearray(original[:-64]);changed[offset]^=128
            data=bytes(changed)+hashlib.sha256(changed).hexdigest().encode()
            with self.subTest(offset=offset),self.assertRaises((ValueError,UnicodeError)):decode(data)

    def test_saved_native_body_heat_must_match_its_actual_binary_commands(self):
        source=b'opaque completed physical fixture';digest=hashlib.sha256(source).hexdigest()
        body=bytearray(self.journal()[:-64]);struct.pack_into('<QQdd',body,24,0,1,0.,.2);body[104:168]=digest.encode()
        struct.pack_into('<Q',body,192,0);struct.pack_into('<I',body,204,0x4d414348);struct.pack_into('<Q',body,248,0)
        journal=bytes(body)+hashlib.sha256(body).hexdigest().encode();parsed=decode(journal)
        row=dict(File='native-frame-inputs-7-1.bin',Bytes=len(journal),Sha256=hashlib.sha256(journal).hexdigest(),SourceSha256=digest,
            Session=7,SourceFrame=0,CompletedFrame=1,SourceTime=0.,Seconds=.2,Flags=0,ReplaySupported=True,
            HeatBatches=1,HeatCommands=1,TransferBatches=1,TransferCommands=1)
        value=dict(AbiVersion=1,PredictsFutureGameCommands=False,ActorReconfigurationReplayImplemented=False,ActorRefreshFramesCompared=0,
            ReplayedFrames=1,Journals=[row],HeatBatches=1,TransferBatches=1)
        report=dict(Status='passed',NativeDomain=dict(Session=7,TimeSeconds=.2,LastTimestepSeconds=.2),
            ColonyCheckpoint=dict(FrameInputReplay=value,ExactContinuationFrames=1,SourceTime=0.,RestoredInitialSha256=digest,ReplayedTime=.2),
            NativeBodyHeat=dict(AbiVersion=1,Source='native-element-chunk-contact',Status=dict(Size=112,Version=1,Session=7,CompletedFrame=1,
                Transfers=1,BodyHeatLossJoules=12.)))
        self.assertEqual(parsed['batches'][0]['commands'][0]['source'],0x4d414348)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'native-frame-inputs-source-7.bin').write_bytes(source);(root/row['File']).write_bytes(journal)
            self.assertEqual(review(report,root)['native_body_heat_commands'],1)
            for key,value in [('Transfers',2),('BodyHeatLossJoules',11.),('CompletedFrame',0),('Session',8)]:
                changed=copy.deepcopy(report);changed['NativeBodyHeat']['Status'][key]=value
                with self.subTest(key=key),self.assertRaises(ValueError):review(changed,root)

    def test_physical_command_digest_distinguishes_inputs_from_delivery_identifiers(self):
        original=self.journal();expected=physical_inputs_digest(original)
        changed=bytearray(original[:-64]);struct.pack_into('<Q',changed,16,11)
        for offset in (184,240):struct.pack_into('<Q',changed,offset,11)
        struct.pack_into('<i',changed,272,-2147483648)
        signed=bytes(changed)+hashlib.sha256(changed).hexdigest().encode()
        self.assertEqual(physical_inputs_digest(signed),expected)
        for offset in (200,204,208,216,256,260,269,288):
            changed=bytearray(original[:-64]);changed[offset]^=1
            signed=bytes(changed)+hashlib.sha256(changed).hexdigest().encode()
            with self.subTest(offset=offset):self.assertNotEqual(physical_inputs_digest(signed),expected)
        changed=bytearray(original[:-64]);struct.pack_into('<d',changed,280,.015625)
        self.assertNotEqual(physical_inputs_digest(bytes(changed)+hashlib.sha256(changed).hexdigest().encode()),expected)
