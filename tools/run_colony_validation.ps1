param(
    [Parameter(Mandatory = $true)][string]$Package,
    [string]$OniUserRoot = "$env:USERPROFILE\Documents\Klei\OxygenNotIncluded",
    [string]$Steam = "${env:ProgramFiles(x86)}\Steam\steam.exe",
    [string]$Output,
    [switch]$ReuseGeneratedWorld,
    [switch]$Presentation,
    [switch]$KeepOpen,
    [int]$Frames = 30,
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$oniWorkspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$oniPackagePath = (Resolve-Path -LiteralPath $Package).Path
$oniRootPath = (Resolve-Path -LiteralPath $OniUserRoot).Path
if (Get-Process OxygenNotIncluded -ErrorAction SilentlyContinue) { throw 'Close ONI before running the disposable colony test.' }
if (!(Test-Path -LiteralPath $Steam -PathType Leaf)) { throw 'Steam is not installed at the supplied path.' }
$oniManifest = Get-Content -LiteralPath (Join-Path $oniPackagePath 'package-manifest.json') -Raw | ConvertFrom-Json
if ($oniManifest.kind -ne 'integration-probe' -or $oniManifest.release_ready -ne $false) { throw 'Supply a built private integration probe package.' }
foreach ($oniEntry in $oniManifest.files.PSObject.Properties) {
    $oniFile = Join-Path $oniPackagePath $oniEntry.Name
    if ((Get-FileHash -LiteralPath $oniFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $oniEntry.Value) {
        throw "Package hash mismatch: $($oniEntry.Name)"
    }
}
$oniRunId = [guid]::NewGuid().ToString('N')
if (!$Output) { $Output = Join-Path $oniWorkspace "local\colony-runs\$oniRunId" }
$oniEvidence = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $oniEvidence) { throw 'Use a fresh evidence directory.' }
New-Item -ItemType Directory -Path $oniEvidence | Out-Null
$oniBackups = Join-Path $oniEvidence 'originals'
New-Item -ItemType Directory -Path $oniBackups | Out-Null
$oniModsPath = Join-Path $oniRootPath 'mods\mods.json'
$oniFolderName = "MixedAtmospheres-ColonyValidation-$oniRunId"
$oniColonyName = "MA Visual Test $($oniRunId.Substring(0, 8))"
foreach ($oniSaveKind in @('save_files', 'RetiredColonies')) {
    if (Test-Path -LiteralPath (Join-Path $oniRootPath "$oniSaveKind\$oniColonyName")) {
        throw 'The disposable colony name must not match an existing save directory.'
    }
}
$oniAppearancePath = Join-Path $oniRootPath 'mods\config\MixedAtmospheres\appearance.json'
$oniAppearanceExisted = Test-Path -LiteralPath $oniAppearancePath
# ONI's pinned Mono runtime uses the legacy deterministic string hash for a
# local-mod label version. Matching it avoids a fake 'updated mod' reinstall.
$oniHashA = [long]5381; $oniHashB = [long]5381
for ($oniIndex = 0; $oniIndex -lt $oniFolderName.Length; $oniIndex += 2) {
    $oniHashA = (($oniHashA * 33) -bxor [int][char]$oniFolderName[$oniIndex]) -band 4294967295L
    if ($oniIndex + 1 -lt $oniFolderName.Length) {
        $oniHashB = (($oniHashB * 33) -bxor [int][char]$oniFolderName[$oniIndex + 1]) -band 4294967295L
    }
}
$oniLabelVersion = ($oniHashA + $oniHashB * 1566083941L) -band 4294967295L
if ($oniLabelVersion -ge 2147483648L) { $oniLabelVersion -= 4294967296L }
$oniInstalledMod = [IO.Path]::GetFullPath((Join-Path $oniRootPath "mods\Dev\$oniFolderName"))
$oniDevRoot = [IO.Path]::GetFullPath((Join-Path $oniRootPath 'mods\Dev'))
if ([IO.Path]::GetDirectoryName($oniInstalledMod) -ne $oniDevRoot) { throw 'Invalid test installation directory.' }
$oniOriginals = @()
$oniFiles = @((Get-Item -LiteralPath $oniModsPath), (Get-Item -LiteralPath (Join-Path $oniRootPath 'kplayerprefs.yaml')))
if ($oniAppearanceExisted) { $oniFiles += Get-Item -LiteralPath $oniAppearancePath }
$oniFiles += @(Get-ChildItem -LiteralPath $oniRootPath -File | Where-Object Name -Like 'WorldGen*')
foreach ($oniFile in $oniFiles) {
    $oniRelative = [IO.Path]::GetRelativePath($oniRootPath, $oniFile.FullName)
    $oniBackupFile = Join-Path $oniBackups $oniRelative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($oniBackupFile)) -Force | Out-Null
    Copy-Item -LiteralPath $oniFile.FullName -Destination $oniBackupFile
    $oniOriginals += [pscustomobject]@{ Path = $oniFile.FullName; Backup = $oniBackupFile; Hash = (Get-FileHash -LiteralPath $oniFile.FullName).Hash }
}
$oniOriginals | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $oniEvidence 'restoration-manifest.json') -Encoding utf8
$oniGameId = $null
$oniLaunchUtc = [DateTime]::UtcNow
$oniProcessIds = @()
$oniExitCode = 1
try {
    Copy-Item -LiteralPath $oniPackagePath -Destination $oniInstalledMod -Recurse
    @{
        Mode = $(if ($Presentation) { 'presentation' } else { 'snapshot-copy' }); AutoStartNewColony = $true; ReuseGeneratedWorld = [bool]$ReuseGeneratedWorld
        CompletedFrames = $Frames; TimeoutSeconds = $TimeoutSeconds; QuitWhenComplete = !$KeepOpen; Seed = '147147'
        ColonyName = $oniColonyName
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $oniInstalledMod 'colony-validation.json') -Encoding utf8
    $oniModSettings = Get-Content -LiteralPath $oniModsPath -Raw | ConvertFrom-Json
    foreach ($oniMod in $oniModSettings.mods) { $oniMod.enabled = $false; $oniMod.enabledForDlc = @() }
    $oniModSettings.mods = @($oniModSettings.mods) + @([pscustomobject]@{
        label = @{ distribution_platform = 4; id = $oniFolderName; title = 'Mixed Atmospheres - Integration Probe'; version = $oniLabelVersion }
        status = 1; enabled = $true; enabledForDlc = @('EXPANSION1_ID', 'VANILLA_ID', ''); crash_count = 0
        reinstall_path = $null; staticID = 'MixedAtmospheres.IntegrationProbe'
    })
    $oniModSettings | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $oniModsPath -Encoding utf8
    $oniLog = Join-Path $oniEvidence 'Player.log'
    Start-Process -FilePath $Steam -ArgumentList @('-applaunch', '457140', '-screen-fullscreen', '0', '-screen-width', '1280', '-screen-height', '720', '-logFile', ('"' + $oniLog + '"')) -WindowStyle Hidden | Out-Null
    $oniDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 120)
    $oniLaunchDeadline = [DateTime]::UtcNow.AddSeconds(90)
    while ([DateTime]::UtcNow -lt $oniLaunchDeadline) {
        $oniGame = Get-Process OxygenNotIncluded -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($oniGame) { $oniGameId = $oniGame.Id; $oniProcessIds += $oniGameId; break }
        Start-Sleep -Seconds 1
    }
    if (!$oniGameId) { throw 'Steam did not launch ONI.' }
    Write-Output "Started disposable colony test PID $oniGameId. Evidence: $oniEvidence"
    while ($true) {
        if ([DateTime]::UtcNow -gt $oniDeadline) { throw 'ONI test process timed out.' }
        if (!(Get-Process -Id $oniGameId -ErrorAction SilentlyContinue)) {
            $oniResultPath = Join-Path $oniInstalledMod 'game-contract-local\colony-validation\colony-validation.json'
            $oniFinished = $false
            if (Test-Path -LiteralPath $oniResultPath) {
                $oniFinished = (Get-Content -LiteralPath $oniResultPath -Raw | ConvertFrom-Json).Status -in @('passed', 'failed')
            }
            if ($oniFinished) { break }
            # ONI's informational workshop-mod dialog can restart its own
            # process. Retain the isolated installation until that child exits.
            $oniRestartDeadline = [DateTime]::UtcNow.AddSeconds(15)
            $oniRestart = $null
            while ([DateTime]::UtcNow -lt $oniRestartDeadline) {
                $oniRestart = Get-Process OxygenNotIncluded -ErrorAction SilentlyContinue |
                    Where-Object { $_.StartTime.ToUniversalTime() -ge $oniLaunchUtc -and $_.Id -notin $oniProcessIds } |
                    Select-Object -First 1
                if ($oniRestart) { break }
                Start-Sleep -Seconds 1
            }
            if (!$oniRestart) { break }
            $oniGameId = $oniRestart.Id; $oniProcessIds += $oniGameId
            Write-Output "Following ONI's own restart to PID $oniGameId."
        }
        Start-Sleep -Seconds 2
    }
    $oniProcessIds | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $oniEvidence 'test-process-ids.json') -Encoding utf8
    $oniResult = Join-Path $oniInstalledMod 'game-contract-local\colony-validation\colony-validation.json'
    if (!(Test-Path -LiteralPath $oniResult)) { throw 'Game exited without colony validation evidence.' }
    $oniResultData = Get-Content -LiteralPath $oniResult -Raw | ConvertFrom-Json
    $oniResultData | ConvertTo-Json -Depth 20 | Write-Output
    if ($oniResultData.Status -eq 'passed') { $oniExitCode = 0 }
}
finally {
    if ($oniGameId -and (Get-Process -Id $oniGameId -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $oniGameId -Force
        Wait-Process -Id $oniGameId -Timeout 15 -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $oniInstalledMod) {
        # Move only this invocation's explicitly checked immediate Dev child.
        if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($oniInstalledMod)) -ne $oniDevRoot) { throw 'Invalid cleanup directory.' }
        Move-Item -LiteralPath $oniInstalledMod -Destination (Join-Path $oniEvidence 'mod')
    }
    $oniFinalLog = Join-Path $env:USERPROFILE 'AppData\LocalLow\Klei\Oxygen Not Included\Player.log'
    if (Test-Path -LiteralPath $oniFinalLog) { Copy-Item -LiteralPath $oniFinalLog -Destination (Join-Path $oniEvidence 'Player-final.log') }
    $oniGeneratedCache = Join-Path $oniEvidence 'generated-world-cache'
    New-Item -ItemType Directory -Path $oniGeneratedCache -Force | Out-Null
    Get-ChildItem -LiteralPath $oniRootPath -File | Where-Object Name -Like 'WorldGen*' | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $oniGeneratedCache $_.Name)
    }
    foreach ($oniOriginal in $oniOriginals) {
        Copy-Item -LiteralPath $oniOriginal.Backup -Destination $oniOriginal.Path -Force
        if ((Get-FileHash -LiteralPath $oniOriginal.Path).Hash -ne $oniOriginal.Hash) { throw "Restoration failed: $($oniOriginal.Path)" }
    }
    if (!$oniAppearanceExisted -and (Test-Path -LiteralPath $oniAppearancePath)) {
        Copy-Item -LiteralPath $oniAppearancePath -Destination (Join-Path $oniEvidence 'test-appearance.json')
        Remove-Item -LiteralPath $oniAppearancePath
    }
    foreach ($oniSaveKind in @('save_files', 'RetiredColonies')) {
        $oniSaveParent = [IO.Path]::GetFullPath((Join-Path $oniRootPath $oniSaveKind))
        $oniTestSave = [IO.Path]::GetFullPath((Join-Path $oniSaveParent $oniColonyName))
        if ([IO.Path]::GetDirectoryName($oniTestSave) -ne $oniSaveParent) { throw 'Invalid disposable colony directory.' }
        if (Test-Path -LiteralPath $oniTestSave -PathType Container) {
            $oniSaveEvidence = Join-Path $oniEvidence 'disposable-colony'
            New-Item -ItemType Directory -Path $oniSaveEvidence -Force | Out-Null
            Move-Item -LiteralPath $oniTestSave -Destination (Join-Path $oniSaveEvidence $oniSaveKind)
        }
    }
    Write-Output "Restored original mods, preferences and generated-world files byte-for-byte. Evidence: $oniEvidence"
}
exit $oniExitCode
