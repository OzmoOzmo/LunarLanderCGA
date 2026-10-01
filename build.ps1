param(
    [string]$Watcom = $(if ($env:WATCOM) { $env:WATCOM } else { 'C:\temp\lunar-dos-tools' }),
    [string]$Dosbox = 'C:\Projects\RetroComputers\Emulators\DOSbox\DOSBox.exe',
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$compiler = Join-Path $Watcom 'binnt64\wcl.exe'
if (-not (Test-Path $compiler)) {
    $compiler = Join-Path $Watcom 'binnt\wcl.exe'
}
if (-not (Test-Path $compiler)) {
    throw "Open Watcom not found under '$Watcom'. Pass -Watcom or set WATCOM."
}

Push-Location $root
try {
    python convert_sprites.py
    if ($LASTEXITCODE -ne 0) { throw 'Sprite conversion failed.' }

    $env:WATCOM = $Watcom
    $env:INCLUDE = "$Watcom\h;$Watcom\h\dos"
    $env:PATH = "$(Split-Path $compiler);$env:PATH"
    & $compiler -q -ml -bt=dos LLander.c
    if ($LASTEXITCODE -ne 0) { throw 'DOS compilation failed.' }
    Write-Host "Built $(Join-Path $root 'LLander.exe') and LANDOFF.DAT / LANDON.DAT"

    if ($Run) {
        if (-not (Test-Path $Dosbox)) { throw "DOSBox not found at '$Dosbox'." }
        Start-Process -FilePath $Dosbox -ArgumentList @(
            '-c', "`"mount c $root`"", '-c', '"c:"', '-c', '"LLander.exe"'
        )
    }
} finally {
    Pop-Location
}
