# Architecture

## Overview

This document describes the architecture of the Vulkan renderer.

Layered structure: `main.cpp` → `App` → `Renderer` → graphics subsystems.

**App** (`src/app.h/cpp`): owns GLFW window, ImGui lifecycle, input dispatch, main loop. Calls `renderer.begin_frame()` / `renderer.end_frame()` each tick; resize handling uses `begin_frame()` return value.

**Renderer** (`src/graphics/renderer.h/cpp`, ~2800 lines): all Vulkan state. Systematic `create_*` functions for each Vulkan object. Helper utilities: `find_memory_type`, `create_buffer`, `begin_single_time_commands`. MAX_FRAMES_IN_FLIGHT = 2.

**Rendering pipeline:** Forward+ (tiled). Shadow maps → depth prepass → compute light culling (`shaders/light_cull.comp`) → forward PBR pass. Descriptor sets:
- Set 0 (per-frame): view/proj, camera pos, lighting, tile grid dims
- Set 1 (per-material): base color, metallic-roughness, normal, emissive and occlusion textures, plus a factor UBO. `MaterialParams` in `src/graphics/material.h` holds the factors. Non-opaque materials skip the depth prepass and shade with all lights.
- Set 2: SSBO of per-tile culled light lists, shadow map array (binding 3)

**Shadows:** every mesh in `Renderer::meshes_` casts and receives. `update_shadows()` picks casters before `begin_frame()`: directional lights first, then the strongest point and spot lights at the camera whose radius sphere touches the camera frustum, up to `MAX_SHADOW_CASTERS` and `MAX_SHADOW_LAYERS` (a point light takes six layers, others one). Other lights stay unshadowed. `FrameUBO::shadowMatrices` holds one matrix per layer, and `GPULight::coneParams.zw` holds the first layer and texel size. Layers are cached across frames: a layer is redrawn only when its matrix changes, a mesh moves inside its view, or the mesh list changes. Gaussian splats neither cast nor receive.

**Scene system** (`src/editor/`): SceneGraph owns hierarchy (imported models keep their glTF node tree; mesh, light and splat nodes; lights are collected from node transforms each frame); SceneFile serializes/deserializes `.scene` files; Selection and Gizmo (ImGuizmo) handle editor interaction. Global mesh index tracked in `Renderer::meshes_` vector; each Mesh stores `sourcePath`/`sourceMeshIndex` for multi-model scenes.

**Asset pipeline:**
1. GLSL shaders compiled to SPIR-V via glslc (CMake custom targets)
2. pak_packer (`tools/packer/`) bundles SPIR-V + textures into LZ4-compressed `assets.pak`
3. Runtime: `pak::PackFile` (`src/pak/`) reads and decompresses on demand

**Loaders** (`src/loaders/`): glTF 2.0 via TinyGLTF, FBX and COLLADA via AssImp (desktop only), PLY for point clouds, dedicated gaussian splat loader.

**Gaussian splats** (`src/graphics/gaussianSplats.h/cpp`, `shaders/gsplat*.comp/vert/frag`): separate rendering path documented in `docs/gaussian_splats.md`.
