# Commits and pushes the rebuild to GitHub, but only if the current sources
# are exactly the ones that last passed build.ps1 (compile + hookcheck), so
# half-written code never reaches the repo. Safe to run on a timer.
# git prints CRLF/progress notices on stderr; those must not abort the sync.
$ErrorActionPreference = "Continue"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

$good = if (Test-Path "$root\build\last-good.sha") { (Get-Content "$root\build\last-good.sha" -Raw).Trim() } else { "" }
$current = & "$PSScriptRoot\sourcehash.ps1"
if ($good -ne $current) {
  Write-Output "autosync: sources changed since the last passing build - not pushing"
  exit 0
}

git -c core.safecrlf=false add -A 2>&1 | Out-Null
git diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
  $count = (Select-String -Path "$root\src\*\*.cpp" -Pattern "REBUILD_FUNCTION(_TOO_SMALL)?\(" | Measure-Object).Count
  $message = "Auto-sync: $count rebuilt functions (build + hookcheck passing)`n`nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`n"
  $file = [IO.Path]::GetTempFileName()
  [IO.File]::WriteAllText($file, $message, (New-Object Text.UTF8Encoding $false))
  git commit -q -F $file
  Remove-Item $file
}
git push -q 2>&1 | Out-Null
Write-Output "autosync: $(git log --oneline -1)"
