param(
    [ValidateSet('vision-check','vision-count')][string]$Target = 'vision-check',
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
$projectName = if ($Target -eq 'vision-count') { 'greeter_count' } else { 'greeter_vision' }
$version = if ($Target -eq 'vision-count') { 'greeter-count-v0.2' } else { 'greeter-vision-check-v0.2' }
$sourceCommit = (git -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Source must be in the shared Git checkout' }
$dirty = [bool](git -C $projectRoot status --porcelain --untracked-files=normal)
$coreCommit = git -C $projectRoot log -1 --format=%H -- core
if (!$coreCommit) { $coreCommit = git -C $projectRoot rev-parse --verify MERGE_HEAD }
if (!$coreCommit) { throw 'Cannot identify the imported core revision' }
$coreCommit = $coreCommit.Trim()
foreach ($required in @($CMake, $model, (Join-Path $sdkOriginal 'CMakeLists.txt'), (Join-Path $devRoot 'kendryte-toolchain\bin\riscv64-unknown-elf-gcc.exe'))) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing dependency: $required" }
}
# SDK source is staged separately; never overwrite the course SDK or other projects.
New-Item -ItemType Directory -Path $sdkStage -Force | Out-Null
foreach ($name in @('cmake','lib','lds','third_party','CMakeLists.txt','LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $sdkOriginal $name) -Destination $sdkStage -Recurse -Force
}
$stageSource = Join-Path $sdkStage "src\$projectName"
# Only this generated target is cleaned, after checking its absolute containment.
$expectedParent = [IO.Path]::GetFullPath((Join-Path $sdkStage 'src')) + '\'
if (![IO.Path]::GetFullPath($stageSource).StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Unsafe staging directory'
}
if (Test-Path -LiteralPath $stageSource) { Remove-Item -LiteralPath $stageSource -Recurse -Force }
New-Item -ItemType Directory -Path $stageSource -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $projectRoot 'firmware\vision_check') | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $stageSource -Recurse -Force
}
Copy-Item -LiteralPath $model -Destination (Join-Path $stageSource 'detect.kmodel') -Force
if ($Target -eq 'vision-count') {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'core') -Destination $stageSource -Recurse -Force
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'firmware\vision_count') -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $stageSource -Force
    }
}
$header = "#define FIRMWARE_VERSION `"$version`"`n#define FIRMWARE_SOURCE_COMMIT `"$sourceCommit`"`n#define FIRMWARE_CORE_COMMIT `"$coreCommit`"`n#define FIRMWARE_SOURCE_DIRTY $([int]$dirty)`n"
$header | Set-Content -LiteralPath (Join-Path $stageSource 'build_info.h') -Encoding ascii
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
$build = "K:/greeter-sdk/build-$projectName"
# Old GCC subprocesses need companion DLLs from bin. This changes this process only.
$env:PATH = 'K:\kendryte-toolchain\bin;' + $env:PATH
$logDir = Join-Path $projectRoot 'build\logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$log = Join-Path $logDir "$Target-build.log"
& $CMake -S 'K:/greeter-sdk' -B $build -G 'MinGW Makefiles' "-DPROJ=$projectName" '-DTOOLCHAIN=K:/kendryte-toolchain/bin' 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed: $log" }
& $CMake --build $build --parallel 4 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $log" }
$dist = Join-Path $projectRoot "dist\$Target"
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$bin = Join-Path $dist "$version.bin"
Copy-Item -LiteralPath (Join-Path $sdkStage "build-$projectName\$projectName.bin") -Destination $bin -Force
$hash = (Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash
"$hash  $version.bin" | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii
$configuration = $null
if ($Target -eq 'vision-count') {
    $configPath = Join-Path $projectRoot 'firmware\vision_count\config.json'
    Copy-Item -LiteralPath $configPath -Destination (Join-Path $dist 'config.json') -Force
    $configuration = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
}
[ordered]@{
    firmware=$version; source_commit=$sourceCommit; source_dirty=$dirty; core_commit=$coreCommit
    binary_sha256=$hash; binary_bytes=(Get-Item -LiteralPath $bin).Length
    model_sha256=(Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash
    config=$configuration; audio_enabled=$false; audio_muted=$true; audio_max_wait_ms=5000
    baud=115200; region_nms=0.2; log_float_format='%.9g'; hardware_tested=$false
    compiled_at_utc=[DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $dist 'manifest.json') -Encoding utf8
Write-Output "Built: $bin"
Write-Output "SHA256: $hash"
