param(
    [string]$CourseRoot = (Join-Path $PSScriptRoot '..\..\..'),
    [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
)
$ErrorActionPreference = 'Stop'
$CourseRoot = (Resolve-Path -LiteralPath $CourseRoot).Path
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$devRoot = Join-Path $CourseRoot 'K210-dev'
$sdkOriginal = Join-Path $devRoot 'kendryte-standalone-sdk-develop'
$sdkStage = Join-Path $devRoot 'greeter-sdk'
$model = Join-Path $CourseRoot '高级实验例程2\7.物体检测\代码\object_detection\detect.kmodel'
foreach ($required in @($CMake, $model, (Join-Path $sdkOriginal 'CMakeLists.txt'), (Join-Path $devRoot 'kendryte-toolchain\bin\riscv64-unknown-elf-gcc.exe'))) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing dependency: $required" }
}
# SDK source is staged separately; never overwrite the course SDK or other projects.
New-Item -ItemType Directory -Path $sdkStage -Force | Out-Null
foreach ($name in @('cmake','lib','lds','third_party','CMakeLists.txt','LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $sdkOriginal $name) -Destination $sdkStage -Recurse -Force
}
$stageSource = Join-Path $sdkStage 'src\greeter_vision'
New-Item -ItemType Directory -Path $stageSource -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $projectRoot 'firmware\vision_check') | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $stageSource -Recurse -Force
}
Copy-Item -LiteralPath $model -Destination (Join-Path $stageSource 'detect.kmodel') -Force
# Existing environment uses K: to avoid non-ASCII paths in this older toolchain.
if (!(Test-Path 'K:\')) {
    & subst.exe K: $devRoot
    if ($LASTEXITCODE -ne 0) { throw 'Unable to map K: to K210-dev' }
}
# subst's localized output can corrupt Unicode. Verify via a unique marker instead.
$markerName = '.greeter-map-check-' + [guid]::NewGuid().ToString('N')
$markerPath = Join-Path $devRoot $markerName
try {
    $markerName | Set-Content -LiteralPath $markerPath -Encoding ascii
    $mappedMarker = Join-Path 'K:\' $markerName
    if (!(Test-Path -LiteralPath $mappedMarker) -or
        (Get-Content -LiteralPath $mappedMarker -Raw).Trim() -ne $markerName) {
        throw 'K: is not the expected K210-dev mapping; existing drive left unchanged.'
    }
} finally {
    Remove-Item -LiteralPath $markerPath -ErrorAction SilentlyContinue
}
$build = 'K:/greeter-sdk/build-greeter_vision'
# Old GCC subprocesses need companion DLLs from bin. This changes this process only.
$env:PATH = 'K:\kendryte-toolchain\bin;' + $env:PATH
$logDir = Join-Path $projectRoot 'build\logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$log = Join-Path $logDir 'vision-build.log'
& $CMake -S 'K:/greeter-sdk' -B $build -G 'MinGW Makefiles' '-DPROJ=greeter_vision' '-DTOOLCHAIN=K:/kendryte-toolchain/bin' 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed: $log" }
& $CMake --build $build --parallel 4 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $log" }
$dist = Join-Path $projectRoot 'dist\vision-check'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$bin = Join-Path $dist 'greeter-vision-check-v0.1.bin'
Copy-Item -LiteralPath (Join-Path $sdkStage 'build-greeter_vision\greeter_vision.bin') -Destination $bin -Force
$hash = (Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash
"$hash  greeter-vision-check-v0.1.bin" | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii
Write-Output "Built: $bin"
Write-Output "SHA256: $hash"
