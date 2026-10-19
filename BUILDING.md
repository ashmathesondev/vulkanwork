# Building vulkanwork

## Prerequisites

- [CMake](https://cmake.org/) 3.20+
- [vcpkg](https://github.com/microsoft/vcpkg) — set the `VCPKG_ROOT` environment variable to your installation directory
- [GNU Make](https://www.gnu.org/software/make/)
- A C++20 compiler (MSVC, Clang, GCC)
- Vulkan-capable GPU and drivers

## Quick Start

```powershell
make build
make run
```

## What the Build Does

1. Installs dependencies via vcpkg (Vulkan, GLFW, GLM, Dear ImGui, stb, LZ4)
2. Configures the CMake project
3. Compiles shaders to SPIR-V
4. Packs shaders and textures into `assets.pak` (LZ4-compressed)
5. Builds the executable
6. Prints a build report with compiler, generator, duration, and executable location

## Makefile Targets

| Target | Description |
|---|---|
| `make build` | Configure (if needed) and build to `build/generic/` |
| `make build VS=22` | Build targeting Visual Studio 2022 (`build/vs22/`) |
| `make build VS=26` | Build targeting Visual Studio 2026 (`build/vs26/`) |
| `make run` | Build and run |
| `make clean` | Clean build artifacts |
| `make rebuild` | Clean then build |

All targets accept the optional `VS=22` or `VS=26` argument to use a Visual Studio generator.

## Build Report

Every build prints a summary report including:

- **Compiler** — full path to the C++ compiler used
- **Generator** — the CMake generator (e.g. Visual Studio 18 2026)
- **Duration** — wall-clock build time in seconds
- **Executable** — output path and file size in bytes

The report appears for `build`, `rebuild`, and `run` targets.

## Build Output Directories

```
build/
├── generic/    Default build output (no VS= specified)
├── vs22/       Visual Studio 2022 build output
└── vs26/       Visual Studio 2026 build output
```

## Configuring vcpkg Path

The Makefile requires the `VCPKG_ROOT` environment variable to be set. You can set it permanently in your shell profile, or pass it inline:

```powershell
$env:VCPKG_ROOT = "E:/vcpkg"   # PowerShell (current session)
make build
```

```powershell
make build VCPKG_ROOT=E:/vcpkg  # inline override
```

## Opening in Visual Studio

```powershell
.\startvs.ps1            # Auto-detect the best installed VS
.\startvs.ps1 -t 2026   # Visual Studio 2026
.\startvs.ps1 -t 2022   # Visual Studio 2022
```

This will generate the solution if needed (or regenerate if files are missing) and launch VS. When multiple editions are installed, the script prefers Enterprise > Professional > Community.

## Android

Android builds use the Gradle project under `android/app` and the same top-level CMake project. Requirements:

- Android SDK with platform 35
- Android NDK
- Gradle or Android Studio
- `VCPKG_ROOT` set to a vcpkg checkout with Android triplet support
- `glslc` available from a desktop Vulkan SDK, or `Vulkan_GLSLC_EXECUTABLE` set for CMake

Build and install a debug APK:

```powershell
gradle :app:assembleDebug
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

The Android target builds `libvulkanwork.so` as a `NativeActivity`, stages `assets.pak` plus the default `DamagedHelmet.glb` into APK assets, then copies those files to app-internal storage on startup so existing filesystem loaders can run unchanged.
