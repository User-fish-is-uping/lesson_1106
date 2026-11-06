param(
    [string]$HostToolchain = (Join-Path $PSScriptRoot '..\..\..\K210-dev\host-toolchain\w64devkit\bin'),
    [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    [string]$Python = 'C:\Users\15846\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
)
$ErrorActionPreference = 'Stop'
$realRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$courseRoot = Split-Path $realRoot
$HostToolchain = (Resolve-Path -LiteralPath $HostToolchain).Path
# GNU Make needs an ASCII path even though GCC itself accepts the Unicode path.
if (!(Test-Path 'L:\')) {
    & subst.exe L: $courseRoot
    if ($LASTEXITCODE -ne 0) { throw 'Unable to map L: for host tests' }
}
$markerName = '.greeter-host-map-' + [guid]::NewGuid().ToString('N')
$marker = Join-Path $courseRoot $markerName
try {
    $markerName | Set-Content -LiteralPath $marker -Encoding ascii
    $mapped = Join-Path 'L:\' $markerName
    if (!(Test-Path -LiteralPath $mapped) -or (Get-Content -LiteralPath $mapped -Raw).Trim() -ne $markerName) {
        throw 'L: belongs to another directory; existing mapping left unchanged'
    }
} finally { Remove-Item -LiteralPath $marker -ErrorAction SilentlyContinue }
$root = 'L:/' + [IO.Path]::GetRelativePath($courseRoot, $realRoot).Replace('\','/')
$HostToolchain = 'L:/' + [IO.Path]::GetRelativePath($courseRoot, $HostToolchain).Replace('\','/')
$env:PATH = "$HostToolchain;" + $env:PATH
$gcc = (Join-Path $HostToolchain 'gcc.exe').Replace('\','/')
$make = (Join-Path $HostToolchain 'mingw32-make.exe').Replace('\','/')
$ctest = Join-Path (Split-Path $CMake) 'ctest.exe'
$build = Join-Path $root 'build\host'
$logDir = Join-Path $root 'build\logs'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$log = Join-Path $logDir 'host-counter-tests.log'
& $CMake --fresh -S $root -B $build -G 'MinGW Makefiles' "-DCMAKE_C_COMPILER=$gcc" "-DCMAKE_MAKE_PROGRAM=$make" "-DPython3_EXECUTABLE=$Python" 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw 'Host configure failed' }
& $CMake --build $build --parallel 4 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw 'Host build failed' }
& $ctest --test-dir $build --output-on-failure 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw 'Core tests failed' }
$dll = Get-ChildItem -LiteralPath $build -Filter '*greeter_replay.dll' | Select-Object -First 1
if (!$dll) { throw 'Replay DLL missing' }
$adapterBuild = Join-Path $root 'build\firmware-tests'
& $CMake --fresh -S (Join-Path $root 'tools\windows\firmware-tests') -B $adapterBuild -G 'MinGW Makefiles' "-DCMAKE_C_COMPILER=$gcc" "-DCMAKE_MAKE_PROGRAM=$make" "-DPython3_EXECUTABLE=$Python" "-DREPLAY_LIBRARY=$($dll.FullName.Replace('\','/'))" 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw 'Adapter configure failed' }
& $CMake --build $adapterBuild --parallel 4 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw 'Adapter build failed' }
& $ctest --test-dir $adapterBuild --output-on-failure 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw 'Adapter tests failed' }
