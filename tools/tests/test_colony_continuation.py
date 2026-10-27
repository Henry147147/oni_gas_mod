import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import colony_continuation as continuation


def pair():
    record={"Frames":3,"RestoredFrame":50,"Submitted":3,"Frame":53,"Complete":True,
            "RuntimeImages":True,"NativeSchedule":True,"SaveSha256":"a"*64,
            "BaselineOwnerSha256":"b"*64,"BaselinePipesSha256":"c"*64,
            "Hashes":[str(i)*64 for i in range(1,4)],"PipeHashes":["d"*64]*3,"ParticleHashes":["e"*64]*3,
            "SecondsBits":[0x3e4ccccd]*3,"Origin":"uninterrupted"}
    source={"status":"passed-colony-owner","mode":"colony-owner","exit_code":0,"platform":"native Linux","host_platform":"linux",
            "game_hashes":{"assembly":"f"*64},"package_manifest":{"files":{"mod.dll":"0"*64}},
            "colony_owner":{"State":1,"Faulted":False,"Continuation":record,
                "PipeSave":{"Active":True,"AuditClosed":True,"NativeScheduleControl":True,"NativeBindingsPending":False}}}
    loaded=copy.deepcopy(source)
    loaded.update(status="passed-colony-owner-load",mode="colony-owner-load")
    loaded["colony_owner"]["Continuation"]["Origin"]="restored"
    loaded["colony_owner"]["PipeSave"]["RestoredSha256"]="c"*64
    loaded["colony_owner"]["RestoredOwner"]={"Reencoded":True,"ImageSha256":"b"*64}
    loaded["load_save_fixture"]={"sha256":"a"*64}
    return source,loaded


class ContinuationTests(unittest.TestCase):
    def test_equal_active_prefix_remains_bounded_evidence(self):
        result=continuation.compare(*pair())
        self.assertEqual(result["status"],"matched")
        self.assertEqual(result["frames"],3)
        self.assertFalse(result["identical_inputs_verified"])
        self.assertFalse(result["full_colony_continuation_verified"])

    def test_first_difference_names_frame_and_physical_sections(self):
        for key in ("Hashes","PipeHashes","ParticleHashes","SecondsBits"):
            with self.subTest(key=key):
                source,loaded=pair()
                loaded["colony_owner"]["Continuation"][key][1]="9"*64 if key!="SecondsBits" else 0x3e000000
                result=continuation.compare(source,loaded)
                self.assertEqual(result["status"],"mismatch")
                self.assertEqual(result["first_mismatch"],{"frame":52,"different":[key]})

    def test_restored_reencoding_alone_cannot_establish_continuation(self):
        source,loaded=pair()
        for key,value in (("RuntimeImages",False),("NativeSchedule",False),("Complete",False),("PipeHashes",[]),("Hashes",[])):
            with self.subTest(key=key):
                changed=copy.deepcopy(loaded);changed["colony_owner"]["Continuation"][key]=value
                with self.assertRaises(ValueError):continuation.compare(source,changed)

    def test_same_save_and_baselines_are_required(self):
        for key in ("SaveSha256","BaselineOwnerSha256","BaselinePipesSha256","RestoredFrame","Frames"):
            source,loaded=pair();record=loaded["colony_owner"]["Continuation"]
            record[key]="9"*64 if "Sha256" in key else record[key]+1
            with self.subTest(key=key),self.assertRaises(ValueError):continuation.compare(source,loaded)
        source,loaded=pair();loaded["load_save_fixture"]["sha256"]="9"*64
        with self.assertRaises(ValueError):continuation.compare(source,loaded)

    def test_healthy_active_pipe_audits_and_actual_restored_sections(self):
        for section,key,value in (("PipeSave","Active",False),("PipeSave","AuditClosed",False),
                ("PipeSave","NativeBindingsPending",True),("PipeSave","NativeBindingsPending",None),
                ("PipeSave","NativeScheduleControl",False),("PipeSave","RestoredSha256","9"*64),
                ("RestoredOwner","Reencoded",False),("RestoredOwner","ImageSha256","9"*64)):
            source,loaded=pair();loaded["colony_owner"][section][key]=value
            with self.subTest(section=section,key=key),self.assertRaises(ValueError):continuation.compare(source,loaded)
        for field,value in (("State",2),("State",True),("Faulted",True)):
            source,loaded=pair();loaded["colony_owner"][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):continuation.compare(source,loaded)

    def test_host_player_and_binary_identities_must_match(self):
        for field,value in (("host_platform","windows"),("platform","Proton"),("game_hashes",{}),("package_manifest",{})):
            source,loaded=pair();loaded[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):continuation.compare(source,loaded)
        for field in ("host_platform","platform","game_hashes","package_manifest"):
            source,loaded=pair();del source[field];del loaded[field]
            with self.subTest(missing=field),self.assertRaises(ValueError):continuation.compare(source,loaded)

    def test_short_duplicate_or_invalid_prefix_records_refuse(self):
        cases=[("Submitted",2),("Submitted",True),("Frame",52),("Frame",True),("RestoredFrame",True),
               ("Frames",3.0),("Origin","uninterrupted"),("Hashes",["1"*64]*2),
               ("SecondsBits",[0,0x3e4ccccd,0x3e4ccccd]),("SecondsBits",[0x7f800000]*3),
               ("SecondsBits",[True]*3),("SaveSha256","not-a-hash")]
        for key,value in cases:
            source,loaded=pair();loaded["colony_owner"]["Continuation"][key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):continuation.compare(source,loaded)
        for field,value in (("status","failed"),("status","passed"),("mode","colony-owner"),
                            ("exit_code",False),("exit_code",2)):
            source,loaded=pair();loaded[field]=value
            with self.subTest(field=field,value=value),self.assertRaises(ValueError):continuation.compare(source,loaded)



def journal(start,frames):
    lines=["session 1 restored_frames %d"%start]
    for frame,items in frames:
        lines.append("frame %d flush 0 seconds 0x1.99999ap-3 ticket %d transfers %d heat 0 terrain 0 freezes 0 germs 0 jobs 0"%(frame,1000+frame,len(items)))
        lines.extend(items)
    return "\n".join(lines)+"\n"


class InputJournalTests(unittest.TestCase):
    breath="T 4 3 2 1 0 0x1.2p-7 0x1.2cp+8 0x0p+0 job 0 cb %d"
    def test_identical_inputs_ignore_session_tickets_and_callback_values(self):
        source=journal(0,[(f,[self.breath%(70+f)]) for f in range(49,54)])
        loaded=journal(50,[(f,[self.breath%(9+f)]) for f in range(51,54)])
        self.assertIsNone(continuation.compare_inputs(source,loaded,50,3))
        result=continuation.compare(*pair(),source,loaded)
        self.assertEqual(result["status"],"matched");self.assertTrue(result["identical_inputs_verified"])

    def test_answered_and_unanswered_requests_differ(self):
        source=journal(50,[(51,[self.breath%7]),(52,[self.breath%7]),(53,[])])
        loaded=journal(50,[(51,[self.breath%7]),(52,[self.breath%-1]),(53,[])])
        divergence=continuation.compare_inputs(source,loaded,50,3)
        self.assertEqual(divergence["frame"],52)
        self.assertEqual(divergence["only_source"],["T 4 3 2 1 0 0x1.2p-7 0x1.2cp+8 0x0p+0 job 0 cb answered"])

    def test_state_difference_before_input_difference_is_a_continuation_failure(self):
        source,loaded=pair();loaded["colony_owner"]["Continuation"]["Hashes"][0]="9"*64
        same=journal(50,[(f,[self.breath%1]) for f in range(51,54)])
        result=continuation.compare(source,loaded,same,same)
        self.assertEqual(result["status"],"continuation-mismatch");self.assertEqual(result["attributed_to"],"continuation")
        later=journal(50,[(51,[self.breath%1]),(52,[]),(53,[self.breath%1])])
        result=continuation.compare(source,loaded,later,same)
        self.assertEqual(result["status"],"continuation-mismatch")
        first=journal(50,[(51,[self.breath%1]),(52,[]),(53,[])])
        result=continuation.compare(source,loaded,first,journal(50,[(51,[]),(52,[]),(53,[])]))
        self.assertEqual((result["status"],result["attributed_to"]),("mismatch","inputs"))
        source,loaded=pair();loaded["colony_owner"]["Continuation"]["Hashes"][1]="9"*64
        result=continuation.compare(source,loaded,journal(50,[(51,[]),(52,[self.breath%1]),(53,[])]),same)
        self.assertEqual((result["status"],result["attributed_to"]),("mismatch","inputs"))

    def test_journals_must_cover_the_restored_branch(self):
        good=journal(50,[(f,[]) for f in range(51,54)])
        for bad in (journal(49,[(f,[]) for f in range(50,54)]),journal(50,[(51,[]),(52,[])]),"frame 1\n","session 1 restored_frames 50\nT 1\n"):
            with self.subTest(bad=bad[:30]),self.assertRaises(ValueError):continuation.compare_inputs(good,bad,50,3)
        with self.assertRaises(ValueError):continuation.compare(*pair(),good,None)

if __name__=="__main__":unittest.main()
