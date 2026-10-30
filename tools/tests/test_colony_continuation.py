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
        lines.append("frame %d flush 0 seconds 0x1.99999ap-3 ticket %d transfers %d heat 0 terrain 0 freezes 0 germs 0 jobs 0"%(frame,1000+frame,sum(item.startswith("T ") for item in items)))
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

    def test_activity_sequence_mode_census_touches_and_order_are_compared(self):
        items = ["A 51 -1", "C 4", "C 8", "D 9"]
        source = journal(0, [(frame, items) for frame in range(51, 54)])
        loaded = journal(50, [(frame, items) for frame in range(51, 54)])
        self.assertIsNone(continuation.compare_inputs(source, loaded, 50, 3))
        self.assertTrue(continuation.compare(*pair(), source, loaded)["identical_inputs_verified"])
        mutations = [
            ["A 52 -1", "C 4", "C 8", "D 9"],
            ["A 51 0", "C 4", "C 8", "D 9"],
            ["A 51 -1", "C 5", "C 8", "D 9"],
            ["A 51 -1", "C 8", "C 4", "D 9"],
            ["A 51 -1", "C 4", "C 8", "D 10"],
            ["A 51 -1", "C 4", "C 8"],
        ]
        for changed in mutations:
            with self.subTest(changed=changed):
                other = journal(50, [(51, changed), (52, items), (53, items)])
                difference = continuation.compare_inputs(source, other, 50, 3)
                self.assertEqual(difference["frame"], 51)
        reordered = journal(50, [(51, mutations[3]), (52, items), (53, items)])
        self.assertTrue(continuation.compare_inputs(source, reordered, 50, 3)["same_multiset"])

    def test_activity_lifecycle_zero_sequence_and_integer_limits_are_preserved(self):
        text = ("session 1 restored_frames 0\n"
                "frame 0 flush 1 seconds 0x0p+0 ticket 0 transfers 0 heat 0 terrain 0 freezes 0 germs 0 jobs 0\n"
                "A 0 -1\nC 0\nD 4294967295\n"
                "frame 1 flush 0 seconds 0x1.99999ap-3 ticket 1 transfers 0 heat 0 terrain 0 freezes 0 germs 0 jobs 0\n"
                "A 18446744073709551615 1\nC 7\n")
        start, frames = continuation.journal(text)
        self.assertEqual(start, 0)
        self.assertEqual(frames[0][1:], ["A 0 -1", "C 0", "D 4294967295"])
        self.assertEqual(frames[1][1:], ["A 18446744073709551615 1", "C 7"])

    def test_activity_records_refuse_malformed_and_unscoped_cells(self):
        malformed = [
            ["A 1"], ["A 1 -1 extra"], ["A -1 -1"], ["A +1 -1"],
            ["A 1.0 -1"], ["A 18446744073709551616 -1"], ["A 1 2"],
            ["A 1 -2"], ["A 1 -1", "A 2 -1"], ["C 1"], ["D 1"],
            ["A 1 -1", "C -1"], ["A 1 -1", "C 4294967296"],
            ["A 1 -1", "D 1 extra"], ["A 1 -1", "D NaN"],
            ["A 1 -1", "Q 1"],
        ]
        for items in malformed:
            with self.subTest(items=items), self.assertRaises(ValueError):
                continuation.journal(journal(0, [(1, items)]))

    def test_checkpoint_observations_do_not_become_inputs_or_hide_state_mismatch(self):
        activity = ["A 51 -1", "C 4", "D 9"]
        source_inputs = journal(0, [(51, activity + ["checkpoint 51 " + "a" * 64]), (52, []), (53, [])])
        loaded_inputs = journal(50, [(51, activity + ["checkpoint 51 " + "b" * 64, "checkpoint 51 " + "b" * 64]), (52, []), (53, [])])
        self.assertIsNone(continuation.compare_inputs(source_inputs, loaded_inputs, 50, 3))
        source, loaded = pair()
        loaded["colony_owner"]["Continuation"]["Hashes"][0] = "9" * 64
        result = continuation.compare(source, loaded, source_inputs, loaded_inputs)
        self.assertEqual(result["status"], "continuation-mismatch")
        self.assertEqual(result["attributed_to"], "continuation")
        self.assertTrue(result["identical_inputs_verified"])

    def test_checkpoint_observations_reject_malformed_fields(self):
        for record in ["checkpoint 1", "checkpoint -1 " + "a" * 64,
                       "checkpoint 18446744073709551616 " + "a" * 64,
                       "checkpoint 1 " + "a" * 63, "checkpoint 1 " + "q" * 64,
                       "checkpoint 1 " + "a" * 64 + " extra"]:
            with self.subTest(record=record), self.assertRaises(ValueError):
                continuation.journal(journal(0, [(1, [record])]))
        with self.assertRaises(ValueError):
            continuation.journal("session 1 restored_frames 0\ncheckpoint 0 " + "a" * 64 + "\n")

    def test_journals_must_cover_the_restored_branch(self):
        good=journal(50,[(f,[]) for f in range(51,54)])
        for bad in (journal(49,[(f,[]) for f in range(50,54)]),journal(50,[(51,[]),(52,[])]),"frame 1\n","session 1 restored_frames 50\nT 1\n"):
            with self.subTest(bad=bad[:30]),self.assertRaises(ValueError):continuation.compare_inputs(good,bad,50,3)
        with self.assertRaises(ValueError):continuation.compare(*pair(),good,None)

if __name__=="__main__":unittest.main()
