param(
    [Parameter(Mandatory = $true)][string]$Package,
    [string]$OniUserRoot = "$env:USERPROFILE\Documents\Klei\OxygenNotIncluded",
    [string]$Steam = "${env:ProgramFiles(x86)}\Steam\steam.exe",
    [string]$Output,
    [switch]$ReuseGeneratedWorld,
    [switch]$Presentation,
    [switch]$TileHover,
    [switch]$CoupledPresentation,
    [switch]$NativeObserver,
    [switch]$SaveColony,
    [switch]$ResaveBeforeResume,
    [switch]$VerifySaveIoRecovery,
    [switch]$VerifyPersistentActors,
    [switch]$ReloadInProcess,
    [ValidateSet('gas','water','import','water-import','water-transfer','heat','water-heat','transfer','actor','water-actor','carbon','carbon-particle','breathing-read','pressure-read','water-pressure-read','conduction','gravity','water-gravity','checkpoint','pair','save','load')][string]$NativeDomain,
    [string]$LoadSave,
    [string]$ModId = 'MixedAtmospheres.IntegrationProbe',
    [string]$Python = 'python',
    [switch]$IdleReservoirLease,
    [switch]$PrivateFileProtocol,
    [switch]$LoadRejection,
    [switch]$KeepOpen,
    [int]$Frames = 30,
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
if ($NativeDomain -and ($NativeObserver -or $Presentation -or $TileHover -or $CoupledPresentation -or $IdleReservoirLease -or $PrivateFileProtocol -or $LoadRejection)) { throw 'A native domain uses its own disposable colony scene.' }
if ($NativeDomain -in @('actor','water-actor') -and $Frames -lt 80) { throw 'The actor registration/change/removal scene requires at least 80 frames.' }
if ($VerifyPersistentActors -and ($NativeDomain -notin @('gas','water') -or !$SaveColony -or $Frames -lt 80)) { throw 'VerifyPersistentActors requires NativeDomain gas or water, SaveColony and at least eighty frames.' }
if ($NativeDomain -in @('carbon-particle','breathing-read') -and $Frames -lt 30) { throw 'The CO2 particle and breathing-reader controls require at least 30 frames.' }
$oniDomainModes = @{ import='native-import-domain'; 'water-import'='native-liquid-import-domain'; gas='native-domain'; water='native-liquid-domain'; 'water-transfer'='native-liquid-transfer-domain'; heat='native-heat-domain'; 'water-heat'='native-liquid-heat-domain'; transfer='native-transfer-domain'; actor='native-actor-domain'; 'water-actor'='native-liquid-actor-domain'; carbon='native-carbon-domain'; 'carbon-particle'='native-carbon-particle-domain'; 'breathing-read'='native-breathing-read-domain'; 'pressure-read'='native-pressure-read-domain'; 'water-pressure-read'='native-liquid-pressure-read-domain'; conduction='native-conduction-domain'; gravity='native-gravity-domain'; 'water-gravity'='native-liquid-gravity-domain'; checkpoint='native-checkpoint-domain'; pair='native-paired-checkpoint-domain'; save='native-save-domain'; load='native-load-domain' }
if ([bool]$LoadSave -ne ($NativeDomain -eq 'load')) { throw 'NativeDomain load requires -LoadSave; other modes cannot use it.' }
if ($LoadSave) {
    $LoadSave = (Resolve-Path -LiteralPath $LoadSave).Path
    if (!(Test-Path -LiteralPath $LoadSave -PathType Leaf)) { throw 'LoadSave must name an existing colony file.' }
}
if ($ResaveBeforeResume -and $NativeDomain -ne 'load') { throw 'ResaveBeforeResume requires NativeDomain load.' }
if ($ReloadInProcess -and ($NativeDomain -ne 'load' -or !$ResaveBeforeResume)) { throw 'ReloadInProcess requires NativeDomain load and ResaveBeforeResume.' }
if ($VerifySaveIoRecovery -and ($NativeDomain -ne 'save' -or $Frames -lt 16)) { throw 'VerifySaveIoRecovery requires NativeDomain save and at least sixteen frames.' }
if ($SaveColony -and (!$NativeDomain -or $NativeDomain -eq 'load')) { throw 'SaveColony requires an initial native domain scene.' }
$oniNeedsNative = [bool]($NativeObserver -or $NativeDomain)
if ($NativeObserver -and ($Presentation -or $TileHover -or $CoupledPresentation -or $IdleReservoirLease -or $PrivateFileProtocol -or $LoadRejection)) { throw 'Native observer validation uses its own forwarding-only colony mode.' }
if ($CoupledPresentation -and ($TileHover -or $Presentation -or $IdleReservoirLease -or $PrivateFileProtocol -or $LoadRejection)) { throw 'Coupled presentation uses its own explicit closed-reference scene.' }
if ($IdleReservoirLease -and !$Presentation) { throw 'The idle reservoir lease check requires -Presentation.' }
if ($TileHover -and ($Presentation -or $IdleReservoirLease -or $PrivateFileProtocol -or $LoadRejection)) { throw 'Tile hover uses its own isolated visual fixture.' }
if ($PrivateFileProtocol -and !$IdleReservoirLease) { throw 'Private file protocol validation requires -IdleReservoirLease.' }
if ($LoadRejection -and ($Presentation -or $IdleReservoirLease -or $PrivateFileProtocol)) { throw 'Load rejection uses a separate ordinary native colony.' }
$oniWorkspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$oniPackagePath = (Resolve-Path -LiteralPath $Package).Path
$oniRootPath = (Resolve-Path -LiteralPath $OniUserRoot).Path
if (Get-Process OxygenNotIncluded -ErrorAction SilentlyContinue) { throw 'Close ONI before running the disposable colony test.' }
if (!(Test-Path -LiteralPath $Steam -PathType Leaf)) { throw 'Steam is not installed at the supplied path.' }
$oniManifest = Get-Content -LiteralPath (Join-Path $oniPackagePath 'package-manifest.json') -Raw | ConvertFrom-Json
if ($oniManifest.kind -ne 'integration-probe' -or $oniManifest.release_ready -ne $false) { throw 'Supply a built private integration probe package.' }
if ([bool]$oniManifest.native_observer_enabled -ne $oniNeedsNative) { throw 'Native packages require explicit -NativeObserver or -NativeDomain; other modes require a package without native hooks.' }
foreach ($oniEntry in $oniManifest.files.PSObject.Properties) {
    $oniFile = Join-Path $oniPackagePath $oniEntry.Name
    if ((Get-FileHash -LiteralPath $oniFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $oniEntry.Value) {
        throw "Package hash mismatch: $($oniEntry.Name)"
    }
}
$oniFileProtocolEntry = $oniManifest.files.PSObject.Properties['private-validation/GameMonoFileProtocolValidation.dll']
if ($PrivateFileProtocol -and !$oniFileProtocolEntry) { throw 'The package does not contain the pinned private file protocol assembly.' }
$oniRunId = [guid]::NewGuid().ToString('N')
if (!$Output) { $Output = Join-Path $oniWorkspace "local\colony-runs\$oniRunId" }
$oniEvidence = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $oniEvidence) { throw 'Use a fresh evidence directory.' }
New-Item -ItemType Directory -Path $oniEvidence | Out-Null
$oniBackups = Join-Path $oniEvidence 'originals'
New-Item -ItemType Directory -Path $oniBackups | Out-Null
$oniModsPath = Join-Path $oniRootPath 'mods\mods.json'
if ($ModId -notmatch '^[A-Za-z0-9_.-]{1,128}$' -or $ModId -in @('.', '..')) { throw 'ModId must be one safe local directory name.' }
$oniFolderName = $ModId
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
if (Test-Path -LiteralPath $oniInstalledMod) { throw 'The stable test mod folder already exists; finish its previous run or supply a different -ModId.' }
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
$oniLoadFixtureManifest = $null
try {
    Copy-Item -LiteralPath $oniPackagePath -Destination $oniInstalledMod -Recurse
    $oniValidationLoadPath = $LoadSave
    if ($LoadSave) {
        $oniLoadSourceHash = (Get-FileHash -LiteralPath $LoadSave -Algorithm SHA256).Hash.ToLowerInvariant()
        $oniStagedLoadSave = Join-Path $oniEvidence 'loaded-colony.sav'
        Copy-Item -LiteralPath $LoadSave -Destination $oniStagedLoadSave
        $oniStagedLoadHash = (Get-FileHash -LiteralPath $oniStagedLoadSave -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($oniStagedLoadHash -ne $oniLoadSourceHash -or
            (Get-FileHash -LiteralPath $LoadSave -Algorithm SHA256).Hash.ToLowerInvariant() -ne $oniLoadSourceHash) {
            throw 'Colony load source changed during staging.'
        }
        # Normal Save and Save As use the active file's directory. Loading a
        # separate copy keeps menu writes inside this invocation's evidence.
        $oniValidationLoadPath = $oniStagedLoadSave
        $oniLoadFixtureManifest = [ordered]@{
            SchemaVersion = 1; SourcePath = $LoadSave; SourceSha256Before = $oniLoadSourceHash
            StagedPath = $oniStagedLoadSave; StagedSha256Before = $oniStagedLoadHash
            SourceSha256After = $null; StagedSha256After = $null; SourceUnchanged = $null; StagedChanged = $null
        }
        $oniLoadFixtureManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $oniEvidence 'load-source-manifest.json') -Encoding utf8
    }
    $oniValidationConfiguration = @{
        Mode = $(if ($NativeDomain) { $oniDomainModes[$NativeDomain] } elseif ($NativeObserver) { 'native-observer' } elseif ($LoadRejection) { 'load-rejection' } elseif ($CoupledPresentation) { 'coupled-presentation' } elseif ($TileHover) { 'tile-hover' } elseif ($Presentation) { 'presentation' } else { 'snapshot-copy' }); AutoStartNewColony = $true; ReuseGeneratedWorld = [bool]$ReuseGeneratedWorld; LoadSavePath = $oniValidationLoadPath; SaveOnComplete = [bool]$SaveColony; SaveBeforeResume = [bool]$ResaveBeforeResume
        CompletedFrames = $Frames; TimeoutSeconds = $TimeoutSeconds; QuitWhenComplete = !$KeepOpen; Seed = '147147'
        ColonyName = $oniColonyName
    }
    if ($VerifySaveIoRecovery) { $oniValidationConfiguration.VerifySaveIoRecovery = $true }
    if ($VerifyPersistentActors) { $oniValidationConfiguration.VerifyPersistentActors = $true }
    if ($ReloadInProcess) { $oniValidationConfiguration.ReloadInProcess = $true }
    if ($IdleReservoirLease) { $oniValidationConfiguration.IdleReservoirLeaseOnly = $true }
    if ($PrivateFileProtocol) { $oniValidationConfiguration.PrivateFileProtocolAssemblySha256 = $oniFileProtocolEntry.Value }
    if ($LoadRejection) { $oniValidationConfiguration.PrivateOwnedLoadRejection = $true }
    $oniValidationConfiguration | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $oniInstalledMod 'colony-validation.json') -Encoding utf8
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
    if ($oniResultData.Status -eq 'passed') {
        if ($NativeDomain) {
            $oniDomainReport = Join-Path $oniInstalledMod 'game-contract-local\colony-validation\native-domain.json'
            $oniObserverReport = Join-Path $oniInstalledMod 'native-backend-observation-report.json'
            $oniEvidenceChecker = Join-Path $PSScriptRoot 'oni_native_domain_evidence.py'
            $oniCheckArguments = @($oniEvidenceChecker, '--domain', $oniDomainReport, '--observation', $oniObserverReport,
                '--mode', $NativeDomain, '--frames', [string]$Frames, '--platform', 'WindowsPlayer')
            if ($SaveColony) { $oniCheckArguments += '--saved' }
            if ($VerifySaveIoRecovery) { $oniCheckArguments += '--save-io-recovery' }
            if ($VerifyPersistentActors) { $oniCheckArguments += '--persistent-actors' }
            if ($ReloadInProcess) { $oniCheckArguments += '--reload-in-process' }
            & $Python @oniCheckArguments
            if ($LASTEXITCODE -ne 0) { throw 'Native physical domain, callback, projection or ownership acceptance failed.' }
        }
        if ($NativeObserver) {
            $oniObserverReportPath = Join-Path $oniInstalledMod 'native-backend-observation-report.json'
            if (!(Test-Path -LiteralPath $oniObserverReportPath -PathType Leaf)) { throw 'Missing native forwarding observer report.' }
            $oniObserverReport = Get-Content -LiteralPath $oniObserverReportPath -Raw | ConvertFrom-Json
            if (!$oniResultData.NativeObserverForwardingPassed -or !$oniResultData.AllCopiesByteIdentical -or
                $oniResultData.ObserverTimestepReturns -lt $Frames -or $oniResultData.ObserverPhysicalUpdates -lt $Frames -or
                !$oniObserverReport.OriginalNativePhysicsActive -or $oniObserverReport.AuthoritativeFluidOwner -or
                !$oniObserverReport.Observation.Attached -or $oniObserverReport.Observation.RecoveryRequired) { throw 'Native forwarding progress/equivalence or ownership assertion failed.' }
        }
        $oniExitCode = 0
    }
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
    # -logFile writes this invocation's authoritative log into its evidence
    # directory. The normal Unity log can belong to an older unrelated run.
    if ((Test-Path -LiteralPath $oniFinalLog) -and (Get-Item -LiteralPath $oniFinalLog).LastWriteTimeUtc -ge $oniLaunchUtc) {
        Copy-Item -LiteralPath $oniFinalLog -Destination (Join-Path $oniEvidence 'Player-final.log')
    }
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
    if ($null -ne $oniLoadFixtureManifest) {
        $oniLoadFixtureManifest.SourceSha256After = (Get-FileHash -LiteralPath $LoadSave -Algorithm SHA256).Hash.ToLowerInvariant()
        $oniLoadFixtureManifest.StagedSha256After = (Get-FileHash -LiteralPath $oniStagedLoadSave -Algorithm SHA256).Hash.ToLowerInvariant()
        $oniLoadFixtureManifest.SourceUnchanged = $oniLoadFixtureManifest.SourceSha256After -eq $oniLoadFixtureManifest.SourceSha256Before
        $oniLoadFixtureManifest.StagedChanged = $oniLoadFixtureManifest.StagedSha256After -ne $oniLoadFixtureManifest.StagedSha256Before
        $oniLoadFixtureManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $oniEvidence 'load-source-manifest.json') -Encoding utf8
        if (!$oniLoadFixtureManifest.SourceUnchanged) { throw 'Original colony load source changed during validation.' }
    }
    Write-Output "Restored original mods, preferences and generated-world files byte-for-byte. Evidence: $oniEvidence"
}
exit $oniExitCode
