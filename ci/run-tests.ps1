#Requires -Version 7.0
<#
.SYNOPSIS
  Configure, build and run ALL tests under tests/ with a chosen Windows compiler.

.DESCRIPTION
  Drives CMake (Ninja) for msvc or clang-cl. Every test target registered in
  tests/CMakeLists.txt is built and executed via ctest.

.PARAMETER Compiler
  msvc (cl.exe) or clang-cl (clang-cl.exe). Must run inside a Visual Studio
  developer environment (INCLUDE/LIB/PATH set), e.g. after launching the
  "x64 Native Tools" prompt or `ilammy/msvc-dev-cmd` in CI.

.PARAMETER CxxStandard
  C++ standard passed as CMAKE_CXX_STANDARD (e.g. 23, 26).

.PARAMETER Defines
  Preprocessor macros, comma/semicolon separated, e.g. "FOO=1;BAR".

.PARAMETER ExtraCxxFlags
  Raw extra compiler flags appended to CMAKE_CXX_FLAGS.

.PARAMETER Config
  Build type: Debug, Release, RelWithDebInfo, MinSizeRel.

.PARAMETER BuildDir
  Override the build directory (default out/ci/<compiler>-cxx<std>-<config>).

.PARAMETER Filter
  ctest -R regex to run a subset (default: all).

.PARAMETER Jobs
  Parallel build/test jobs.

.EXAMPLE
  ./ci/run-tests.ps1 -Compiler msvc -CxxStandard 23
.EXAMPLE
  ./ci/run-tests.ps1 -Compiler clang-cl -CxxStandard 26 -Defines "FOO=1;BAR"
#>
[CmdletBinding()]
param(
  [ValidateSet('msvc', 'clang-cl')][string]$Compiler = 'msvc',
  [string]$CxxStandard = '23',
  [string]$Defines = '',
  [string]$ExtraCxxFlags = '',
  [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')][string]$Config = 'Debug',
  [string]$BuildDir = '',
  [int]$Jobs = 8,
  [string]$Filter = '',
  [switch]$ConfigureOnly,
  [switch]$SkipRun
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $BuildDir) {
  $BuildDir = Join-Path $repo ("out/ci/{0}-cxx{1}-{2}" -f $Compiler, $CxxStandard, $Config)
}

$cCompiler = switch ($Compiler) { 'msvc' { 'cl.exe' } 'clang-cl' { 'clang-cl.exe' } }

# Translate NAME[=VAL] macros to the MSVC/clang-cl '/D' form.
$defFlags = ''
if ($Defines) {
  $defFlags = (($Defines -split '[;,]') | Where-Object { $_.Trim() } | ForEach-Object { '/D' + $_.Trim() }) -join ' '
}
$cxxFlags = (@($defFlags, $ExtraCxxFlags) | Where-Object { $_ }) -join ' '

$env:BUILD_MODE = $Config.ToLowerInvariant()

$cmakeArgs = @(
  '-S', $repo, '-B', $BuildDir, '-G', 'Ninja',
  "-DCMAKE_BUILD_TYPE=$Config",
  "-DCMAKE_C_COMPILER=$cCompiler",
  "-DCMAKE_CXX_COMPILER=$cCompiler",
  '-DCMAKE_C_STANDARD=23', '-DCMAKE_C_STANDARD_REQUIRED=ON', '-DCMAKE_C_EXTENSIONS=OFF',
  "-DCMAKE_CXX_STANDARD=$CxxStandard", '-DCMAKE_CXX_STANDARD_REQUIRED=ON', '-DCMAKE_CXX_EXTENSIONS=OFF',
  '-DMSVC_RUNTIME_MODE=MD',
  '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
  '-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON',
  '-DBUILD_TESTING=ON'
)
$cmakeArgs += "-DCMAKE_CXX_FLAGS=$cxxFlags"   # always set (empty resets any cached flags)

Write-Host "== configure: $Compiler, C++$CxxStandard, $Config ==" -ForegroundColor Cyan
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed ($LASTEXITCODE)"; exit $LASTEXITCODE }
if ($ConfigureOnly) { Write-Host "configure-only requested; done." -ForegroundColor Yellow; exit 0 }

Write-Host "== build all tests ==" -ForegroundColor Cyan
& cmake --build $BuildDir --config $Config -j $Jobs
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed ($LASTEXITCODE)"; exit $LASTEXITCODE }
if ($SkipRun) { Write-Host "skip-run requested; done." -ForegroundColor Yellow; exit 0 }

Write-Host "== ctest ==" -ForegroundColor Cyan
$ctestArgs = @('--test-dir', $BuildDir, '--build-config', $Config, '--output-on-failure', '-j', "$Jobs")
if ($Filter) { $ctestArgs += @('-R', $Filter) }
& ctest @ctestArgs
exit $LASTEXITCODE
