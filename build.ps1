[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $repoRoot 'build'

# Resolve cmake.exe — prefer PATH, fall back to the copy bundled with Visual Studio.
$cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsRoot = & $vswhere -latest -property installationPath
        $cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
    if (-not $cmake -or -not (Test-Path $cmake)) {
        throw 'cmake.exe not found. Install CMake (https://cmake.org/download) or add it to PATH.'
    }
}
Write-Host "Using cmake: $cmake" -ForegroundColor DarkGray

if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}

if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Write-Host "Configuring CMake ($Platform)..." -ForegroundColor Cyan
    & $cmake -S $repoRoot -B $buildDir -A $Platform
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
}

Write-Host "Building ($Configuration)..." -ForegroundColor Cyan
& $cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$artifact = Get-ChildItem -Recurse -Path $buildDir -Filter 'reaper_ReapletonLink.*' |
    Where-Object { $_.Extension -in '.dll', '.dylib', '.so' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if ($artifact) {
    Write-Host "`nBuild succeeded: $($artifact.FullName)" -ForegroundColor Green
} else {
    Write-Host "`nBuild completed but no output binary was found." -ForegroundColor Yellow
}