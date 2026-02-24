param(
    [ValidateSet("2022", "2026")]
    [string]$Version = "2026",
    
    [switch]$Build
)

$ErrorActionPreference = "Stop"

# Configuration mapping
$versionConfig = @{
    "2022" = @{
        BuildDir  = "build/vs22"
        Generator = "Visual Studio 17 2022"
        VSVersion = "22"
        VersionRange = "[17.0,18.0)"
    }
    "2026" = @{
        BuildDir  = "build/vs26"
        Generator = "Visual Studio 18 2026"
        VSVersion = "26"
        VersionRange = "[18.0,19.0)"
    }
}

# VCPKG configuration
$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot) {
    $vcpkgRoot = "D:/vcpkg"
    Write-Host "VCPKG_ROOT not set. Using default: $vcpkgRoot"
}

$toolchain = "$vcpkgRoot/scripts/buildsystems/vcpkg.cmake"
$triplet = "x64-windows"

if (-not (Test-Path $toolchain)) {
    Write-Error "VCPKG toolchain not found at '$toolchain'. Please set VCPKG_ROOT environment variable."
    exit 1
}

$config = $versionConfig[$Version]
$buildDir = $config.BuildDir
$generator = $config.Generator
$vsVersion = $config.VSVersion

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Vulkan Project Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Visual Studio Version : $Version"
Write-Host "Build Directory       : $buildDir"
Write-Host "Generator             : $generator"
Write-Host "VCPKG Triplet         : $triplet"
Write-Host "VCPKG Root            : $vcpkgRoot"
Write-Host ""

# Generate CMake configuration if needed
Write-Host "Step 1: Configuring with CMake..." -ForegroundColor Yellow

if (Test-Path $buildDir) {
    Write-Host "  Cleaning existing build directory: $buildDir"
    Remove-Item -Recurse -Force $buildDir | Out-Null
}

$cmakeArgs = @(
    "-B", $buildDir,
    "-S", ".",
    "-G", $generator,
    "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_TARGET_TRIPLET=$triplet"
)

Write-Host "  Running: cmake $($cmakeArgs -join ' ')"
& cmake @cmakeArgs

if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed with exit code $LASTEXITCODE"
    exit 1
}

Write-Host "  ✓ CMake configuration completed successfully" -ForegroundColor Green
Write-Host ""

# Build if requested
if ($Build) {
    Write-Host "Step 2: Building project..." -ForegroundColor Yellow
    
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    
    $buildArgs = @(
        "--build", $buildDir,
        "--verbose"
    )
    
    Write-Host "  Running: cmake $($buildArgs -join ' ')"
    & cmake @buildArgs
    
    $stopwatch.Stop()
    $exitCode = $LASTEXITCODE
    
    if ($exitCode -ne 0) {
        Write-Error "Build failed with exit code $exitCode"
        exit 1
    }
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "BUILD REPORT" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    
    $duration = [math]::Round($stopwatch.Elapsed.TotalSeconds, 1)
    Write-Host "  Duration          : ${duration}s" -ForegroundColor Green
    
    # Find executable
    $exe = Get-ChildItem -Path $buildDir -Filter "vulkanwork.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($exe) {
        $sizeMB = [math]::Round($exe.Length / 1MB, 2)
        Write-Host "  Executable        : $($exe.FullName)" -ForegroundColor Green
        Write-Host "  Size              : ${sizeMB} MB" -ForegroundColor Green
    } else {
        Write-Warning "  Executable        : not found in build directory"
    }
    
    # Find solution file
    $sln = Get-ChildItem -Path $buildDir -Filter "vulkanwork.sln*" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($sln) {
        Write-Host "  Solution File     : $($sln.FullName)" -ForegroundColor Green
    }
    
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Build completed successfully!" -ForegroundColor Green
} else {
    Write-Host "Step 2: Build skipped (use -Build flag to compile)" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To build the project, run:" -ForegroundColor Cyan
    Write-Host "  .\build.ps1 -Version $Version -Build" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "CMake files have been generated in: $buildDir" -ForegroundColor Green
}

Write-Host "Completed!" -ForegroundColor Green
