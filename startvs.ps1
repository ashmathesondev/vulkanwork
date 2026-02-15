param(
    [ValidateSet("2022", "2026")]
    [string]$t
)

$ErrorActionPreference = "Stop"

# Version configuration: folder, VS= value for make, vswhere version range
$versions = @{
    "2022" = @{ Folder = "build/vs22"; MakeVS = "22"; VersionRange = "[17.0,18.0)" }
    "2026" = @{ Folder = "build/vs26"; MakeVS = "26"; VersionRange = "[18.0,19.0)" }
}

# Locate vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found at '$vswhere'. Is Visual Studio installed?"
    exit 1
}

# Products in priority order (prefer Professional over Community)
$productPriority = @(
    "Microsoft.VisualStudio.Product.Enterprise",
    "Microsoft.VisualStudio.Product.Professional",
    "Microsoft.VisualStudio.Product.Community"
)

function Find-VS($versionArg) {
    foreach ($product in $productPriority) {
        $vsArgs = @("-products", $product, "-requires", "Microsoft.Component.MSBuild", "-latest", "-prerelease")
        if ($versionArg) { $vsArgs = @("-version", $versionArg) + $vsArgs }

        $path = & $vswhere @vsArgs -property productPath
        if ($path) {
            $name = & $vswhere @vsArgs -property displayName
            $ver  = & $vswhere @vsArgs -property installationVersion
            return @{ DevEnv = $path; DisplayName = $name; Version = $ver }
        }
    }
    return $null
}

if (-not $t) {
    # Auto-detect the best installed VS
    $vs = Find-VS $null
    if (-not $vs) {
        Write-Error "No Visual Studio installation found."
        exit 1
    }

    $major = ($vs.Version -split '\.')[0]
    switch ($major) {
        "17" { $t = "2022" }
        "18" { $t = "2026" }
        default { Write-Error "Detected VS major version $major but no matching configuration."; exit 1 }
    }

    Write-Host "Auto-detected $($vs.DisplayName)"
} else {
    $config = $versions[$t]
    $vs = Find-VS $config.VersionRange
    if (-not $vs) {
        Write-Error "Visual Studio $t is not installed."
        exit 1
    }

    Write-Host "Found $($vs.DisplayName)"
}

$devenv = $vs.DevEnv
$config = $versions[$t]
$folder = $config.Folder
$makeVS = $config.MakeVS

Write-Host "Using: $devenv"

# Check for solution (.sln or .slnx) and .vcxproj in the build folder
$sln = Get-ChildItem -Path $folder -Filter "vulkanwork.sln*" -ErrorAction SilentlyContinue | Select-Object -First 1
$vcxproj = Get-ChildItem -Path $folder -Filter "*.vcxproj" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not $sln -or -not $vcxproj) {
    Write-Host "Solution files not found in '$folder'. Generating..."

    if (Test-Path $folder) {
        Write-Host "Removing existing '$folder' directory..."
        Remove-Item -Recurse -Force $folder
    }

    make build "VS=$makeVS"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to generate Visual Studio $t project (make build VS=$makeVS)."
        exit 1
    }

    $sln = Get-ChildItem -Path $folder -Filter "vulkanwork.sln*" -ErrorAction SilentlyContinue | Select-Object -First 1
}

$slnPath = $sln.FullName
Write-Host "Opening $slnPath in Visual Studio $t..."
& $devenv $slnPath
