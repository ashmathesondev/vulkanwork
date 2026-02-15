# vulkanwork

A barebones Vulkan project rendering a spinning, multi-coloured 3D cube with an FPS camera and Dear ImGui overlay.

## Building & Running

```powershell
make build
make run
```

See [BUILDING.md](BUILDING.md) for full build instructions, prerequisites, and Makefile targets.

## Controls

| Input | Action |
|---|---|
| W / A / S / D | Move forward / left / back / right |
| Space / Ctrl | Move up / down |
| Right-click + drag | Look around |
| Escape | Quit |

## Project Structure

```
├── CMakeLists.txt          CMake build configuration
├── Makefile                Convenience build driver
├── startvs.ps1             Launch project in Visual Studio
├── vcpkg.json              vcpkg package manifest
├── build/
│   ├── generic/            Default build output
│   ├── vs22/               Visual Studio 2022 build output
│   └── vs26/               Visual Studio 2026 build output
├── shaders/
│   ├── cube.vert           Vertex shader (MVP transform)
│   └── cube.frag           Fragment shader (vertex colours)
└── src/
    ├── main.cpp            Entry point
    ├── app.h / app.cpp     Vulkan application (pipeline, rendering, ImGui)
    ├── camera.h            FPS camera system
    └── config.h.in         CMake-generated shader path
```
