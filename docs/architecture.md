# Architecture

## Overview

This document describes the architecture of the Vulkan renderer.

Layered structure: `main.cpp` → `App` → `Renderer` → graphics subsystems.

**App** (`src/app.h/cpp`): owns GLFW window, ImGui lifecycle, input dispatch, main loop. Calls `renderer.begin_frame()` / `renderer.end_frame()` each tick; resize handling uses `begin_frame()` return value.

**Renderer** (`src/graphics/renderer.h/cpp`, ~2800 lines): all Vulkan state. Systematic `create_*` functions for each Vulkan object. Helper utilities: `find_memory_type`, `create_buffer`, `begin_single_time_commands`. MAX_FRAMES_IN_FLIGHT = 2.

**Rendering pipeline:** Forward+ (tiled). Depth prepass → compute light culling (`shaders/light_cull.comp`) → forward PBR pass. Descriptor sets:
- Set 0 (per-frame): view/proj, camera pos, lighting, tile grid dims
- Set 1 (per-material): albedo, normal, metallic-roughness textures
- Set 2: SSBO of per-tile culled light lists

**Scene system** (`src/editor/`): SceneGraph owns hierarchy; SceneFile serializes/deserializes `.scene` files; Selection and Gizmo (ImGuizmo) handle editor interaction. Global mesh index tracked in `Renderer::meshes_` vector; each Mesh stores `sourcePath`/`sourceMeshIndex` for multi-model scenes.

**Asset pipeline:**
1. GLSL shaders compiled to SPIR-V via glslc (CMake custom targets)
2. pak_packer (`tools/packer/`) bundles SPIR-V + textures into LZ4-compressed `assets.pak`
3. Runtime: `pak::PackFile` (`src/pak/`) reads and decompresses on demand

**Loaders** (`src/loaders/`): glTF 2.0 via TinyGLTF, PLY for point clouds, dedicated gaussian splat loader.

**Gaussian splats** (`src/graphics/gaussianSplats.h/cpp`, `shaders/gsplat*.comp/vert/frag`): separate rendering path documented in `docs/gaussian_splats.md`.
