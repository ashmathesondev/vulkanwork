# vulkanwork

A barebones Vulkan project rendering a spinning, multi-coloured 3D cube with an FPS camera and Dear ImGui overlay.

## Prerequisites

- [CMake](https://cmake.org/) 3.20+
- [vcpkg](https://github.com/microsoft/vcpkg) (expected at `D:/vcpkg` — edit `Makefile` to change)
- [GNU Make](https://www.gnu.org/software/make/)
- A C++20 compiler (MSVC, Clang, GCC)
- Vulkan-capable GPU and drivers

## Building

```bash
make build
```

This will:
1. Install dependencies via vcpkg (Vulkan, GLFW, GLM, Dear ImGui)
2. Configure the CMake project
3. Compile shaders to SPIR-V
4. Build the executable

## Running

```bash
make run
```

## Controls

| Input | Action |
|---|---|
| W / A / S / D | Move forward / left / back / right |
| Space / Ctrl | Move up / down |
| Right-click + drag | Look around |
| Escape | Quit |

## Makefile Targets

| Target | Description |
|---|---|
| `make build` | Configure (if needed) and build |
| `make run` | Build and run |
| `make clean` | Clean build artifacts |
| `make rebuild` | Clean then build |

## Project Structure

```
├── CMakeLists.txt          CMake build configuration
├── Makefile                Convenience build driver
├── vcpkg.json              vcpkg package manifest
├── shaders/
│   ├── cube.vert           Vertex shader (MVP transform)
│   └── cube.frag           Fragment shader (vertex colours)
└── src/
    ├── main.cpp            Entry point
    ├── app.h / app.cpp     Vulkan application (pipeline, rendering, ImGui)
    ├── camera.h            FPS camera system
    └── config.h.in         CMake-generated shader path
```
