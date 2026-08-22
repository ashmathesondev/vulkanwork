# Building the Project

## Building In Powershell

```powershell
make build          # Build to build/generic/
make build VS=22    # Build for Visual Studio 2022 (build/vs22/)
make run            # Build and run
make rebuild        # Clean then build
make clean          # Remove build artifacts
```

## Unit Testing

No unit test framework. Validate assets with `pak_packer -v assets.pak`.

## Git Hooks

Run `.\setup.ps1` once after cloning to activate git hooks (auto-formats staged C++ files via clang-format on commit).