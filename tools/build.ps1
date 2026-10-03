# Builds build\Release\version.dll with the VS Build Tools' bundled CMake,
# then runs hookcheck to install every hook against the real H1Z1.exe image.
param([string]$Config = "Release", [switch]$NoCheck)
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { $vs = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools" }
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { $cmake = "cmake" }

$sourceHash = & "$PSScriptRoot\sourcehash.ps1"  # taken before building, so later edits don't count

& $cmake -S $root -B "$root\build" -A x64
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build "$root\build" --config $Config
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($NoCheck) { exit 0 }

& "$root\build\$Config\hookcheck.exe"
$failed = $LASTEXITCODE
Select-String -Path "$root\build\$Config\rebuild.log" -Pattern "ERROR|failed" | ForEach-Object { $_.Line }
if ($failed -eq 0) { Set-Content "$root\build\last-good.sha" $sourceHash -Encoding ascii }
exit $failed
