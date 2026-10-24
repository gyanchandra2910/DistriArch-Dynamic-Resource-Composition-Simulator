param([string]$Output = 'DistriArch_CPP_Complete_Fixed.zip')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskRoot = $PSScriptRoot
$taskOutput = [System.IO.Path]::GetFullPath((Join-Path $taskRoot $Output))
if (-not $taskOutput.StartsWith($taskRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase) -or [System.IO.Path]::GetExtension($taskOutput) -ne '.zip') {
    throw 'The ZIP output must be inside this project and end in .zip.'
}
if ($taskOutput -eq (Join-Path $taskRoot 'DistriArch_CPP_Complete.zip')) { throw 'The original submission ZIP must remain unchanged. Choose a new output filename.' }
$taskStageParent = Join-Path $taskRoot ('tmp\package-' + [guid]::NewGuid().ToString('N'))
$taskStage = Join-Path $taskStageParent 'DistriArch_CPP'
New-Item -ItemType Directory -Path $taskStage -Force | Out-Null
$taskPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$taskRootFiles = @('.gitignore','.gitattributes','README.md','QA.md','THIRD_PARTY.md','START_HERE.txt','START_WINDOWS.bat','RUN_TESTS_WINDOWS.bat','ProjectStatements.pdf','CMakeLists.txt','build.ps1','launch.ps1','verify.ps1','package.ps1','dependency-lock.json')
foreach ($taskFile in $taskRootFiles) { [void]$taskPaths.Add($taskFile) }
foreach ($taskDir in @('src','include','tests','configs','docs','evidence','third_party','results','frontend\src','frontend\public','frontend\dist')) {
    foreach ($taskFile in Get-ChildItem -LiteralPath (Join-Path $taskRoot $taskDir) -File -Recurse) {
        [void]$taskPaths.Add($taskFile.FullName.Substring($taskRoot.Length + 1))
    }
}
foreach ($taskFile in @('frontend\index.html','frontend\package.json','frontend\pnpm-lock.yaml','frontend\pnpm-workspace.yaml','frontend\tsconfig.json','frontend\vite.config.ts','build\distriarch.exe','build\distriarch_tests.exe')) { [void]$taskPaths.Add($taskFile) }
$taskManifestFiles = @()
foreach ($taskRelative in ($taskPaths | Sort-Object)) {
    $taskSource = Join-Path $taskRoot $taskRelative
    if (-not (Test-Path -LiteralPath $taskSource -PathType Leaf)) { throw "Required package file missing: $taskRelative" }
    $taskDestination = Join-Path $taskStage $taskRelative
    New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($taskDestination)) -Force | Out-Null
    Copy-Item -LiteralPath $taskSource -Destination $taskDestination
    $taskManifestFiles += [ordered]@{path=$taskRelative.Replace('\','/');bytes=(Get-Item -LiteralPath $taskDestination).Length;sha256=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash.ToLowerInvariant()}
}
[ordered]@{project='DistriArch';implementation='C++20';version='3.0.0';trace_schema='event-boundaries-v1';ready_to_run='Windows 10/11 x64';source_included=$true;python_required=$false;node_required_to_run=$false;file_count=$taskManifestFiles.Count;manifest_excludes_itself=$true;files=$taskManifestFiles} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskStage 'PACKAGE_MANIFEST.json') -Encoding utf8
$taskOutputParent = [System.IO.Path]::GetDirectoryName($taskOutput)
New-Item -ItemType Directory -Path $taskOutputParent -Force | Out-Null
$taskTemporaryZip = Join-Path $taskRoot ('tmp\archive-' + [guid]::NewGuid().ToString('N') + '.zip')
[System.IO.Compression.ZipFile]::CreateFromDirectory($taskStageParent,$taskTemporaryZip,[System.IO.Compression.CompressionLevel]::Optimal,$false)
Move-Item -LiteralPath $taskTemporaryZip -Destination $taskOutput -Force
$taskArchive = [System.IO.Compression.ZipFile]::OpenRead($taskOutput)
try { $taskEntryCount = $taskArchive.Entries.Count } finally { $taskArchive.Dispose() }
[ordered]@{zip=$taskOutput;bytes=(Get-Item -LiteralPath $taskOutput).Length;entries=$taskEntryCount;sha256=(Get-FileHash -LiteralPath $taskOutput -Algorithm SHA256).Hash.ToLowerInvariant()} | ConvertTo-Json
