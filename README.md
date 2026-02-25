# vulkanwork

A barebones Vulkan project rendering a spinning, textured 3D cube with an FPS camera and Dear ImGui overlay.

## Building & Running

```powershell
make build
make run
```

See [BUILDING.md](BUILDING.md) for full build instructions, prerequisites, and Makefile targets.
See [ASSETS.md](ASSETS.md) for details on the `.pak` asset packing system.

## Controls

| Input | Action |
|---|---|
| W / A / S / D | Move forward / left / back / right |
| Space / Ctrl | Move up / down |
| Right-click + drag | Look around |
| Escape | Quit |

## Project Structure

```
├── CMakeLists.txt              CMake build configuration
├── Makefile                    Convenience build driver
├── setup.ps1                   One-time setup after cloning (Windows)
├── setup.sh                    One-time setup after cloning (Linux/macOS)
├── startvs.ps1                 Launch project in Visual Studio
├── format-staged.ps1           Format staged C/C++ files and optionally commit
├── vcpkg.json                  vcpkg package manifest
├── .githooks/
│   └── pre-commit              clang-format pre-commit hook
├── build/
│   ├── generic/                Default build output (includes assets.pak)
│   ├── vs22/                   Visual Studio 2022 build output
│   └── vs26/                   Visual Studio 2026 build output
├── shaders/
│   ├── cube.vert               Vertex shader (MVP transform)
│   └── cube.frag               Fragment shader (textured output)
├── textures/                   Source texture images
├── tools/
│   └── packer/main.cpp         CLI tool to build .pak asset archives
└── src/
    ├── main.cpp                Entry point
    ├── app.h / app.cpp         Application shell (window, ImGui, main loop)
    ├── renderer.h / renderer.cpp   Vulkan renderer (pipeline, drawing)
    ├── camera.h / camera.cpp   FPS camera system
    ├── pak_format.h            Binary .pak format structs
    ├── packfile.h / packfile.cpp   Asset pack reader (LZ4 decompression)
    └── config.h.in             CMake-generated paths
```
## Contributing

### First-time setup

After cloning, run the setup script once to activate the git hooks:

**Windows:**
```powershell
.\setup.ps1
```

**Linux/macOS:**
```sh
./setup.sh
```

This configures git to use the `.githooks/` directory, which contains a pre-commit hook that automatically runs clang-format on staged C/C++ files before each commit.

### Committing

The pre-commit hook runs automatically on `git commit`. You can also use the helper script to format and commit in one step:

```powershell
# Format and re-stage only:
.\format-staged.ps1

# Format, re-stage, and commit in one step:
.\format-staged.ps1 "your commit message"
```

## ToDo

- Version the .scene file format
- 

## Tools/Utilities

(DAE to glb online converter)[https://imagetostl.com/convert/file/dae/to/glb]
