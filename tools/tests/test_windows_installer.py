"""Exercise installer startup without touching a real ONI installation."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


INSTALLER = Path(__file__).resolve().parents[1] / "install_windows.ps1"
POWERSHELL = os.environ.get("POWERSHELL_EXE") or shutil.which("powershell") or shutil.which("pwsh")


@unittest.skipUnless(POWERSHELL, "PowerShell is needed for installer startup checks")
class WindowsInstallerStartupTests(unittest.TestCase):
    def check_package_path(self, delayed_script_root, explicit_package):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            extracted = root / "extracted package"
            extracted.mkdir()
            script = extracted / "install_windows.ps1"
            shutil.copyfile(INSTALLER, script)
            working = root / "another working directory"
            working.mkdir()
            expected = root / "custom package" if explicit_package else extracted / "MixedAtmospheres"
            env = dict(os.environ, INSTALLER_TEST_SCRIPT=str(script),
                       INSTALLER_TEST_USER_ROOT=str(root / "ONI user data"),
                       INSTALLER_TEST_PACKAGE=str(expected) if explicit_package else "",
                       INSTALLER_TEST_DELAYED_ROOT=str(int(delayed_script_root)))
            # Stop at the first filesystem lookup. Parameter binding, default
            # resolution, and the running-game guard still execute unmodified.
            command = r"""
$ErrorActionPreference = 'Stop'
function Get-Process { param($Name, $ErrorAction) }
function Resolve-Path { param($LiteralPath); throw ('RESOLVED:' + $LiteralPath) }
$arguments = @{ OniUserRoot = $env:INSTALLER_TEST_USER_ROOT }
if ($env:INSTALLER_TEST_PACKAGE) { $arguments.Package = $env:INSTALLER_TEST_PACKAGE }
try {
    if ($env:INSTALLER_TEST_DELAYED_ROOT -eq '1') {
        # Reproduce the reported binding context: the script location becomes
        # available only after parameters have been bound.
        $tokens = $null; $errors = $null
        $ast = [System.Management.Automation.Language.Parser]::ParseFile(
            $env:INSTALLER_TEST_SCRIPT, [ref]$tokens, [ref]$errors)
        if ($errors.Count) { throw 'Installer parse failed.' }
        $directory = (Split-Path -Parent $env:INSTALLER_TEST_SCRIPT).Replace("'", "''")
        $body = ($ast.EndBlock.Statements | ForEach-Object { $_.Extent.Text }) -join "`n"
        $program = $ast.ParamBlock.Extent.Text + "`n" +
            '$PSScriptRoot = ''' + $directory + "'`n" + $body
        & ([scriptblock]::Create($program)) @arguments
    } else {
        & $env:INSTALLER_TEST_SCRIPT @arguments
    }
} catch { Write-Output $_.Exception.Message; exit 1 }
"""
            result = subprocess.run(
                [POWERSHELL, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", command],
                cwd=working, env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("RESOLVED:" + str(expected), result.stdout + result.stderr)
            self.assertFalse((root / "ONI user data").exists())

    def test_defaults_are_relative_to_extracted_script_from_another_directory(self):
        self.check_package_path(False, False)

    def test_explicit_package_with_spaces_is_preserved(self):
        self.check_package_path(False, True)

    def test_default_package_survives_late_script_root_initialization(self):
        self.check_package_path(True, False)

    def test_explicit_package_survives_late_script_root_initialization(self):
        self.check_package_path(True, True)


if __name__ == "__main__":
    unittest.main()
