# Copies the built proxy into the game folder. Re-run after every build.
#   -Uninstall  removes it again (the stock client then runs untouched)
param(
  [string]$GameDir = "C:\Users\zam\Documents\H1emu",
  [string]$Config = "Release",
  [switch]$Uninstall
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

if ($Uninstall) {
  Remove-Item "$GameDir\version.dll", "$GameDir\version_orig.dll", "$GameDir\version.pdb" -ErrorAction SilentlyContinue
  Write-Host "Removed rebuild from $GameDir"
  exit 0
}

if (Get-Process H1Z1 -ErrorAction SilentlyContinue) { throw "Close H1Z1.exe first - the DLL is locked while the game runs." }

$built = "$root\build\$Config"
Copy-Item "$built\version.dll" $GameDir -Force
if (Test-Path "$built\version.pdb") { Copy-Item "$built\version.pdb" $GameDir -Force }
# Real version.dll exports are forwarded to this copy (see version.def).
Copy-Item "$env:SystemRoot\System32\version.dll" "$GameDir\version_orig.dll" -Force
if (-not (Test-Path "$GameDir\rebuild.ini")) { Copy-Item "$root\rebuild.ini" $GameDir }
Write-Host "Installed to $GameDir - log: $GameDir\rebuild.log"
