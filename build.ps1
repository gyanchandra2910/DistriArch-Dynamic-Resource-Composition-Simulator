param([switch]$Clean, [switch]$TestsOnly)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $compiler) { throw 'G++ is required. Install MSYS2 UCRT64 or MinGW-w64 with GCC and zlib, and add its bin folder to PATH.' }
if ($Clean -and (Test-Path 'build')) {
    $target = (Resolve-Path 'build').Path
    if ($target -ne (Join-Path $PSScriptRoot 'build')) { throw 'Refusing to clean outside workspace/build' }
    Remove-Item -LiteralPath $target -Recurse -Force
}
New-Item -ItemType Directory -Path build -Force | Out-Null
$flags = @('-std=c++20','-O2','-g','-ffp-contract=off','-Wall','-Wextra','-Wpedantic','-Wno-misleading-indentation','-D_WIN32_WINNT=0x0A00','-Iinclude','-Ithird_party','-pthread')
$sources = @('model','engine','metrics','experiments')
if (-not $TestsOnly) { $sources += @('server','main') }
foreach ($name in $sources) {
    $source = "src/$name.cpp"
    $object = "build/$name.o"
    $headers = Get-ChildItem include/distriarch/*.hpp,third_party/*.h,third_party/*.hpp
    $newestHeader = ($headers | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
    if (-not (Test-Path $object) -or (Get-Item $source).LastWriteTime -gt (Get-Item $object).LastWriteTime -or $newestHeader -gt (Get-Item $object).LastWriteTime) {
        Write-Host "Compiling $source"
        & $compiler.Source @flags -c $source -o $object
        if ($LASTEXITCODE) { throw "Compilation failed: $source" }
    }
}
$core = @('build/model.o','build/engine.o','build/metrics.o','build/experiments.o')
if (-not $TestsOnly) {
    & $compiler.Source @flags @core build/server.o build/main.o -static -s -lz -lws2_32 -lcrypt32 -o build/distriarch.exe
    if ($LASTEXITCODE) { throw 'Executable link failed' }
}
if (Test-Path 'tests/native_tests.cpp') {
    & $compiler.Source @flags -c tests/native_tests.cpp -o build/native_tests.o
    if ($LASTEXITCODE) { throw 'Test compilation failed' }
    & $compiler.Source @flags @core build/native_tests.o -static -s -lz -o build/distriarch_tests.exe
    if ($LASTEXITCODE) { throw 'Test link failed' }
}
Write-Host 'Native C++20 build complete.'
