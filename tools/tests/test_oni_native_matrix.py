import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SPEC=importlib.util.spec_from_file_location("oni_native_matrix",Path(__file__).parents[1]/"oni_native_matrix.py")
matrix=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(matrix)

class NativeMatrixTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory();self.addCleanup(self.folder.cleanup)
        self.root=Path(self.folder.name);self.runner=self.root/"frozen";self.package=self.root/"package";self.output=self.root/"evidence"
        (self.runner/"tools").mkdir(parents=True);self.package.mkdir()
        for file in ("oni_headless.py","oni_native_domain_evidence.py","oni_proton_headless.py","oni_native_matrix.py"):
            (self.runner/"tools"/file).write_text("frozen original")
        (self.package/"probe.dll").write_bytes(b"original native package")
        self.commands=[]

    def windows_game(self,command,log,timeout):
        self.game(command,log,timeout)
        run=Path(command[command.index("--output")+1]);path=run/"result.json";data=json.loads(path.read_text())
        data.update(target_platform="WindowsPlayer",host_platform="linux",actual_windows_os=False,real_unity_player=True)
        checkpoint=data["native_domain"]["ColonyCheckpoint"]
        if checkpoint:
            data.update(host_saved_file=checkpoint["SavedFile"],host_saved_file_sha256=checkpoint["SavedFileSha256"])
            checkpoint["SavedFile"]=r"C:\users\steamuser\Documents\colony.sav"
        path.write_text(json.dumps(data));return 0

    def test_proton_matrix_loads_only_the_authenticated_private_host_save_and_records_linux_host(self):
        with mock.patch.object(matrix,"invoke",side_effect=self.windows_game):
            report=matrix.run_matrix(self.runner,self.package,self.output,["water"],proton=self.root/"proton",game_dir=self.root/"windows-game")
        self.assertTrue(report["passed"]);self.assertTrue(report["windows_unity_verified"])
        self.assertFalse(report["actual_windows_os"]);self.assertFalse(report["first_playable_colony_passed"])
        self.assertEqual(report["target_platform"],"WindowsPlayer")
        self.assertIn("oni_proton_headless.py",Path(self.commands[0][1]).name)
        self.assertNotIn("run",self.commands[0]);self.assertIn("--proton",self.commands[0])
        self.assertEqual(Path(self.commands[1][self.commands[1].index("--load-save")+1]),self.output/"water-save/colony.sav")
        self.assertIn("tools/oni_proton_headless.py",report["inputs"]["runner_files"])

    def test_proton_cannot_promote_missing_host_metadata_or_windows_os_claim_to_success(self):
        for key,value in (("actual_windows_os",True),("real_unity_player",False),("target_platform","LinuxPlayer"),("host_platform",None)):
            output=self.root/key
            def wrong(*args):
                self.windows_game(*args);path=output/"water-save/result.json";data=json.loads(path.read_text());data[key]=value
                path.write_text(json.dumps(data));return 0
            with mock.patch.object(matrix,"invoke",side_effect=wrong),self.assertRaisesRegex(ValueError,"misidentifies"):
                matrix.run_matrix(self.runner,self.package,output,["water"],proton=self.root/"proton")
            self.assertFalse(json.loads((output/"matrix.json").read_text())["passed"])

    def test_proton_exported_save_hash_and_frozen_runner_must_both_match(self):
        for change in ("hash","runner"):
            output=self.root/change
            def wrong(*args):
                self.windows_game(*args)
                if change=="runner":(self.runner/"tools/oni_proton_headless.py").write_text("changed during game")
                else:
                    path=output/"water-save/result.json";data=json.loads(path.read_text());data["host_saved_file_sha256"]="0"*64
                    path.write_text(json.dumps(data))
                return 0
            with mock.patch.object(matrix,"invoke",side_effect=wrong),self.assertRaisesRegex(ValueError,"receipt|Frozen"):
                matrix.run_matrix(self.runner,self.package,output,["water"],proton=self.root/"proton")
            self.assertFalse(json.loads((output/"matrix.json").read_text())["passed"])

    def game(self,command,log,timeout):
        self.commands.append(command);run=Path(command[command.index("--output")+1]);run.mkdir()
        mode=command[command.index("--mode")+1]
        checkpoint={}
        if mode!="native-load-domain":
            save=run/"colony.sav";save.write_bytes(b"complete source")
            checkpoint={"Saved":True,"SavedFile":str(save),"SavedFileSha256":matrix.digest(save)}
        (run/"result.json").write_text(json.dumps({"status":"passed-"+mode,"native_domain":{"ColonyCheckpoint":checkpoint}}))
        return 0

    def test_each_scene_uses_a_fresh_source_and_reload_with_exact_source_receipt(self):
        with mock.patch.object(matrix,"invoke",side_effect=self.game):
            report=matrix.run_matrix(self.runner,self.package,self.output,["water","heat"])
        self.assertTrue(report["passed"]);self.assertEqual(len(self.commands),4)
        self.assertIn("--save-colony",self.commands[0]);self.assertIn("--resave-before-resume",self.commands[1])
        self.assertNotEqual(self.commands[1][self.commands[1].index("--load-save")+1],self.commands[3][self.commands[3].index("--load-save")+1])
        self.assertFalse(report["first_playable_colony_passed"]);self.assertFalse(report["whole_colony_fluid_authority"])

    def test_nonzero_exit_with_passing_text_stops_without_loading_or_starting_the_next_scene(self):
        def rejected(*args):self.game(*args);return 2
        with mock.patch.object(matrix,"invoke",side_effect=rejected),self.assertRaises(ValueError):
            matrix.run_matrix(self.runner,self.package,self.output,["water","heat"])
        self.assertEqual(len(self.commands),1);self.assertFalse(json.loads((self.output/"matrix.json").read_text())["passed"])

    def test_zero_exit_with_failed_report_still_rejects(self):
        def rejected(*args):
            self.game(*args);path=self.output/"water-save/result.json";data=json.loads(path.read_text());data["status"]="failed-native-domain";path.write_text(json.dumps(data));return 0
        with mock.patch.object(matrix,"invoke",side_effect=rejected),self.assertRaises(ValueError):
            matrix.run_matrix(self.runner,self.package,self.output,["water"])
        self.assertEqual(len(self.commands),1)

    def test_changed_package_is_detected_before_the_reload(self):
        def changed(*args):self.game(*args);(self.package/"probe.dll").write_bytes(b"different build");return 0
        with mock.patch.object(matrix,"invoke",side_effect=changed),self.assertRaisesRegex(ValueError,"Frozen"):
            matrix.run_matrix(self.runner,self.package,self.output,["water"])
        self.assertEqual(len(self.commands),1)

    def test_no_result_retains_a_failed_case_and_cannot_start_a_reload(self):
        with mock.patch.object(matrix,"invoke",return_value=2) as invoke,self.assertRaisesRegex(ValueError,"no result"):
            matrix.run_matrix(self.runner,self.package,self.output,["water"])
        invoke.assert_called_once();report=json.loads((self.output/"matrix.json").read_text())
        self.assertFalse(report["passed"]);self.assertEqual(report["cases"][0]["status"],"missing-result")

    def test_source_outside_the_disposable_run_is_never_loaded(self):
        outside=self.root/"existing-user-save.sav";outside.write_bytes(b"existing user save")
        def changed(command,*args):
            self.game(command,*args);path=self.output/"water-save/result.json";data=json.loads(path.read_text())
            data["native_domain"]["ColonyCheckpoint"].update(SavedFile=str(outside),SavedFileSha256=matrix.digest(outside))
            path.write_text(json.dumps(data));return 0
        with mock.patch.object(matrix,"invoke",side_effect=changed),self.assertRaisesRegex(ValueError,"outside"):
            matrix.run_matrix(self.runner,self.package,self.output,["water"])
        self.assertEqual(len(self.commands),1);self.assertEqual(outside.read_bytes(),b"existing user save")

    def test_mutated_source_save_does_not_produce_a_successful_matrix(self):
        def changed(command,*args):
            self.game(command,*args)
            if "--load-save" in command:Path(command[command.index("--load-save")+1]).write_bytes(b"mutated source")
            return 0
        with mock.patch.object(matrix,"invoke",side_effect=changed),self.assertRaisesRegex(ValueError,"Source save changed"):
            matrix.run_matrix(self.runner,self.package,self.output,["water"])
        self.assertFalse(json.loads((self.output/"matrix.json").read_text())["passed"])

    def test_existing_evidence_and_unsupported_short_actor_runs_are_rejected_before_launch(self):
        self.output.mkdir();sentinel=self.output/"retained";sentinel.write_bytes(b"first failure")
        with mock.patch.object(matrix,"invoke") as invoke,self.assertRaises(FileExistsError):
            matrix.run_matrix(self.runner,self.package,self.output,["water"])
        invoke.assert_not_called();self.assertEqual(sentinel.read_bytes(),b"first failure")
        for actor in ("actor","water-actor"):
            with mock.patch.object(matrix,"invoke") as invoke,self.assertRaises(ValueError):
                matrix.run_matrix(self.runner,self.package,self.root/"short",[actor],frames=30)
            invoke.assert_not_called()

if __name__=="__main__":unittest.main()
