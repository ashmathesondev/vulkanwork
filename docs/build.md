# Building the Project

## Building

Windows recipes run in PowerShell, Linux recipes in `/bin/sh`.

```sh
make build                   # Build (Windows: build/generic/, Linux: build/gcc-Debug/)
make build VS=22             # Build for Visual Studio 2022 (build/vs22/)
make build COMPILER=clang    # Clang (Windows: clang-cl + Ninja, Linux: clang++)
make build CONFIG=Release    # Select configuration
make run                     # Build and run
make rebuild                 # Clean then build
make clean                   # Remove build, package and dist/ outputs
make package                 # Relocatable Release archive in dist/
make help                    # Show targets and resolved options
```

See `BUILDING.md` for the full option list and package layout.

## Android

Gradle, not the Makefile: `.\gradlew.bat :app:assembleDebug` / `./gradlew :app:installDebug`. Prerequisites, SDK setup and deployment steps are in the Android section of `BUILDING.md`.

## Unit Testing

No unit test framework. Validate assets with `pak_packer -v assets.pak`.

## Git Hooks

Run `.\setup.ps1` once after cloning to activate git hooks (auto-formats staged C++ files via clang-format on commit).