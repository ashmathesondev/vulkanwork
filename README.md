# vulkanwork

A Vulkan renderer and small scene editor, built as a learning project. It renders glTF models with physically based shading under a Forward+ (tiled) light culling pipeline. It also renders 3D Gaussian Splatting captures entirely on the GPU. BUI 0.5.0 provides three docked editor panes and native file browsers for building, saving and loading scenes. Viewport transform handles use the Vulkan renderer.

The BUI editor currently targets Windows x64 using the SDK in `libs/bui`. Linux and Android builds are unavailable with this native host; they require a platform-specific BUI SDK and host integration.

## Features

- **PBR rendering:** metallic-roughness glTF 2.0 materials with base color, normal, metallic-roughness and emissive maps. Tangents are generated when a model has none.
- **Forward+ lighting:** depth prepass, then a compute shader culls lights into 16×16 pixel tiles, then the shading pass reads only each tile's lights. Directional, point and spot lights are supported, with a tile heatmap and light wireframes for debugging.
- **3D Gaussian Splatting:** loads standard 3DGS `.ply` files. Splats are projected, colored from spherical harmonics and depth sorted with a GPU radix sort every frame. A cloud can be drawn as alpha-blended Gaussians or as a plain point cloud.
- **Scene editor:**
  - **Application menu:** File (scene files and imports), Edit (selection and transforms), Add (lights), View (render toggles), and Window (pane focus, floating, docking and layout reset). Ctrl+N/O/S, Ctrl+Shift+S, and Ctrl+I / Ctrl+Shift+I work in the viewport and panes; text fields keep their editing keys.
  - **Dockable panes:** drag a Scene/Object, Material, or Options header out to float a resizable window. Drop it over either right-hand dock column to insert it at the preview position. Double-click a floating title/header, close its window, or choose Dock back from its header menu to re-dock. Drag the visible dividers to resize the dock area, columns, or stacked panes; double-click a divider to reset its size. Floating panes resize from their borders and corners. Right-click a header for explicit positions and Reset pane layout.
  - **Hierarchy:** meshes, lights and splat clouds are all scene graph nodes.
  - **Editing:** click to select, use the gizmo to move, rotate or scale, and edit node properties in the Scene Hierarchy panel.
  - **Scene files:** scenes save to and load from versioned `.scene` JSON files. Older files are migrated on load.
- **Asset pipeline:** shaders compile to SPIR-V at build time and are packed with textures into an LZ4-compressed `assets.pak` (see [ASSETS.md](ASSETS.md)).

## Building & Running

```powershell
make build CONFIG=RelWithDebInfo  # MSVC on Windows x64
make build COMPILER=clang
make run
make package            # relocatable Release archive in dist/
```

Android builds use Gradle (`.\gradlew.bat :app:assembleDebug`).

See [BUILDING.md](BUILDING.md) for prerequisites, every Makefile option, packaging, and the full Android build and deploy guide.

## Controls

| Input | Action |
|---|---|
| Right-click + drag | Look around |
| Right-click + W / A / S / D | Move forward / left / back / right |
| Space / Ctrl | Move up / down |
| Left-click | Select a mesh or light in the viewport |
| W / E / R (mouse released) | Gizmo mode: translate / rotate / scale |
| Delete | Delete the selected node |
| F2 / double-click a node | Rename it in the Scene Hierarchy (Enter or click away to apply, Escape to cancel) |
| Right-click a node | Context menu: Rename, Delete |
| Ctrl+S | Save the scene |
| Escape | Quit |

## Using the Editor

- **File menu:** New Scene, Load Scene, Save Scene, Save Scene As, Import Mesh (`.gltf`/`.glb`, plus `.fbx`/`.dae` on desktop), and Import Gaussian Splat (`.ply`).
- **Scene Hierarchy:** shows the scene as a tree; imported models keep their node hierarchy. Rename any node inline or with the inspector's Name field. Select a node to edit it there:
  - **Lights:** position, rotation, type, color, intensity, radius and cone angles.
  - **Splats:** switch between Gaussian and Points rendering and set the point size.
  - **"Import Mesh..." / "Import Splat..."** buttons open the matching import dialogs.
- **Lighting:** ambient light, plus buttons to add directional, point and spot lights.
- **Frame Statistics / Depth Debug:** FPS, tile counts, heatmap and wireframe toggles, and rasterizer debug switches.

Sample models live in `models/`. Gaussian splat samples, including synthetic test scenes and one real capture, live in `models/splats/`; see its README for sources and licenses.

## Project Structure

```
├── CMakeLists.txt              CMake build (shaders, asset packing, install/CPack)
├── Makefile                    Build driver: build / run / clean / package
├── settings.gradle, gradlew    Android build entry points
├── android/app/                Android app module (manifest, Gradle, vcpkg triplet)
├── setup.ps1 / setup.sh        One-time setup after cloning (git hooks)
├── startvs.ps1                 Generate and open the Visual Studio solution
├── format-staged.ps1           Format staged C/C++ files and optionally commit
├── vcpkg.json                  vcpkg package manifest
├── docs/
│   ├── architecture.md         Renderer and editor architecture
│   ├── build.md                Build quick reference
│   ├── gaussian_splats.md      3DGS implementation walkthrough
│   └── LEARNINGS.md            Design decisions, lessons, and a 3DGS deep dive
├── models/                     Sample glTF models; splats/ holds 3DGS samples
├── Scenes/                     Example .scene files
├── shaders/
│   ├── pbr.vert / pbr.frag     PBR forward shading
│   ├── light_cull.comp         Forward+ tile light culling
│   ├── gsplat_*.comp           Splat preprocess and GPU radix sort
│   ├── gsplat.vert / .frag     Splat and point drawing
│   └── debug_*                 Tile heatmap and light wireframes
├── textures/                   Source textures
├── tools/
│   ├── packer/                 pak_packer CLI and Python fallback for Android
│   └── splatgen/               Synthetic 3DGS test scene generator
└── src/
    ├── main.cpp                Entry point
    ├── app.h / app.cpp         Window, main loop, editor actions, file dialogs
    ├── logger.h                Header-only logger
    ├── config.h.in             CMake-generated asset paths
    ├── editor/                 Scene graph, scene files, selection, gizmo, UI panels
    ├── graphics/               Renderer, camera, lights, materials, Gaussian splat system
    ├── loaders/                glTF, PLY and 3DGS loaders
    ├── pak/                    .pak format and reader
    ├── platform/               Desktop (GLFW) and Android platform layers
    └── vendor/                 ImGuiFileDialog
```

## Documentation

- [BUILDING.md](BUILDING.md): prerequisites, build options, packaging, Android
- [ASSETS.md](ASSETS.md): the `.pak` asset format
- [docs/architecture.md](docs/architecture.md): how the renderer and editor fit together
- [docs/gaussian_splats.md](docs/gaussian_splats.md): the splat pipeline, shader by shader
- [docs/LEARNINGS.md](docs/LEARNINGS.md): lessons learned and how 3D Gaussian Splatting works

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

- Render more than one Gaussian splat cloud at a time
- Faster splat sorting for multi-million-splat captures
- Signing config for Android release builds

## Tools/Utilities

- [DAE to GLB online converter](https://imagetostl.com/convert/file/dae/to/glb)
