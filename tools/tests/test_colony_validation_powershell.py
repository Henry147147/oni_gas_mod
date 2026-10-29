"""Run the Windows runner's actual preflight/functions without launching a game.

Set ONI_TEST_POWERSHELL to an existing portable pwsh/powershell executable when
it is not on PATH. PowerShell 7 checks parsing/runtime logic; it does not stand
in for an actual Windows PowerShell 5.1 or Windows-player acceptance run.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNNER = Path(os.environ.get("ONI_TEST_COLONY_RUNNER", ROOT / "tools/run_colony_validation.ps1"))
POWERSHELL = os.environ.get("ONI_TEST_POWERSHELL") or shutil.which("pwsh") or shutil.which("powershell")


class FrameworkCompatibilityControls(unittest.TestCase):
    def test_runner_does_not_require_core_only_platform_or_path_apis(self):
        source = RUNNER.read_text()
        self.assertNotIn("[IO.Path]::GetRelativePath", source)
        self.assertNotIn("[Runtime.InteropServices.RuntimeInformation]", source)


@unittest.skipUnless(POWERSHELL, "set ONI_TEST_POWERSHELL or install PowerShell to execute the actual runner AST")
class PowerShellRunnerControls(unittest.TestCase):
    def run_control(self, body):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            script = folder / "control.ps1"
            script.write_text("""param([string]$RunnerPath, [string]$FixtureRoot)
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($RunnerPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
""" + body)
            result = subprocess.run([POWERSHELL, "-NoLogo", "-NoProfile", "-NonInteractive", "-File", str(script),
                                     "-RunnerPath", str(RUNNER), "-FixtureRoot", str(folder)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS", result.stdout)

    def test_actual_preflight_resave_reload_truth_matrix(self):
        self.run_control(r"""
# Stop at the first workspace/package/process operation. Run the original
# parameter block and every preceding guard; only the Windows-host guard is
# tested separately below so the mode matrix also runs on Linux PowerShell.
$prefix = @()
$found = $false
foreach ($statement in $ast.EndBlock.Statements) {
    if ($statement -is [Management.Automation.Language.AssignmentStatementAst] -and
        $statement.Left -is [Management.Automation.Language.VariableExpressionAst] -and
        $statement.Left.VariablePath.UserPath -eq 'oniWorkspace') { $found = $true; break }
    $prefix += $statement
}
if (!$found) { throw 'Runner preflight boundary moved; review this control.' }
$hostGuard = @($prefix | Where-Object { $_.Extent.Text.Contains('This runner records Windows OS evidence;') })
if ($hostGuard.Count -ne 1) { throw 'Expected exactly one Windows-host guard.' }
$logic = [scriptblock]::Create($ast.ParamBlock.Extent.Text + "`n" +
    (($prefix | Where-Object { $_ -ne $hostGuard[0] } | ForEach-Object { $_.Extent.Text }) -join "`n") + "`n'PREFLIGHT_OK'")
if ($logic.Ast.Find({param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -in @('Start-Process','Copy-Item','Move-Item','Remove-Item','Get-Process')}, $true)) {
    throw 'Preflight control reached process or mutation operations.'
}
$save = Join-Path $FixtureRoot 'unchanged source.sav'
[IO.File]::WriteAllText($save, 'disposable runner fixture')
$before = (Get-FileHash -LiteralPath $save).Hash
$checks = 0
foreach ($owner in @($false,$true)) {
foreach ($domain in @('', 'load', 'gas')) {
foreach ($load in @($false,$true)) {
foreach ($resave in @($false,$true)) {
    $arguments = @{Package='unused'; DlcMask=0; Frames=80}
    if ($owner) { $arguments.ColonyOwner = $true }
    if ($domain) { $arguments.NativeDomain = $domain }
    if ($load) { $arguments.LoadSave = $save }
    if ($resave) { $arguments.ResaveBeforeResume = $true }
    $expected = (!$owner -or !$domain) -and ($owner -or ($load -eq ($domain -eq 'load'))) -and
        (!$resave -or ($load -and ($owner -or $domain -eq 'load')))
    $actual = $false
    try { $actual = @(& $logic @arguments) -contains 'PREFLIGHT_OK' } catch { }
    if ($actual -ne $expected) { throw "Mode mismatch owner=$owner domain=$domain load=$load resave=$resave expected=$expected actual=$actual" }
    $checks++
}}}}
# Native load and owner load retain their existing in-process reload contract.
foreach ($case in @(
    @{ColonyOwner=$true;LoadSave=$save;ReloadInProcess=$true;Expected=$true},
    @{ColonyOwner=$true;ReloadInProcess=$true;Expected=$false},
    @{NativeDomain='load';LoadSave=$save;ResaveBeforeResume=$true;ReloadInProcess=$true;Expected=$true},
    @{NativeDomain='load';LoadSave=$save;ReloadInProcess=$true;Expected=$false},
    @{ColonyOwner=$true;LoadSave=$save;ResaveBeforeResume=$true;Presentation=$true;Expected=$false},
    @{ColonyOwner=$true;LoadSave=$save;ResaveBeforeResume=$true;NativeObserver=$true;Expected=$false},
    @{NativeDomain='load';LoadSave=$save;ResaveBeforeResume=$true;Presentation=$true;Expected=$false},
    @{NativeDomain='load';LoadSave=(Join-Path $FixtureRoot 'missing.sav');ResaveBeforeResume=$true;Expected=$false}
)) {
    $expected = $case.Expected; $case.Remove('Expected')
    $case.Package='unused'; $case.DlcMask=0; $case.Frames=80
    $actual=$false
    try { $actual = @(& $logic @case) -contains 'PREFLIGHT_OK' } catch { }
    if ($actual -ne $expected) { throw ('Invalid reload/wrong-mode permission: ' + ($case | ConvertTo-Json -Compress)) }
    $checks++
}
# Independently retain the actual Windows-only owner admission guard.
$ColonyOwner=$true; $refused=$false
try { & ([scriptblock]::Create($hostGuard[0].Extent.Text)) } catch { $refused=$true }
if ($refused -ne ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT)) { throw 'Windows-host permission changed.' }
if ((Get-FileHash -LiteralPath $save).Hash -ne $before) { throw 'Argument validation changed its input save.' }
Write-Output "PASS actual runner preflight: $checks argument combinations; host guard; unchanged save"
""")

    def test_actual_framework_compatible_backup_path_helper(self):
        self.run_control(r"""
$helper = @($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Get-OniRelativeBackupPath'}, $true))
if ($helper.Count -ne 1) { throw 'Expected one bounded backup-path helper.' }
. ([scriptblock]::Create($helper[0].Extent.Text))
$root = Join-Path $FixtureRoot 'root with space'
$child = Join-Path $root 'mods/config/[literal]/appearance.json'
$expected = Join-Path 'mods' 'config/[literal]/appearance.json'
if ((Get-OniRelativeBackupPath -Root $root -Path $child) -ne $expected) { throw 'Nested literal path changed.' }
if ((Get-OniRelativeBackupPath -Root ($root + [IO.Path]::DirectorySeparatorChar) -Path $child) -ne $expected) { throw 'Trailing separator changed.' }
foreach ($outside in @($root, ($root + [IO.Path]::DirectorySeparatorChar), ($root + '-sibling/file'), (Join-Path $root '../outside'))) {
    $refused=$false
    try { Get-OniRelativeBackupPath -Root $root -Path $outside | Out-Null } catch { $refused=$true }
    if (!$refused) { throw "Accepted a non-descendant backup path: $outside" }
}
Write-Output 'PASS actual backup helper: nested/literal paths, trailing separator, root/sibling/traversal refusals'
""")


if __name__ == "__main__":
    unittest.main()
