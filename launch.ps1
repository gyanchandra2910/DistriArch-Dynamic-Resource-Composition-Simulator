param([switch]$Setup, [ValidateRange(1024,65535)][int]$Port = 8000)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
function Require-Success([string]$Step) {
    if ($LASTEXITCODE -ne 0) { throw "$Step failed (exit $LASTEXITCODE)." }
}
if ($Setup -or -not (Test-Path 'build\distriarch.exe')) {
    & (Join-Path $PSScriptRoot 'build.ps1')
}
if ($Setup -or -not (Test-Path 'frontend\dist\index.html')) {
    $taskRuntime = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies'
    $taskNode = Get-Command node -ErrorAction SilentlyContinue
    if (-not $taskNode) {
        $taskNodeBin = Join-Path $taskRuntime 'node\bin'
        if (Test-Path $taskNodeBin) { $env:PATH = "$taskNodeBin;$env:PATH" }
    }
    $taskPnpm = Get-Command pnpm -ErrorAction SilentlyContinue
    if ($taskPnpm) { $taskPnpm = $taskPnpm.Source }
    else { $taskPnpm = Join-Path $taskRuntime 'bin\fallback\pnpm.cmd' }
    if (-not (Test-Path -LiteralPath $taskPnpm)) { throw 'Install Node.js 22.12+ and pnpm 11.25.0 for the dashboard build.' }
    Push-Location 'frontend'
    try {
        & $taskPnpm install --frozen-lockfile
        Require-Success 'Installing frontend dependencies'
        & $taskPnpm run build
        Require-Success 'Building the dashboard'
    } finally { Pop-Location }
}
Write-Host "DistriArch C++20: http://127.0.0.1:$Port" -ForegroundColor Cyan
Write-Host 'Press Ctrl+C to stop. Native engine and API; production React dashboard.'
& (Join-Path $PSScriptRoot 'build\distriarch.exe') serve --root $PSScriptRoot --port $Port
Require-Success 'Starting DistriArch'
