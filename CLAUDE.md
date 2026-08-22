# CLAUDE.md

## Build Commands

Reference `docs/build.md` for detailed build instructions.
/
## Architecture

Reference `docs/architecture.md` for detailed architecture documentation.

## Key Dependencies (via vcpkg)

glfw3, glm, vulkan, imgui, imguizmo, tinygltf, stb, nlohmann-json, lz4

## Code Style

clang-format enforced (LLVM style, 80-col, 4-space tabs, Allman braces). Pre-commit hook runs automatically after `setup.ps1`. Manual format: `.\format-staged.ps1`.

C++20. Follow `.editorconfig` (tabs, LF, UTF-8). Follow `.clang-format` for formatting rules.

## Docs

`docs/LEARNINGS.md` — architecture decisions and Vulkan patterns used in this project. Read before making non-trivial renderer changes.

`ASSETS.md` — `.pak` binary format spec (FileHeader 24B + TocEntry 280B each + LZ4 data).

`BUILDING.md` — prerequisites and vcpkg path configuration.

`docs/build.md` — build instructions and troubleshooting.
