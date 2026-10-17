param(
    [Parameter(Mandatory=$true)][string]$ProbeDirectory,
    [Parameter(Mandatory=$true)][string]$NativeDll,
    [Parameter(Mandatory=$true)][string]$ContractDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateSet('baseline','multi-region','fluid-actors','sanitize','rebuild-shadow','load-shadow','transition','filtered-transition','reject-custom-envelope','sealed-gas','sealed-water-small-heat','sealed-water-heat','sealed-water-transfer','sealed-water','sealed-heat','sealed-conduction','sealed-gravity','sealed-checkpoint','sealed-pair','sealed-restore','sealed-restore-water','sealed-restore-gravity','zero-time-frame','native-disease-retention')][string]$Mode = 'baseline',
    [string]$Observer,
    [string]$GameDirectory,
    [string]$Dotnet = 'dotnet.exe'
)
$ErrorActionPreference = 'Stop'
if ([bool]$Observer -ne [bool]$GameDirectory -or (($Mode.StartsWith('sealed-') -or $Mode -eq 'zero-time-frame') -and !$Observer)) { throw 'Sealed domains and zero-time controls require both -Observer and -GameDirectory.' }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$report = Join-Path $OutputDirectory "$Mode.json"
$stdout = Join-Path $OutputDirectory "$Mode.stdout.log"
$stderr = Join-Path $OutputDirectory "$Mode.stderr.log"
$localProbe = Join-Path ([System.IO.Path]::GetTempPath()) ('oni-shadow-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $localProbe | Out-Null
foreach ($name in @('NativeShadowProbe.dll','NativeShadowProbe.deps.json','NativeShadowProbe.runtimeconfig.json','MixedAtmospheres.Core.dll')) {
    Copy-Item -LiteralPath (Join-Path $ProbeDirectory $name) -Destination (Join-Path $localProbe $name)
}
$assembly = Join-Path $localProbe 'NativeShadowProbe.dll'
# Execute a local copy: Windows may reject managed assembly loading from SMB.
# Quote paths, reject embedded quotes, and bound this process's lifetime.
$values = @($assembly, $NativeDll, $ContractDirectory, $Mode, $report)
if ($Observer) { $values += @($Observer, $GameDirectory) }
foreach ($value in $values) { if ($value.Contains('"')) { throw 'Quote in argument.' } }
$arguments = ($values | ForEach-Object { '"' + $_ + '"' }) -join ' '
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $Dotnet
$start.Arguments = $arguments
$start.WorkingDirectory = $localProbe
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = [System.Diagnostics.Process]::Start($start)
$outputTask = $process.StandardOutput.ReadToEndAsync()
$errorTask = $process.StandardError.ReadToEndAsync()
$finished = $process.WaitForExit(45000)
if (!$finished) {
    $process.Kill()
    $process.WaitForExit()
}
[System.IO.File]::WriteAllText($stdout, $outputTask.Result)
[System.IO.File]::WriteAllText($stderr, $errorTask.Result)
Get-Content $stdout
Get-Content $stderr
if (Test-Path $report) { Get-Content $report }
if (!$finished) { throw "Isolated probe exceeded 45 seconds; killed only probe PID $($process.Id)." }
exit $process.ExitCode
