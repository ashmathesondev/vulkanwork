# Learnings from the Vulkan Work Project

This document summarizes the key architectural decisions, implementation patterns, and lessons learned from building this Vulkan rendering application.

## 0. References
-   [Khronos Vulkan Tutorial](https://docs.vulkan.org/tutorial/latest/00_Introduction.html)
-   [Vulkan Tutorial](https://vulkan-tutorial.com/)
-   [Vulkan API Documentation](https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VkInstance.html)
-   [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
-   [Vulkan Game Tutorial](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/introduction.html)

## 1. Project Structure & Build System

-   **Layered Code Organization:** The project successfully separates concerns into distinct layers:
    -   `main.cpp`: The application entry point.
    -   `app` (`app.h`, `app.cpp`): Manages the window, user input, and the main loop. It orchestrates the high-level interaction between other components.
    -   `renderer` (`renderer.h`, `renderer.cpp`): Encapsulates all Vulkan-specific logic. The `App` class interacts with the renderer through a clean, high-level interface (`init`, `begin_frame`, `end_frame`, etc.).
    -   `tools/packer`: A separate, self-contained command-line utility for asset preparation.

-   **CMake for Build Orchestration:** CMake is used not just for compiling C++ code, but as a full build system.
    -   It finds and links dependencies managed by `vcpkg`.
    -   It invokes the `glslc` compiler to build shaders as a custom command.
    -   It builds the `pak_packer` tool and then uses it in another custom command to bundle assets, ensuring the `assets.pak` file is up-to-date before the main application is built. This demonstrates a robust, dependency-aware build pipeline.

## 2. Core Rendering Architecture

-   **Systematic Vulkan Initialization:** The `Renderer` class follows a structured, methodical approach to creating Vulkan objects. Each object type (instance, device, swapchain, etc.) has its own `create_*` function, making the complex setup process more manageable and debuggable.

-   **Robust Frame Loop:** The main loop in `app.cpp` is designed for correctness and stability.
    -   It uses a `MAX_FRAMES_IN_FLIGHT` of 2, a common and effective strategy for keeping the CPU and GPU working in parallel without too much input lag.
    -   It correctly handles swapchain recreation by checking the return value of `renderer.begin_frame()` and skipping a frame's work if the swapchain is being rebuilt. This is a critical feature for a resizeable window.

-   **Centralized Helper Functions:** The `Renderer` class contains a rich set of helper functions for common Vulkan tasks (`find_memory_type`, `create_buffer`, `begin_single_time_commands`). This is a huge learning point: investing in these helpers pays off by making the rest of the rendering code cleaner, less error-prone, and easier to read.

## 3. PBR and Materials

-   **Efficient Descriptor Set Strategy:** The renderer uses a highly efficient two-level descriptor set binding strategy, which is a key pattern for performant rendering:
    -   **Set 0 (Per-Frame):** Bound once per frame. Contains uniforms that are constant for the entire frame, such as view/projection matrices, camera position, and lighting information.
    -   **Set 1 (Per-Material):** Bound once per material. Contains the textures (albedo, normal, metallic-roughness) and other properties specific to that material. This avoids redundant state changes when rendering multiple objects that share the same material.

-   **Persistently Mapped Uniform Buffers:** The per-frame `FrameUBO` is updated using persistently mapped buffers (`uniformBuffersMapped_`). This is the most efficient way to get frequently-changing data from the CPU to the GPU, as it avoids the overhead of memory mapping and unmapping every frame.

## 4. Asset Pipeline

This project features a complete, end-to-end asset pipeline, which is a major learning in itself. The pipeline is more than just the renderer; it's the entire process from source art to final rendered pixel.

-   **glTF as the Scene Standard:** Using glTF 2.0 is a modern and effective choice. The `gltfLoader` shows what is required to properly ingest this format.
    -   **On-the-Fly Tangent Generation:** The loader doesn't assume the input model has tangents. The `compute_tangents` function is a critical piece of robustness, allowing the renderer to use normal maps even if the source model wasn't prepared for it.
    -   **sRGB Correctness:** The loader correctly identifies which textures should be treated as sRGB (e.g., base color) and which should be linear (e.g., normal maps, metallic-roughness). This is absolutely essential for correct lighting calculations in a PBR system.

-   **Custom Asset Packing (`pak_packer`):**
    -   **Why Pack Assets?** This approach reduces the number of files that need to be handled at runtime, which can improve loading speed. It simplifies application distribution and enables global operations like compression.
    -   **LZ4 for Fast Compression:** The choice of LZ4 is deliberate. It prioritizes decompression speed over the highest compression ratio, which is the right trade-off for game assets that need to be loaded quickly.
    -   **Tooling as Part of the Project:** The `pak_packer` is not an external dependency; it's a first-class citizen of the codebase, built and maintained alongside the renderer.

## 5. UI and Debugging

-   **Seamless ImGui Integration:** ImGui is integrated directly into the main render pass and command buffer. This is an efficient approach that avoids the complexity of multiple render passes for UI. The project also correctly gives ImGui its own dedicated descriptor pool.

-   **Keep Windows Off the Main Menu Bar:** ImGui windows can be dragged over the main menu bar, and positions persist in `imgui.ini`. A window at the default `(10, 10)` covered the File menu, so Load and Save As looked unimplemented even though they worked. Each editor window now clamps itself below `ImGui::GetMainViewport()->WorkPos` after `Begin()`, which also repairs saved layouts.

-   **File Dialog Settings Matter:** ImGuiFileDialog defaults to no flags when given a config. Save dialogs need `ImGuiFileDialogFlags_ConfirmOverwrite`, and both load and save should be `Modal`, with a minimum size passed to `Display()`.

-   **Global Hotkeys Must Yield to Text Input:** Escape used to quit the app unconditionally, so cancelling a rename with Escape would have closed the editor. Checking `io.WantCaptureKeyboard` is not enough on its own, because `key_down` is level-triggered: the text field closes on the press frame and the still-held key quits on the next one. Quit only on the press *edge*, and only when ImGui is not capturing the keyboard.

-   **Validation Layers are Essential:** The setup of the `VkDebugUtilsMessengerEXT` in the `Renderer` is non-negotiable for serious Vulkan development. The validation layers are the single most important tool for catching errors, and the detailed feedback they provide is invaluable.

## 6. Scene File Format & Multi-Model Scenes

-   **Global vs. Local Mesh Indices:** The renderer maintains a flat, global array of all loaded meshes (`Renderer::meshes_`). When a scene contains objects from multiple source files (imported one by one), the global mesh index of any given mesh depends on the *order* in which models were loaded. This is a runtime detail that cannot be stored directly in a persistent file.

-   **Per-Node Source Tracking is Required:** The original scene file format stored a single root-level `modelPath` and relied on each `SceneNode` having a *global* `meshIndex`. This was a latent bug: any scene with more than one imported model would silently fail to load correctly, because the second and subsequent models were never loaded at all. Objects would appear in the scene hierarchy (since the graph was deserialized) but would not render (since their mesh indices were out of bounds). The fix requires two things:
    1.  **`Mesh` carries its origin:** Add `sourcePath` (the glTF/GLB file the mesh was loaded from) and `sourceMeshIndex` (the mesh's local index *within that file*) to the `Mesh` struct. These are set in `Renderer::load_scene` and `Renderer::import_gltf` immediately after loading.
    2.  **`SceneNode` mirrors this for serialization:** Add `modelPath` and `meshIndexInModel` to `SceneNode`. The scene file serializes these per-node fields instead of the global `meshIndex`. On load, `do_load_scene` lazily loads each unique `modelPath` via `import_gltf`, tracks the mesh offset for each model, and computes the correct global `meshIndex` for every node.

-   **Backward Compatibility via Fallback:** The loader can support old-format scene files (single root `modelPath`, global `meshIndex` per node) by detecting the absence of the new per-node `modelPath` field and falling back to the root value. This prevents hard breaks when loading scenes saved before the format change.

-   **Keep the Source Hierarchy:** Imports used to flatten a glTF into one root node per mesh with baked world transforms, so a 400-node model became 400 unrelated roots and moving a parent part was impossible. The loader now also returns the node tree (`ImportedNode`: local transform, parent/children, owned meshes). `App::add_imported_hierarchy()` rebuilds it as scene nodes, with one child node per primitive when a glTF node draws several, because a `SceneNode` holds one mesh. The flat mesh order is unchanged, so `meshIndexInModel` in saved scenes still resolves. A load-time comparison confirmed the rebuilt world transforms match the old baked ones exactly on Buggy, GearboxAssy and Duck.

-   **Rolling FPS Averages Lie Right After a Stall:** ImGui's `io.Framerate` averages recent frames, so a reading taken within a second of a long load shows an inflated frame time. Wait a few seconds before comparing numbers.

-   **Built-in Meshes Need a Source Identity Too:** The renderer always injects a built-in cube into every scene (via `add_cube_to_scene`). This mesh has no source file, so it uses an empty `sourcePath`. On load, `load_scene_empty()` is called first, placing the cube at a known global index (0), and `modelOffsets["internal://cube"] = 0` establishes its identity in the offset map.

## 7. Logging & Observability

-   **Silent Failures Are the Enemy:** Several bugs in this project manifested as objects being visible in the scene hierarchy but not rendering. Without a log, there was no output to diagnose the problem. A bare `catch (...) { return false; }` around JSON parsing, or a `return false` on a failed file open, gives the caller no actionable information. Every error path should log *what* failed and *why*.

-   **Add Logging Before Debugging:** The first step when an operation silently produces the wrong result should be to add structured logging around the entire operation, not to immediately reach for a debugger. Logging the count of nodes loaded, the count of meshes available, and any index-out-of-range conditions quickly narrows the problem to the exact point of failure.

-   **A Simple Singleton Logger Is Sufficient:** A header-only `Logger` singleton with `LOG_INFO` / `LOG_WARN` / `LOG_ERROR` macros (backed by `fprintf` + `vsnprintf`) is zero-dependency and covers almost all practical needs. Key design choices:
    -   **Truncate on startup** (`fopen(..., "w")`): the log always reflects the most recent run, so there is no stale noise to sift through.
    -   **Flush after every write** (`fflush`): if the application crashes, the last log line is not lost in a buffer.
    -   **Mirror to `stderr`**: log messages are visible immediately in the IDE output window without needing to open a file.
    -   **Platform-aware log path**: on Windows, write to `%USERPROFILE%\VulkanWork\vulkanwork.log`; on Linux, follow XDG conventions. This keeps logs out of the working directory (which may be the project root) and in a predictable per-user location.

## 8. Build System, Toolchains & Packaging

-   **One Makefile, Two Shells:** GNU Make picks recipes per OS (`ifeq ($(OS),Windows_NT)`): PowerShell on Windows, `/bin/sh` on Linux. `COMPILER=` selects MSVC, Clang or GCC. Single-config generators (Ninja, Unix Makefiles) get one build tree per `CONFIG`; multi-config Visual Studio trees hold all configs side by side.

-   **Two Ways to Use Clang on Windows:** The Visual Studio `-T ClangCL` toolset needs the optional "C++ Clang tools for Windows" VS component. Missing it fails at configure time with `MSB8020`. Ninja + LLVM's `clang-cl` works without that component or a developer shell, because clang-cl locates MSVC headers, libs and the Windows SDK itself. Ninja ships with Visual Studio and can be found with `vswhere`.

-   **Baked Absolute Paths Block Relocation:** `config.h` bakes build-tree paths (`PAK_FILE`, `MODEL_DIR`, `CONFIG_DIR`) into the binary. That suits development but breaks any package moved to another directory or machine. The `VW_PORTABLE` CMake option switches desktop builds to resolve assets relative to the executable (`GetModuleFileNameW` / `/proc/self/exe`). `make package` always builds with it on.

-   **Let CMake Find Runtime DLLs:** `$<TARGET_RUNTIME_DLLS:target>` (CMake 3.21+) installs exactly the DLLs a target links (GLFW, LZ4). `InstallRequiredSystemLibraries` adds the MSVC runtime. The Vulkan loader is deliberately not bundled, because it ships with the GPU driver.

## 9. Android Build Pitfalls

-   **Check What Git Actually Tracks:** `.gitignore` excluded `android/` and `gradle/`, so the Gradle project and wrapper existed only on one machine. Every build looked fine locally while a fresh clone could not build at all. `git check-ignore -v <path>` exposes this quickly.

-   **No Machine-Specific Paths in Tracked Files:** The NDK path was hardcoded in `build.gradle` and in the vcpkg triplet. vcpkg triplets cannot receive CMake arguments, so Gradle now resolves the NDK from `sdk.dir`/`ANDROID_HOME` plus the pinned `ndkVersion`. It then writes the overlay triplet from a template to `build/android-triplets/`, outside the app build directory so `gradlew clean` does not delete it. `local.properties` stays untracked.

-   **CMake Lists vs. Quoted Strings:** `set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DA=1 -DB=2")` passes one argument, so the NDK saw `ANDROID_PLATFORM="android-26 -DANDROID_ABI=arm64-v8a"`, failed to resolve the API level and could not find `libvulkan.so` ("Vulkan wasn't found"). Unquoted `set(VAR -DA=1 -DB=2)` makes a proper list.

-   **The vcpkg Binary Cache Can Hide Broken Triplets:** The bad triplet went unnoticed because vcpkg restored previously built packages. Changing the triplet changed its ABI hash, forcing a real rebuild that exposed the bug. To validate a triplet change, build from a clean `.cxx/` directory.

## 10. Header Order and clang-format

-   **`IncludeBlocks: Regroup` Reorders Headers:** The project's clang-format merges and sorts include blocks. That silently breaks headers that are not self-contained:
    -   `vulkan/vulkan_android.h` needs the core Vulkan types first. The fix is to define `VK_USE_PLATFORM_ANDROID_KHR` from CMake for Android and include only `<vulkan/vulkan.h>`.
    -   `ImGuizmo.h` needs `imgui.h` first, and case-sensitive sorting puts `ImGuizmo.h` ahead of it. Guard such includes with `// clang-format off` / `// clang-format on`.

-   **Unformatted Files Are Latent Build Breaks:** A file that has never been run through clang-format may compile only because of its current include order. The pre-commit hook formats staged files, so the first edit to such a file can break the build. Run `clang-format --dry-run --Werror` on files before relying on the hook.

## 11. Debugging "Unlit" Assets

-   **Inspect the Asset Before the Renderer:** "The helmet receives no light" was not a shader bug. The GLB's material had been re-exported with `baseColorFactor = [0,0,0,1]`, no base color, normal or metallic-roughness textures, and the metal-roughness image wired to the occlusion slot. Per glTF, a missing `metallicFactor` defaults to 1.0. A fully metallic surface with black albedo has F0 = 0, so every light contributes nothing and only the emissive texture shows.

-   **The Loader Log Is a Cheap First Check:** The log line `1 material(s), 2 texture(s)` for a model known to use 5 textures pointed straight at the asset. Dumping the GLB's JSON chunk (12-byte header, then a length-prefixed JSON chunk) confirmed it in seconds.

-   **Exporters Drop Material Wiring:** Blender's glTF exporter only exports what it can trace through the shader node graph. Check the material's node wiring before re-exporting a model.

## 12. Lights as Scene Nodes

-   **One Source of Truth:** Lights used to live in `LightEnvironment`, separate from the scene graph, and were edited only in the Lighting window. They are now a `LightComponent` on `SceneNode`. Each frame `SceneGraph::collect_lights()` rebuilds the directional/point/spot lists from node world transforms, so the hierarchy, gizmo, picking and save/load all treat lights like any other node. Ambient light stays global.

-   **Transform Conventions:** A light's position is the node's world translation and its direction is the node's local -Z axis, matching glTF's `KHR_lights_punctual`. The direction comes from the normalized Z column, so node scale never skews it. `make_light_transform()` builds the inverse mapping, from a position and direction to a matrix.

-   **Picking Without Geometry:** Light nodes have no mesh, so picking tests a small fixed-size box around the light's origin, using the same ray/AABB code as meshes.

-   **Versioned Scene Files with Migration:** Scene version 2 stores lights per node. Version 1 files are migrated on load by converting their typed light arrays into root light nodes, so old scenes keep working without a separate conversion step.

## 13. Gaussian Splatting: Bugs That Hid Until Real Data

The splat pipeline compiled and "ran" for months, but it had never been fed a real
capture. Synthetic test scenes built to isolate each property (anisotropy and rotation,
SH view dependence, overlapping translucency), plus one real capture, exposed seven
independent bugs:

-   **Missing focal lengths in the Jacobian:** The projected covariance was in normalized
    camera units, so the 0.3 px² low-pass term dominated and every splat became a dot.
-   **Row-major thinking in a column-major language:** `mat3(...)` literals typed row by
    row produced transposed rotation and Jacobian matrices.
-   **Double Y flip:** The projection matrix already contains Vulkan's Y flip. Flipping
    the pixel centre again left quads in the right place but evaluated the Gaussian
    around a mirrored centre, so most fragments were discarded.
-   **Unnormalized quaternions:** Trained 3DGS PLYs store them unnormalized.
-   **Unstable radix scatter:** `atomicAdd`-claimed slots are not stable, which breaks
    LSD radix sort. Rank elements deterministically in shared memory instead.
-   **Write-after-read across frames in flight:** Buffers shared by both frames need a
    draw → compute barrier at the start of the next frame's dispatch.
-   **Gamma applied twice:** sRGB-trained colours written to an sRGB swapchain must be
    linearized first.

**Lesson:** for a numeric pipeline, build tiny synthetic inputs where the correct image is
obvious (an axis gizmo, a sphere whose colour depends on view direction). Each one
isolates a single property, so a wrong picture points straight at the bug.

-   **Discoverability counts as functionality:** Splat import existed only in the File
    menu, so it looked missing to the user. The Scene Hierarchy now has "Import Splat..."
    next to "Import Mesh...".

## 14. Deep Dive: How 3D Gaussian Splatting Works

This section explains the technique itself: what a splat is, why the math is shaped the way it is, how a scene is trained, and how it is drawn. `docs/gaussian_splats.md` walks through this project's shaders line by line; this section is the "why" behind them.

### 14.1 The core idea

3D Gaussian Splatting (Kerbl, Kopanas, Leimkühler and Drettakis, SIGGRAPH 2023) represents a scene as millions of small, semi-transparent, colored 3D ellipsoids. Each one is a 3D Gaussian, the same bell curve as the normal distribution but in three dimensions. To render, each Gaussian is projected to the screen as a 2D ellipse ("splatted"), and the ellipses are alpha-blended in depth order.

It sits between two older ideas:

-   **Point clouds** are explicit and fast but have holes and hard edges, because a point has no extent.
-   **NeRF** stores the scene implicitly in a neural network and renders by marching rays through it. Quality is excellent, but every pixel needs hundreds of network evaluations.

3DGS keeps the explicit, rasterizable nature of points but gives each point a size, an orientation, a soft falloff and view-dependent color. Nothing is evaluated by a network at render time, so it runs in real time on ordinary GPUs.

### 14.2 What one Gaussian stores

| Parameter | Stored as | Used as | Why that encoding |
|---|---|---|---|
| Mean μ | 3 floats | World-space centre | |
| Scale s | 3 floats, log-space | `exp(s)` per axis | Stays positive under any optimizer step |
| Rotation q | 4 floats, unnormalized quaternion | `normalize(q)` | Any 4 numbers give a valid rotation once normalized |
| Opacity o | 1 float, logit-space | `sigmoid(o)` | Stays in (0, 1) under any optimizer step |
| Color | 48 floats of SH (3 channels × 16) | Evaluated per view direction | View-dependent appearance |

These encodings exist for **training**: gradient descent can push any parameter anywhere and the decoded value is still valid. A renderer just has to remember to decode them. Missing the quaternion normalization was one of this project's real bugs (section 13).

In the reference PLY, each Gaussian takes 62 floats (248 bytes), including three unused normal slots. `RawSplat` drops the normals and pads to 60 floats (240 bytes) so the GPU can read it directly as a std430 array. The 741,883-splat `train_7k.ply` sample therefore takes about 178 MB of GPU memory before any per-frame buffers.

### 14.3 From scale and rotation to a covariance

A 3D Gaussian's shape is its covariance matrix Σ, a symmetric 3×3 matrix. Its density at point x is:

```
G(x) = exp( -1/2 · (x - μ)ᵀ Σ⁻¹ (x - μ) )
```

Optimizing Σ's six entries directly does not work: a gradient step can easily make Σ non-positive-definite, which no longer describes an ellipsoid. 3DGS factors it instead:

```
Σ = R · S · Sᵀ · Rᵀ        with S = diag(exp(s)), R = rotation(normalize(q))
```

This is the matrix version of "take a unit sphere, stretch it along its axes by S, then rotate it by R". It is positive-definite by construction for any parameter values.

### 14.4 Projecting an ellipsoid to the screen (EWA splatting)

Perspective projection is not linear, so a 3D Gaussian does not project to an exact 2D Gaussian. 3DGS borrows the standard approximation from EWA splatting (Zwicker et al., 2001): linearize the projection around the splat's centre with its Jacobian J, then push the covariance through that linear map:

```
Σ' = J · W · Σ · Wᵀ · Jᵀ        (keep the upper-left 2×2)
```

-   **W** is the linear part of world-to-view (here `view · model`).
-   **J** is the Jacobian of view-space-to-pixel coordinates at the splat centre. It depends on the focal lengths in pixels and on depth, which is why far splats shrink and splats near the screen edge stretch.

Splats only look right if Σ' really is in pixels. This project originally left the focal lengths out of J, and every splat collapsed to a dot.

Two practical details:

-   **Low-pass filter.** 0.3 is added to Σ' on the diagonal, so even a tiny or very thin Gaussian covers about one pixel. Without it, sub-pixel splats would flicker in and out as the camera moves. It is a simple anti-aliasing filter; Mip-Splatting (Yu et al., CVPR 2024) later refined this for scenes viewed at very different scales.
-   **Bounding quad.** The eigenvectors of Σ' give the ellipse's axes and the square roots of its eigenvalues give its standard deviations. Drawing a quad that reaches 3 standard deviations covers about 99% of the Gaussian, as in `gsplat.vert`.

The first-order linearization is also a known limitation. Large splats near the screen edge, especially with a wide field of view, are visibly distorted because the true projection curves away from the tangent approximation.

### 14.5 Compositing: why sorting is unavoidable

Each pixel's color is the classic front-to-back alpha compositing sum over the splats that cover it, sorted by depth:

```
C = Σᵢ cᵢ · αᵢ · Πⱼ<ᵢ (1 - αⱼ)
αᵢ = sigmoid(oᵢ) · exp( -1/2 · dᵀ Σ'⁻¹ d )       (d = pixel - projected centre)
```

`Πⱼ<ᵢ (1 - αⱼ)` is the transmittance: how much light still gets through the splats in front. This is exactly the same equation NeRF uses for volume rendering along a ray. The difference is that NeRF samples a continuous field at many points along the ray, while 3DGS sums a short list of explicit primitives. That shared equation is why 3DGS can reach NeRF-like quality.

Because the sum is order dependent, splats must be sorted by depth. Front-to-back with the "under" operator and back-to-front with the "over" operator give the same result. This project draws back-to-front and lets the hardware blender apply `src·α + dst·(1 - α)`. Vulkan guarantees that blending happens in primitive order, so a sorted draw composites correctly.

Splats are sorted by the depth of their **centres**, once per splat, not per pixel. Where two large splats overlap, the true front-to-back order can differ across the overlap, and it can flip suddenly as the camera moves. That causes the "popping" artifacts visible in most 3DGS viewers. Later work (for example StopThePop, 2024) sorts more locally to reduce it.

### 14.6 View-dependent color with spherical harmonics

Real surfaces look different from different angles: highlights move and reflections shift. 3DGS stores each splat's color as a function on the sphere of view directions, encoded with real spherical harmonics (SH) up to degree 3:

| Degree | Coefficients per channel | Captures |
|---|---|---|
| 0 | 1 | Base color (the "DC" term) |
| 1 | 3 | Simple directional tint |
| 2 | 5 | Broader lobes |
| 3 | 7 | Sharper view-dependent detail |

That is 16 coefficients per channel and 48 per splat. At render time the shader evaluates the SH polynomial for the direction from the camera to the splat, then adds 0.5, because training stores colors offset by -0.5 so they start centred at zero. The direction must be in the cloud's own object space. If the cloud is moved or rotated, the direction has to be brought back through the inverse model transform, as `gsplat_preprocess.comp` does.

SH is also where most of the memory goes: 45 of the 59 meaningful floats per splat are the higher-order coefficients. Many compressed formats keep only degree 0 or quantize the rest aggressively.

### 14.7 How a scene is trained

A splat scene is not modeled by hand; it is fitted to photographs:

1.  **Camera poses and a sparse point cloud.** Structure-from-Motion (COLMAP) estimates where each photo was taken and triangulates a sparse set of 3D points. This is why captures arrive in COLMAP's coordinate convention (Y down), and why this editor imports them with a 180° rotation about X.
2.  **Initialization.** One Gaussian per SfM point, with an isotropic size based on the distance to its nearest neighbours, low opacity, and color from the point.
3.  **Differentiable rendering.** For a random training photo, render the Gaussians from that camera with a differentiable rasterizer. Compare against the photo using a loss of `(1 - λ)·L1 + λ·D-SSIM`, with λ = 0.2. Backpropagate to every Gaussian's parameters.
4.  **Adaptive density control.** The sparse start cannot represent fine detail, so every 100 iterations during the early phase of training:
    -   **Clone** small Gaussians in under-reconstructed regions. These are detected by large view-space position gradients, meaning the optimizer keeps trying to move them.
    -   **Split** large Gaussians in over-reconstructed regions into two smaller ones, with scales divided by 1.6.
    -   **Prune** Gaussians that are nearly transparent or far too large.
    -   Periodically **reset opacities** to a low value so floaters that are not really needed fade and get pruned.
5.  **Progressive SH.** Training starts with only the DC color and enables higher SH degrees step by step, so view dependence does not absorb errors that should be fixed by geometry.

The reference schedule trains for 30,000 iterations. The `train_7k.ply` sample is an earlier 7,000-iteration snapshot: densification has done much of its work, but fine detail and floater cleanup are not finished. That is why it looks blurrier, especially close to the camera.

### 14.8 How the reference rasterizer differs from this project

| | Reference (CUDA) | This project (Vulkan) |
|---|---|---|
| Work split | Screen divided into 16×16 tiles; each splat is duplicated into every tile it touches | One quad per splat |
| Sort | One radix sort on a 64-bit key: tile ID (high bits) + depth (low bits) | One global 32-bit depth sort |
| Blending | Software, front-to-back per pixel, in a compute kernel | Hardware blending, back-to-front |
| Early exit | Stops a pixel once transmittance drops below 0.0001 | None; every covering splat is blended |
| Needs gradients | Yes, for training | No, render only |

The tiled design suits training: per-pixel early termination saves a lot of work, and a hand-written blend loop is easy to differentiate. Hardware rasterization is simpler and fast for viewing, but it pays for overdraw that the reference skips. In both designs, the per-frame sort is the main fixed cost. For `train_7k.ply` on an RTX 3080 Ti, this project spends about 17 ms per frame, roughly the same in Gaussian and Points modes. That shows the cost is in projection and sorting, not in drawing.

### 14.9 Practical consequences and limitations

-   **Splats are not surfaces.** There are no normals, meshes or materials, so a splat scene cannot be relit with this renderer's PBR lights. Its lighting is baked into the colors.
-   **Quality is tied to the training views.** Near the capture cameras the result is close to the photos. Away from them, under-constrained regions show floaters and blurry, stretched splats.
-   **Mixing with meshes works through depth.** Meshes are drawn first and write depth; splats then depth-test against them without writing depth. A mesh in front hides the splats behind it, and splats in front of a mesh blend over it. The mesh fades out as the splats' combined opacity builds up. What does not work is a second transparent layer after the splats, because the splats leave no depth for it to test against.
-   **Size.** Uncompressed PLYs run to hundreds of MB. Lighter formats such as antimatter15's `.splat`, Niantic's `.spz` and PlayCanvas's compressed PLY quantize positions, scales and colors and often drop higher-order SH. They are worth supporting if loading time or download size matters.

### 14.10 Further reading

-   [3D Gaussian Splatting for Real-Time Radiance Field Rendering](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/) (Kerbl et al., 2023): the original paper, code and models
-   EWA Splatting (Zwicker, Pfister, van Baar and Gross, 2001): the projection approximation 3DGS builds on
-   Mip-Splatting (Yu et al., CVPR 2024): anti-aliasing across scales
-   StopThePop (Radl et al., SIGGRAPH 2024): sorting that reduces popping
-   `docs/gaussian_splats.md`: this project's implementation, shader by shader
