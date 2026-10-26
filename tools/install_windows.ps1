# Run from an extracted private Windows package with ONI closed.
[CmdletBinding()]
param(
    [string]$Package,
    [string]$OniUserRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Resolve defaults after parameter binding, when the script's location is set.
if ([string]::IsNullOrWhiteSpace($Package)) {
    $Package = Join-Path $PSScriptRoot 'MixedAtmospheres'
}
if ([string]::IsNullOrWhiteSpace($OniUserRoot)) {
    $documents = [Environment]::GetFolderPath('MyDocuments')
    if ([string]::IsNullOrWhiteSpace($documents)) {
        throw 'Windows Documents folder could not be located. Specify -OniUserRoot with your ONI user-data folder.'
    }
    $OniUserRoot = Join-Path $documents 'Klei\OxygenNotIncluded'
}
if (Get-Process -Name OxygenNotIncluded -ErrorAction SilentlyContinue) {
    throw 'Close Oxygen Not Included before installing.'
}
$source = (Resolve-Path -LiteralPath $Package).Path
$manifest = Get-Content -LiteralPath (Join-Path $source 'package-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.native_observer_enabled -ne $true) { throw 'The Windows package needs its native backend.' }
foreach ($name in @('MixedAtmospheres.Oni.dll', 'MixedAtmospheres.Core.dll', 'native/win-x64/mixed_atmospheres.dll', 'native/win-x64/mixed_atmospheres_oni.dll')) {
    if (!$manifest.files.PSObject.Properties[$name]) { throw "Package is missing $name" }
}
foreach ($entry in $manifest.files.PSObject.Properties) {
    $file = [IO.Path]::GetFullPath((Join-Path $source $entry.Name))
    if (!$file.StartsWith($source.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid package path.' }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) {
        throw "Package hash mismatch: $($entry.Name)"
    }
}
$root = [IO.Path]::GetFullPath($OniUserRoot)
$mods = Join-Path $root 'mods'
if ($source.StartsWith($mods.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Extract the package outside the mods folder before installing.'
}
$dev = Join-Path $mods 'Dev'
$target = Join-Path $dev 'MixedAtmospheres.IntegrationProbe'
$token = [Guid]::NewGuid().ToString('N')
$staging = Join-Path $dev ('.MixedAtmospheres-install-' + $token)
$backup = Join-Path $root ('MixedAtmospheres-install-backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $token)
$moved = @()
New-Item -ItemType Directory -Path $dev -Force | Out-Null
try {
    Copy-Item -LiteralPath $source -Destination $staging -Recurse
    foreach ($entry in $manifest.files.PSObject.Properties) {
        if ((Get-FileHash -LiteralPath (Join-Path $staging $entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) {
            throw "Installed copy hash mismatch: $($entry.Name)"
        }
    }
    # Move old copies outside ONI's scanned mod folders. User config and saves
    # are siblings of these folders and are never moved or deleted.
    foreach ($distribution in @('Dev', 'Local', 'Steam')) {
        $folder = Join-Path $mods $distribution
        if (!(Test-Path -LiteralPath $folder -PathType Container)) { continue }
        foreach ($old in Get-ChildItem -LiteralPath $folder -Directory) {
            if ($old.FullName -eq $staging) { continue }
            if ($old.FullName -ne $target -and !(Test-Path -LiteralPath (Join-Path $old.FullName 'MixedAtmospheres.Oni.dll'))) { continue }
            $saved = Join-Path $backup ($distribution + '\' + $old.Name)
            New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
            Move-Item -LiteralPath $old.FullName -Destination $saved
            $moved += [PSCustomObject]@{ Original = $old.FullName; Backup = $saved }
        }
    }
    Move-Item -LiteralPath $staging -Destination $target
} catch {
    # A failed update restores every previous mod directory.
    foreach ($old in $moved) { Move-Item -LiteralPath $old.Backup -Destination $old.Original }
    throw
} finally {
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
}
Write-Host "Installed Mixed Atmospheres in $target"
if ($moved.Count) { Write-Host "Previous copies kept in $backup" }
Write-Host 'Start ONI, enable Mixed Atmospheres in Mods, and restart when asked.'
Write-Host 'In the main menu, turn OFF Spaced Out! and any other gameplay packs (ONI restarts), then start a new base-game colony.'
