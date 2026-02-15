param(
    [Parameter(Mandatory)]
    [ValidateSet("2022", "2026")]
    [string]$t
)

$ErrorActionPreference = "Stop"

# Version configuration: folder, make target, vswhere version range
$versions = @{
    "2022" = @{ Folder = "vs22"; Target = "vs22"; VersionRange = "[17.0,18.0)" }
    "2026" = @{ Folder = "vs26"; Target = "vs26"; VersionRange = "[18.0,19.0)" }
}

$config = $versions[$t]
$folder = $config.Folder
$target = $config.Target
$versionRange = $config.VersionRange

# Locate vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found at '$vswhere'. Is Visual Studio installed?"
    exit 1
}

# Find VS Professional devenv.exe
$devenv = & $vswhere -version $versionRange `
    -products Microsoft.VisualStudio.Product.Professional `
    -requires Microsoft.Component.MSBuild `
    -property productPath `
    -latest
if (-not $devenv) {
    Write-Error "Visual Studio $t Professional is not installed."
    exit 1
}

Write-Host "Found Visual Studio $t at: $devenv"

# Check for .sln and .vcxproj in the build folder
$sln = Join-Path $folder "vulkanwork.sln"
$vcxproj = Get-ChildItem -Path $folder -Filter "*.vcxproj" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not (Test-Path $sln) -or -not $vcxproj) {
    Write-Host "Solution files not found in '$folder'. Generating..."

    if (Test-Path $folder) {
        Write-Host "Removing existing '$folder' directory..."
        Remove-Item -Recurse -Force $folder
    }

    make $target
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to generate Visual Studio $t project (make $target)."
        exit 1
    }
}

Write-Host "Opening $sln in Visual Studio $t..."
& $devenv $sln
