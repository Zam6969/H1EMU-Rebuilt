# Prints one hash over everything that affects the build (src/, tools/*.cpp,
# CMakeLists.txt). build.ps1 records it after a passing build+hookcheck;
# autosync.ps1 only pushes when the tree still matches that hash.
$root = Split-Path $PSScriptRoot -Parent
$files = @(Get-ChildItem "$root\src" -Recurse -File) + @(Get-ChildItem "$root\tools" -Filter *.cpp -File) +
         @(Get-Item "$root\CMakeLists.txt")
$lines = $files | Sort-Object FullName | ForEach-Object {
  "$($_.FullName.Substring($root.Length)) $((Get-FileHash $_.FullName -Algorithm SHA256).Hash)"
}
$bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
$sha = [Security.Cryptography.SHA256]::Create()
-join ($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") })
