# SPDX-License-Identifier: MIT
# Copyright (c) 2026 SunnySlopes
#
# Builds NoSlimeFinderBase without CMake, using MSYS2 UCRT64/MinGW GCC.
#
# Outputs:
#   build/lib/libnoslimefinderbase.a    static library (GUI JNI shim links it)
#   build/bin/NoSlimeFinderBase-cli.exe standalone CLI
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\build_msys2.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\build_msys2.ps1 -Clean
#
# Env:
#   MSYS2_ROOT   default C:\msys64
#   MSYS2_MINGW  ucr64 | mingw64 (auto-detected when omitted)

param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "build"
$ObjDir = Join-Path $Build "obj"
$LibDir = Join-Path $Build "lib"
$BinDir = Join-Path $Build "bin"

$msysRoot = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }
$searchDirs = @()
if ($env:MSYS2_MINGW) {
    $searchDirs += Join-Path $msysRoot "$($env:MSYS2_MINGW)\bin"
}
$searchDirs += (Join-Path $msysRoot "ucrt64\bin")
$searchDirs += (Join-Path $msysRoot "mingw64\bin")

$toolDir = $null
foreach ($dir in $searchDirs) {
    if (Test-Path (Join-Path $dir "g++.exe")) {
        $toolDir = $dir
        break
    }
}
if (-not $toolDir) {
    throw "MSYS2 MinGW toolchain not found (tried: $($searchDirs -join ', ')). Set MSYS2_ROOT or MSYS2_MINGW."
}

$env:PATH = "$toolDir;$env:PATH"
$gcc = Join-Path $toolDir "gcc.exe"
$gxx = Join-Path $toolDir "g++.exe"
$ar = Join-Path $toolDir "ar.exe"

$cubiomes = Join-Path $Root "cubiomes"
$optimizer = Join-Path $Root "slimeoptimizer"
$preCubiomes = Join-Path $Root "cxx_compat\pre_cubiomes.h"

foreach ($check in @(
        (Join-Path $cubiomes "generator.c"),
        (Join-Path $cubiomes "biomes.h"),
        (Join-Path $optimizer "include\geometry.hpp"),
        (Join-Path $optimizer "src\slime_check.cpp"),
        (Join-Path $Root "src\noslime_search.cpp"),
        (Join-Path $Root "src\biomes.cpp"),
        (Join-Path $Root "src\cli\main.cpp"),
        (Join-Path $Root "include\noslimefinder\base.h"))) {
    if (-not (Test-Path $check)) {
        throw "Missing source: $check`nIf cubiomes/slimeoptimizer are empty run: git submodule update --init --recursive"
    }
}

if ($Clean) {
    foreach ($dir in @($ObjDir, $LibDir, $BinDir)) {
        if (Test-Path $dir) {
            Remove-Item -LiteralPath $dir -Recurse -Force
        }
    }
}

foreach ($dir in @($ObjDir, $LibDir, $BinDir,
        (Join-Path $ObjDir "cubiomes"), (Join-Path $ObjDir "optimizer"),
        (Join-Path $ObjDir "core"), (Join-Path $ObjDir "cli"))) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

function Invoke-Compiler {
    param(
        [string]$Compiler,
        [string[]]$Arguments,
        [string]$Description
    )
    Write-Host "[base] $Description"
    & $Compiler @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed (exit code $LASTEXITCODE)"
    }
}

$cFlags = @("-std=c17", "-O3", "-DNDEBUG", "-I$Root")
$cxxFlags = @("-std=c++20", "-O3", "-DNDEBUG", "-include", $preCubiomes,
    "-I$Root", "-I$(Join-Path $Root 'include')",
    "-I$(Join-Path $optimizer 'include')")

$cubiomesSources = @("generator.c", "layers.c", "biomenoise.c", "biomes.c",
    "noise.c", "util.c")
$optimizerSources = @("geometry.cpp", "slime_check.cpp", "java_random.cpp")

$objects = New-Object System.Collections.Generic.List[string]

foreach ($src in $cubiomesSources) {
    $srcPath = Join-Path $cubiomes $src
    $objPath = Join-Path (Join-Path $ObjDir "cubiomes") ($src + ".o")
    Invoke-Compiler $gcc ($cFlags + @("-c", $srcPath, "-o", $objPath)) "cubiomes/$src"
    $objects.Add($objPath)
}

foreach ($src in $optimizerSources) {
    $srcPath = Join-Path (Join-Path $optimizer "src") $src
    $objPath = Join-Path (Join-Path $ObjDir "optimizer") ($src + ".o")
    Invoke-Compiler $gxx ($cxxFlags + @("-c", $srcPath, "-o", $objPath)) "slimeoptimizer/$src"
    $objects.Add($objPath)
}

foreach ($src in @("noslime_search.cpp", "biomes.cpp")) {
    $srcPath = Join-Path (Join-Path $Root "src") $src
    $objPath = Join-Path (Join-Path $ObjDir "core") ($src + ".o")
    Invoke-Compiler $gxx ($cxxFlags + @("-c", $srcPath, "-o", $objPath)) "core/$src"
    $objects.Add($objPath)
}

$libPath = Join-Path $LibDir "libnoslimefinderbase.a"
if (Test-Path $libPath) {
    Remove-Item -LiteralPath $libPath -Force
}
Write-Host "[base] archiving libnoslimefinderbase.a"
& $ar @(@("rcs", $libPath) + $objects.ToArray())
if ($LASTEXITCODE -ne 0) {
    throw "ar failed (exit code $LASTEXITCODE)"
}

$cliObj = Join-Path (Join-Path $ObjDir "cli") "main.cpp.o"
$cliFlags = @("-std=c++20", "-O3", "-DNDEBUG", "-I$Root",
    "-I$(Join-Path $Root 'include')")
Invoke-Compiler $gxx ($cliFlags + @("-c", (Join-Path $Root "src\cli\main.cpp"), "-o", $cliObj)) "cli/main.cpp"

$exePath = Join-Path $BinDir "NoSlimeFinderBase-cli.exe"
Write-Host "[base] linking NoSlimeFinderBase-cli.exe"
& $gxx @($cliObj, "-o", $exePath, "-L$LibDir", "-lnoslimefinderbase",
    "-static", "-static-libgcc", "-static-libstdc++", "-pthread")
if ($LASTEXITCODE -ne 0) {
    throw "Linking NoSlimeFinderBase-cli.exe failed (exit code $LASTEXITCODE)"
}

$licenseTarget = Join-Path $BinDir "LICENSE.txt"
Copy-Item -LiteralPath (Join-Path $Root "LICENSE") -Destination $licenseTarget -Force

Write-Host ""
Write-Host "[base] done"
Write-Host "  static library : $libPath"
Write-Host "  CLI executable : $exePath"
Write-Host "  license        : $licenseTarget"
Write-Host "  toolchain      : $toolDir"
