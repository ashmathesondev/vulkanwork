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

-   **Validation Layers are Essential:** The setup of the `VkDebugUtilsMessengerEXT` in the `Renderer` is non-negotiable for serious Vulkan development. The validation layers are the single most important tool for catching errors, and the detailed feedback they provide is invaluable.

## 6. Scene File Format & Multi-Model Scenes

-   **Global vs. Local Mesh Indices:** The renderer maintains a flat, global array of all loaded meshes (`Renderer::meshes_`). When a scene contains objects from multiple source files (imported one by one), the global mesh index of any given mesh depends on the *order* in which models were loaded. This is a runtime detail that cannot be stored directly in a persistent file.

-   **Per-Node Source Tracking is Required:** The original scene file format stored a single root-level `modelPath` and relied on each `SceneNode` having a *global* `meshIndex`. This was a latent bug: any scene with more than one imported model would silently fail to load correctly, because the second and subsequent models were never loaded at all. Objects would appear in the scene hierarchy (since the graph was deserialized) but would not render (since their mesh indices were out of bounds). The fix requires two things:
    1.  **`Mesh` carries its origin:** Add `sourcePath` (the glTF/GLB file the mesh was loaded from) and `sourceMeshIndex` (the mesh's local index *within that file*) to the `Mesh` struct. These are set in `Renderer::load_scene` and `Renderer::import_gltf` immediately after loading.
    2.  **`SceneNode` mirrors this for serialization:** Add `modelPath` and `meshIndexInModel` to `SceneNode`. The scene file serializes these per-node fields instead of the global `meshIndex`. On load, `do_load_scene` lazily loads each unique `modelPath` via `import_gltf`, tracks the mesh offset for each model, and computes the correct global `meshIndex` for every node.

-   **Backward Compatibility via Fallback:** The loader can support old-format scene files (single root `modelPath`, global `meshIndex` per node) by detecting the absence of the new per-node `modelPath` field and falling back to the root value. This prevents hard breaks when loading scenes saved before the format change.

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
