param([switch]$SkipBuild, [switch]$SkipHttp)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') }
& .\build\distriarch.exe experiments --output results
if ($LASTEXITCODE) { throw 'Native experiment audit failed' }
& .\build\distriarch_tests.exe --all
if ($LASTEXITCODE) { throw 'Native tests failed' }
if (-not $SkipHttp) {
    $taskNode = Get-Command node -ErrorAction SilentlyContinue
    if ($taskNode) { $taskNode = $taskNode.Source }
    else { $taskNode = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe' }
    if (-not (Test-Path -LiteralPath $taskNode)) { throw 'Node.js is needed only for the optional HTTP development harness. Use -SkipHttp to run all native checks.' }
    & $taskNode tests/http_smoke.mjs
    if ($LASTEXITCODE) { throw 'Real HTTP/CLI integration failed' }
    & $taskNode --experimental-strip-types --test tests/replay_tests.mjs
    if ($LASTEXITCODE) { throw 'Replay/config regression tests failed' }
}
Write-Host 'All requested native verification checks passed.' -ForegroundColor Green
