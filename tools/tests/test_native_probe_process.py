"""Argument and evidence boundaries for native and Wine execution."""
import argparse
import contextlib
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_probe_process import ProbeRuntime, add_runtime_arguments, windows_host_path
from oni_native_domain_evidence import native_domain_passed


class NativeRuntimeTests(unittest.TestCase):
    def test_native_arguments_preserve_paths_and_report_execution_environment(self):
        parser = argparse.ArgumentParser(); add_runtime_arguments(parser)
        runtime = ProbeRuntime(parser.parse_args([]), parser)
        probe = Path('probe folder/NativeShadowProbe.dll')
        self.assertEqual(runtime.command(probe, Path('game folder/SimDLL.dll'), 'baseline'),
                         ['dotnet', str(probe.resolve()), str(Path('game folder/SimDLL.dll').resolve()), 'baseline'])
        self.assertFalse(runtime.metadata()['unityGameTest'])

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Wine runner is Linux-only')
    def test_wine_maps_each_file_argument_without_shell_quoting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); wine = root/'wine exe'; dotnet = root/'runtime folder/dotnet.exe'
            wine.touch(); dotnet.parent.mkdir(); dotnet.touch()
            (root/'dosdevices').mkdir(); (root/'dosdevices/z:').symlink_to('/')
            parser = argparse.ArgumentParser(); add_runtime_arguments(parser)
            runtime = ProbeRuntime(parser.parse_args(['--wine', str(wine), '--wine-prefix', str(root), '--windows-dotnet', str(dotnet)]), parser)
            probe = root/'probe folder/NativeShadowProbe.dll'; report = root/'report folder/result.json'
            command = runtime.command(probe, 'sealed-gas', report)
            self.assertEqual(command, [str(wine), windows_host_path(dotnet), windows_host_path(probe), 'sealed-gas', windows_host_path(report)])
            self.assertEqual(runtime.environment()['WINEPREFIX'], str(root))
            self.assertEqual(runtime.metadata()['executionEnvironment'], 'wine')
            self.assertFalse(runtime.metadata()['actualWindowsOS'])

    def test_partial_wine_configuration_rejects_before_launch(self):
        parser = argparse.ArgumentParser(); add_runtime_arguments(parser)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            ProbeRuntime(parser.parse_args(['--wine', '/missing/wine']), parser)

    def test_isolated_windows_or_wine_success_cannot_pass_unity_domain_acceptance(self):
        report = {'kind': 'isolated-native-interface-experiment', 'passed': True,
                  'platform': 'Microsoft Windows 10.0.19043', 'sealedNativeDomainPassed': True}
        self.assertFalse(native_domain_passed(report, {}, 30, expected_platform='WindowsPlayer'))
