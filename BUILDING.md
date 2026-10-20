# Building vulkanwork

## Prerequisites

- [CMake](https://cmake.org/) 3.21+
- [vcpkg](https://github.com/microsoft/vcpkg) — set the `VCPKG_ROOT` environment variable to your installation directory
- [GNU Make](https://www.gnu.org/software/make/)
- A C++20 compiler: MSVC or Clang on Windows, GCC or Clang on Linux
- Windows Clang builds need [LLVM](https://llvm.org/) `clang-cl` on `PATH` and Ninja. The VS-bundled Ninja is found automatically. `VS=22`/`VS=26` Clang builds need the "C++ Clang tools for Windows" VS component instead
- Vulkan-capable GPU and drivers
- Linux (Ubuntu/Debian) system packages. vcpkg needs the X11 headers for GLFW, and autotools to build the X11/xcb ports from source:

  ```sh
  sudo apt install build-essential pkg-config \
      libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
      autoconf autoconf-archive automake libtool
  ```

  Install the Vulkan SDK (or `libvulkan-dev` plus `glslc`) as well.

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
| `make build` | Configure (if needed) and build |
| `make run` | Build and run |
| `make clean` | Remove the build, package and `dist/` directories for the selected options |
| `make rebuild` | Clean then build |
| `make package` | Build a relocatable Release archive into `dist/` (ZIP on Windows, TGZ on Linux) |
| `make help` | Show targets and the resolved options |

Options, accepted by every target:

| Option | Values | Default |
|---|---|---|
| `COMPILER` | Windows: `msvc`, `clang`. Linux: `gcc`, `clang` | `msvc` / `gcc` |
| `CONFIG` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` | `Debug` |
| `VS` | `22`, `26` (Windows only, selects a Visual Studio generator) | default generator |
| `TRIPLET` | any vcpkg triplet | `x64-windows` / `x64-linux` |
| `NINJA` | path to `ninja.exe` (Windows `COMPILER=clang` without `VS`) | auto-detected |

```sh
make build COMPILER=clang                  # Windows: Ninja + clang-cl
make build COMPILER=clang VS=22            # Windows: VS 2022 + ClangCL toolset
make build COMPILER=clang CONFIG=Release   # Linux: clang++, Release
make package COMPILER=gcc                  # Linux: dist/vulkanwork-0.1.0-linux-gnu.tar.gz
```

## Packaging

`make package` configures a separate `<build dir>-package` tree with `CMAKE_BUILD_TYPE=Release` and `-DVW_PORTABLE=ON`, builds it, and runs CPack. With `VW_PORTABLE` the executable loads `assets.pak`, `models/` and `window.cfg` from its own directory instead of the build tree, so the archive runs from any location. The archive contains the executable, runtime DLLs (Windows, including the MSVC runtime), `assets.pak` and `models/DamagedHelmet.glb`. The Vulkan loader is not bundled; it ships with the GPU driver.

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
├── generic/          Windows default (MSVC, no VS= specified)
├── vs22/ vs26/       Visual Studio 2022 / 2026 (MSVC)
├── vs22-clang/ ...   Visual Studio + ClangCL toolset
├── clang-<CONFIG>/   Clang (Windows: Ninja + clang-cl, Linux: clang++)
├── gcc-<CONFIG>/     Linux GCC
└── *-package/        Release + VW_PORTABLE trees used by `make package`
dist/                 Package archives
```

Multi-config Visual Studio trees hold every `CONFIG` side by side. Single-config Ninja/Makefile trees get one directory per `CONFIG`.

## Configuring vcpkg Path

The Makefile requires the `VCPKG_ROOT` environment variable to be set. You can set it permanently in your shell profile, or pass it inline:

```powershell
$env:VCPKG_ROOT = "E:/vcpkg"   # PowerShell (current session)
make build
```

```powershell
make build VCPKG_ROOT=E:/vcpkg  # inline override
```

## CMake Presets (VS Code, Visual Studio, CLI)

`CMakePresets.json` defines Windows presets that already include the vcpkg toolchain and triplet:

| Configure preset | Generator | Compiler | Needs |
|---|---|---|---|
| `vs2022-msvc-debug` | Visual Studio 2022 | MSVC | VS 2022 C++ workload |
| `clang-cl-debug` / `clang-cl-release` | Ninja | `clang-cl` | `ninja` and `clang-cl` on `PATH` |
| `vs2026-clangcl` | Visual Studio 2026 | ClangCL toolset | VS component "MSBuild support for LLVM (clang-cl) toolset" |

```powershell
cmake --preset clang-cl-debug
cmake --build --preset clang-cl-debug
```

In VS Code, set `"cmake.useCMakePresets": "always"` and pick the preset in the CMake Tools status bar. Avoid CMake Tools kits for this project; a kit configures without the vcpkg toolchain.

The Clang presets use whichever `clang-cl` is first on `PATH`. To pin a specific one, such as the copy that ships with Visual Studio, put an override in an untracked `CMakeUserPresets.json`:

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "my-clang",
            "inherits": "clang-cl-debug",
            "cacheVariables": {
                "CMAKE_C_COMPILER": "C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Tools/Llvm/x64/bin/clang-cl.exe",
                "CMAKE_CXX_COMPILER": "C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Tools/Llvm/x64/bin/clang-cl.exe"
            }
        }
    ]
}
```

### Clang troubleshooting

| Symptom | Cause / fix |
|---|---|
| `MSB8020: The build tools for ClangCL (Platform Toolset = 'ClangCL') cannot be found` | The Visual Studio generator with `-T ClangCL` needs the MSBuild ClangCL toolset. Having the Clang compiler installed is not enough. Either install "MSBuild support for LLVM (clang-cl) toolset" (`Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset`) in the VS Installer, or use the `clang-cl-*` Ninja presets or `make build COMPILER=clang`, which do not need it. |
| `CMake was unable to find a build program corresponding to "Ninja"` | Put Ninja on `PATH` (`scoop install ninja`, `winget install Ninja-build.Ninja`, or use a Developer PowerShell, which includes the VS-bundled Ninja). The Makefile finds the VS-bundled Ninja automatically. |
| `Could not find a package configuration file provided by "glfw3"` | Configured without the vcpkg toolchain, typically through a CMake Tools kit. Use a preset, or pass `-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake`. |

## Opening in Visual Studio

```powershell
.\startvs.ps1            # Auto-detect the best installed VS
.\startvs.ps1 -t 2026   # Visual Studio 2026
.\startvs.ps1 -t 2022   # Visual Studio 2022
```

This will generate the solution if needed (or regenerate if files are missing) and launch VS. When multiple editions are installed, the script prefers Enterprise > Professional > Community.

## Android

Android builds don't go through the Makefile. They use Gradle (`settings.gradle` at the repo root, app module in `android/app`), and Gradle drives the same top-level `CMakeLists.txt` through the NDK. The output is an `arm64-v8a` APK that runs as a `NativeActivity`.

### Prerequisites

| Requirement | Version / notes |
|---|---|
| JDK | 17 or newer. Set `JAVA_HOME` to it. Android Studio's bundled JBR works |
| Android SDK Platform | 35 (`compileSdk`/`targetSdk`) |
| Android NDK | Exactly `28.2.13676358` (pinned by `ndkVersion`) |
| CMake (SDK package) | `3.22.1`, installed through the SDK Manager. Gradle does not use the CMake on `PATH` |
| SDK Platform-Tools | Provides `adb` for deploying |
| vcpkg | `VCPKG_ROOT` set, same checkout as desktop builds |
| `glslc` | On `PATH` from the desktop [Vulkan SDK](https://vulkan.lunarg.com/), or pass `-DVulkan_GLSLC_EXECUTABLE=...` |
| Python 3 | On `PATH`. Android packs `assets.pak` with `tools/packer/pack_uncompressed.py` because the native `pak_packer` can't run on the host during a cross build |
| Device | arm64 (`arm64-v8a`), Android 8.0 (API 26) or newer, Vulkan 1.1 |

No Gradle install is needed. Use the `gradlew` / `gradlew.bat` wrapper in the repo root, which downloads the pinned Gradle version.

Install the SDK pieces with Android Studio's SDK Manager (SDK Tools tab, enable "Show Package Details") or with `sdkmanager`:

```sh
sdkmanager "platforms;android-35" "ndk;28.2.13676358" "cmake;3.22.1" "platform-tools"
```

### Telling Gradle where the SDK is

The build looks for the SDK in this order: `sdk.dir` in `local.properties` at the repo root, then `ANDROID_HOME`, then `ANDROID_SDK_ROOT`. Android Studio writes `local.properties` for you. For command-line builds, create it or set `ANDROID_HOME`:

```properties
# local.properties (not committed; machine specific)
sdk.dir=C\:\\Users\\<you>\\AppData\\Local\\Android\\Sdk
```

The NDK path comes from `<sdk>/ndk/28.2.13676358`, so you don't need `ANDROID_NDK_HOME`. On the first configure, Gradle writes the vcpkg overlay triplet to `build/android-triplets/arm64-android.cmake`, using the template `android/app/triplets/arm64-android.cmake.in`.

### Building the APK

From the repo root:

```powershell
# Windows (PowerShell)
$env:VCPKG_ROOT = "E:/vcpkg"
$env:JAVA_HOME  = "C:/Program Files/Android/Android Studio/jbr"
.\gradlew.bat :app:assembleDebug
```

```sh
# Linux / macOS
export VCPKG_ROOT=$HOME/vcpkg
./gradlew :app:assembleDebug
```

Output: `android/app/build/outputs/apk/debug/app-debug.apk`.

The first build is slow because vcpkg compiles every dependency for `arm64-android`. Later builds reuse the vcpkg binary cache.

The build does this:

1. Compiles shaders to SPIR-V with the host `glslc`
2. Packs shaders and textures into an uncompressed `assets.pak` (Python packer)
3. Builds `libvulkanwork.so` with the NDK (`c++_shared` STL)
4. Stages `assets.pak` and `models/DamagedHelmet.glb` into `android/app/build/generated/assets/vulkanwork/`, which is merged into the APK `assets/`

On first launch the app copies `assets.pak` and the model from APK assets to app-internal storage. The desktop filesystem loaders then run unchanged.

`:app:assembleRelease` also builds, but there's no `signingConfig` yet, so it produces `app-release-unsigned.apk`. You must sign that with `apksigner` before it will install.

To use Android Studio instead, open the repo root (not `android/`). Let it sync, then run the `app` configuration. Android Studio needs `VCPKG_ROOT` in its environment, so set it system-wide and restart Android Studio.

### Deploying to a device

1. On the device, enable Developer options, then turn on USB debugging (or Wireless debugging).
2. Connect it and accept the RSA prompt, then check it shows up:

   ```sh
   adb devices
   ```

3. Install and launch:

   ```sh
   ./gradlew :app:installDebug                  # build + install in one step
   # or install an existing APK:
   adb install -r android/app/build/outputs/apk/debug/app-debug.apk

   adb shell am start -n dev.vulkanwork/android.app.NativeActivity
   ```

4. Watch logs, and uninstall when finished:

   ```sh
   adb logcat --pid=$(adb shell pidof dev.vulkanwork)   # sh; in PowerShell use $(adb shell pidof dev.vulkanwork) the same way
   adb uninstall dev.vulkanwork
   ```

The APK contains only `arm64-v8a` code. It won't install on x86_64 emulator images. Use a physical device or an arm64 system image.

### Troubleshooting

| Symptom | Fix |
|---|---|
| `VCPKG_ROOT is not set` | Export it in the shell (or system-wide for Android Studio) before running Gradle |
| `Android SDK not found` | Add `sdk.dir` to `local.properties` or set `ANDROID_HOME` |
| `NDK 28.2.13676358 not found` | Install that exact NDK version in the SDK Manager |
| `Could not find program glslc` | Put the Vulkan SDK `Bin` directory on `PATH` |
| `Could NOT find Python3` | Install Python 3 and put it on `PATH` |
| Gradle refuses to start / JVM version error | Point `JAVA_HOME` at JDK 17+ |
| `INSTALL_FAILED_NO_MATCHING_ABIS` | The device or emulator isn't arm64 |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | An APK signed with another key is installed. `adb uninstall dev.vulkanwork` first |
