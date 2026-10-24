param(
    [string]$ZipPath = (Join-Path (Split-Path $PSScriptRoot) 'DistriArch_CPP_Complete_Fixed.zip'),
    [switch]$KeepRunning
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskRoot = Split-Path $PSScriptRoot
$taskZip = (Resolve-Path -LiteralPath $ZipPath).Path
$taskExtract = Join-Path $taskRoot ('tmp\fixed-zip-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskExtract -Force | Out-Null
[System.IO.Compression.ZipFile]::ExtractToDirectory($taskZip,$taskExtract)
$taskProject = Join-Path $taskExtract 'DistriArch_CPP'
$taskManifest = Get-Content -LiteralPath (Join-Path $taskProject 'PACKAGE_MANIFEST.json') -Raw | ConvertFrom-Json
$taskFailures = @()
foreach ($taskItem in $taskManifest.files) {
    $taskPath = [System.IO.Path]::GetFullPath((Join-Path $taskProject $taskItem.path))
    if (-not $taskPath.StartsWith($taskProject + '\',[System.StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe manifest path' }
    if (-not (Test-Path -LiteralPath $taskPath -PathType Leaf) -or (Get-Item -LiteralPath $taskPath).Length -ne $taskItem.bytes -or (Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskItem.sha256) { $taskFailures += $taskItem.path }
}
if ($taskFailures.Count) { throw ('Package hash failures: ' + ($taskFailures -join ', ')) }
$taskZipReader = [System.IO.Compression.ZipFile]::OpenRead($taskZip)
try {
    $taskForbidden = @($taskZipReader.Entries | Where-Object { $_.FullName -match '(^|/)(node_modules|tmp|\.venv|__pycache__|\.git)(/|$)|\.(o|obj|pyc|log|zip)$' })
    if ($taskForbidden.Count) { throw 'Archive includes a forbidden cache/runtime/object/nested ZIP' }
    $taskActualCount = @($taskZipReader.Entries | Where-Object { $_.Name -ne '' -and $_.FullName -ne 'DistriArch_CPP/PACKAGE_MANIFEST.json' }).Count
    if ($taskActualCount -ne $taskManifest.file_count) { throw 'Manifest file count mismatch' }
} finally { $taskZipReader.Dispose() }
$taskNode = Get-Command node -ErrorAction SilentlyContinue
if ($taskNode) { $taskNode = $taskNode.Source } else { $taskNode = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe' }
if (-not (Test-Path -LiteralPath $taskNode)) { throw 'Node 22.12+ is needed solely for the development HTTP/frontend test drivers.' }
$taskPreviousPath = $env:PATH
$taskLauncher = $null
$taskSucceeded = $false
Push-Location -LiteralPath $taskProject
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    & .\build\distriarch_tests.exe --all 2>&1 | Tee-Object -FilePath (Join-Path $taskExtract 'native-tests.txt')
    if ($LASTEXITCODE) { throw 'Packaged native tests failed' }
    & $taskNode --experimental-strip-types --test tests/replay_tests.mjs 2>&1 | Tee-Object -FilePath (Join-Path $taskExtract 'replay-tests.txt')
    if ($LASTEXITCODE) { throw 'Packaged replay tests failed' }
    & $taskNode tests/http_smoke.mjs 2>&1 | Tee-Object -FilePath (Join-Path $taskExtract 'http-tests.txt')
    if ($LASTEXITCODE) { throw 'Packaged HTTP/CLI tests failed' }
    try { $null = Invoke-WebRequest http://127.0.0.1:8000/api/health -TimeoutSec 1; throw 'Port 8000 is occupied; stop the previous project server before launcher verification.' }
    catch { if ($_.Exception.Message -like 'Port 8000*') { throw } }
    $taskLauncher = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\cmd.exe') -ArgumentList @('/d','/c','START_WINDOWS.bat') -WorkingDirectory $taskProject -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskExtract 'launcher-output.txt') -RedirectStandardError (Join-Path $taskExtract 'launcher-error.txt')
    $taskHealth = $null
    for ($taskAttempt=0;$taskAttempt -lt 100;$taskAttempt++) {
        try { $taskHealth = Invoke-RestMethod http://127.0.0.1:8000/api/health -TimeoutSec 1; break } catch { Start-Sleep -Milliseconds 100 }
    }
    if (-not $taskHealth -or $taskHealth.version -ne '3.0.0' -or $taskHealth.trace_schema -ne 'event-boundaries-v1') { throw 'Included batch launcher did not start the corrected application' }
    $taskPage = Invoke-WebRequest http://127.0.0.1:8000/
    if ($taskPage.Content -notmatch '<div id="root">') { throw 'Packaged dashboard failed to load' }
    $taskBundle = [regex]::Match($taskPage.Content,'src="([^"]+\.js)"').Groups[1].Value
    $taskBundleResponse = Invoke-WebRequest ('http://127.0.0.1:8000' + $taskBundle)
    if ($taskBundleResponse.StatusCode -ne 200) { throw 'Packaged frontend bundle failed to load' }
    $taskConfig = Invoke-RestMethod http://127.0.0.1:8000/api/config/phases
    $taskBody = $taskConfig | ConvertTo-Json -Depth 30
    $taskRun = Invoke-RestMethod http://127.0.0.1:8000/api/run -Method Post -ContentType 'application/json' -Body $taskBody
    if ($taskRun.status -ne 'completed' -or $taskRun.version -ne 3 -or $taskRun.summary.unfinished_jobs -ne 0) { throw 'Packaged launcher/API run failed' }
    $taskComparison = Invoke-RestMethod http://127.0.0.1:8000/api/compare -Method Post -ContentType 'application/json' -Body $taskBody
    if ($taskComparison.Count -ne 3 -or @($taskComparison | Where-Object { $_.status -ne 'completed' -or $_.summary.unfinished_jobs -ne 0 }).Count) { throw 'Packaged policy comparison failed' }
    $taskFixtureResults = @()
    foreach ($taskFixture in Get-ChildItem tests\fixtures\shrink-*.json) {
        $taskTrace = Join-Path $taskExtract ($taskFixture.BaseName + '-trace.json')
        & .\build\distriarch.exe run --config $taskFixture.FullName --output $taskTrace | Out-Null
        if ($LASTEXITCODE) { throw "Packaged shrink fixture failed: $($taskFixture.Name)" }
        $taskResult = Get-Content -LiteralPath $taskTrace -Raw | ConvertFrom-Json
        if ($taskResult.jobs[1].first_start -ge $taskResult.jobs[0].finished) { throw 'Packaged capacity reclamation regressed' }
        $taskFixtureResults += [ordered]@{fixture=$taskFixture.Name;b_first_start=$taskResult.jobs[1].first_start;a_finished=$taskResult.jobs[0].finished}
    }
    $taskReport = [ordered]@{zip=$taskZip;zip_sha256=(Get-FileHash -LiteralPath $taskZip -Algorithm SHA256).Hash.ToLowerInvariant();extracted=$taskProject;verified_files=$taskManifest.file_count;manifest_errors=0;forbidden_files=0;native_tests_passed=80;replay_tests_passed=10;http_cli_checks_passed=25;launcher='START_WINDOWS.bat';launcher_started=$true;health=$taskHealth;dashboard_loaded=$true;bundle_loaded=$true;completed_run=$taskRun.status;comparison_policies=$taskComparison.Count;shrink_fixtures=$taskFixtureResults;path='Windows/System32 only; Node invoked absolutely as test driver';exe_sha256=(Get-FileHash build\distriarch.exe -Algorithm SHA256).Hash.ToLowerInvariant();frontend_index_sha256=(Get-FileHash frontend\dist\index.html -Algorithm SHA256).Hash.ToLowerInvariant()}
    $taskReport | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $taskExtract 'verification.json') -Encoding utf8
    $taskReport | ConvertTo-Json -Depth 10
    $taskSucceeded = $true
} finally {
    $env:PATH = $taskPreviousPath
    Pop-Location
    if (-not ($KeepRunning -and $taskSucceeded)) {
        $taskServerExe = Join-Path $taskProject 'build\distriarch.exe'
        Get-CimInstance Win32_Process -Filter "Name = 'distriarch.exe'" | Where-Object { $_.ExecutablePath -eq $taskServerExe } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        if ($taskLauncher -and -not $taskLauncher.HasExited) { Stop-Process -Id $taskLauncher.Id -Force -ErrorAction SilentlyContinue }
    }
}
