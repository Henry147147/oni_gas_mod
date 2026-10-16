param(
    [Parameter(Mandatory = $true)][string]$ReviewedReport,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][ValidateRange(0, 3)][int]$ExpectedCase,
    [switch]$AllNonTraceRowsVisible,
    [switch]$TraceRowsOmitted,
    [switch]$HazardWarningVisible,
    [switch]$NoSelection
)
$ErrorActionPreference = 'Stop'
# These switches are the operator's pixel-review assertions, not conclusions
# inferred from the report. Never publish a receipt before reviewing the image.
if (!$AllNonTraceRowsVisible -or !$TraceRowsOmitted -or !$HazardWarningVisible -or !$NoSelection) {
    throw 'All four explicit pixel-review assertions are required.'
}
function Require-Integer([object]$Value, [string]$Name, [long]$Minimum, [long]$Maximum) {
    if (($Value -isnot [int] -and $Value -isnot [long]) -or $Value -lt $Minimum -or $Value -gt $Maximum) {
        throw "Invalid held observation integer: $Name"
    }
    return [long]$Value
}
$reportPath = (Resolve-Path -LiteralPath $ReviewedReport).Path
$directory = [IO.Path]::TrimEndingDirectorySeparator((Resolve-Path -LiteralPath $OutputDirectory).Path)
if (!(Test-Path -LiteralPath $directory -PathType Container)) { throw 'Receipt output must be an existing directory.' }
$bytes = [IO.File]::ReadAllBytes($reportPath)
if ($bytes.Length -gt 65536) { throw 'Held hover report exceeds its byte limit.' }
$encoding = [Text.UTF8Encoding]::new($false, $true)
$report = $encoding.GetString($bytes).TrimStart([char]0xfeff) | ConvertFrom-Json
if ($report.Kind -cne 'actual-native-tile-hover-contents-fixture' -or $report.Status -cne 'pending' -or
    $report.Captured -isnot [bool] -or !$report.Captured -or
    $report.Error -isnot [string] -or $report.Error.Length -ne 0 -or
    $report.CallbackError -isnot [string] -or $report.CallbackError.Length -ne 0) {
    throw 'Supply the completed, held observation reviewed before continuation.'
}
$case = Require-Integer $report.Case 'Case' 0 3
$observation = $report.Observation
if ($case -ne $ExpectedCase -or (Require-Integer $observation.Case 'Observation.Case' 0 3) -ne $ExpectedCase -or
    $observation.NoSelection -isnot [bool] -or !$observation.NoSelection -or
    $observation.ShadowBarSelected -isnot [bool] -or $observation.ShadowBarSelected -or
    (Require-Integer $observation.DistinctHoverFrames 'DistinctHoverFrames' 3 ([int]::MaxValue)) -lt 3) {
    throw 'Held observation does not match the reviewed native hover case.'
}
$receipt = [ordered]@{
    Case = $ExpectedCase
    Cell = [int](Require-Integer $observation.Cell 'Cell' 0 ([int]::MaxValue))
    Revision = Require-Integer $observation.Revision 'Revision' 0 ([long]::MaxValue)
    PublicationGeneration = Require-Integer $observation.PublicationGeneration 'PublicationGeneration' 0 ([long]::MaxValue)
    ScreenWidth = [int](Require-Integer $observation.ScreenWidth 'ScreenWidth' 1 ([int]::MaxValue))
    ScreenHeight = [int](Require-Integer $observation.ScreenHeight 'ScreenHeight' 1 ([int]::MaxValue))
    AllNonTraceRowsVisible = $true
    TraceRowsOmitted = $true
    HazardWarningVisible = $true
    NoSelection = $true
}
$names = @('default-all-gases', 'exact-threshold', 'below-threshold', 'ui-scale-125')
$destination = [IO.Path]::GetFullPath((Join-Path $directory ($names[$ExpectedCase] + '.continue.json')))
$temporary = [IO.Path]::GetFullPath((Join-Path $directory ('.hover-receipt-' + [guid]::NewGuid().ToString('N') + '.tmp')))
if ([IO.Path]::GetDirectoryName($destination) -ne $directory -or [IO.Path]::GetDirectoryName($temporary) -ne $directory) {
    throw 'Receipt and temporary file must be immediate children of the output directory.'
}
if ([IO.File]::Exists($destination)) { throw 'A receipt for this case already exists; it will not be replaced.' }
$encoded = $encoding.GetBytes(($receipt | ConvertTo-Json -Compress))
if ($encoded.Length -gt 4096) { throw 'Hover receipt exceeds its byte limit.' }
try {
    $stream = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($encoded, 0, $encoded.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    # Rename only after the writer is closed. The native poller must never see
    # a locked or incomplete final receipt. Same directory, no overwrite/retry.
    [IO.File]::Move($temporary, $destination)
}
finally {
    if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
}
Write-Output $destination
