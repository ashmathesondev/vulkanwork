cmake_minimum_required(VERSION 3.20)
project(vulkanwork LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Packages ----------------------------------------------------------------
find_package(Vulkan REQUIRED)
if(NOT ANDROID)
    find_package(glfw3 CONFIG REQUIRED)
endif()
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_path(Stb_INCLUDE_DIR "stb_image.h")
find_package(lz4 CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
find_package(imguizmo CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# --- Shader compilation ------------------------------------------------------
if(Vulkan_GLSLC_EXECUTABLE)
    set(GLSLC ${Vulkan_GLSLC_EXECUTABLE})
else()
    find_program(GLSLC glslc REQUIRED)
endif()

set(SHADER_SRC_DIR  ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR  ${CMAKE_BINARY_DIR}/shaders)

set(SHADERS
    ${SHADER_SRC_DIR}/cube.vert
    ${SHADER_SRC_DIR}/cube.frag
    ${SHADER_SRC_DIR}/pbr.vert
    ${SHADER_SRC_DIR}/pbr.frag
    ${SHADER_SRC_DIR}/light_cull.comp
    ${SHADER_SRC_DIR}/debug_heatmap.vert
    ${SHADER_SRC_DIR}/debug_heatmap.frag
    ${SHADER_SRC_DIR}/debug_lines.vert
    ${SHADER_SRC_DIR}/debug_lines.frag
    ${SHADER_SRC_DIR}/gsplat_preprocess.comp
    ${SHADER_SRC_DIR}/gsplat_sort_hist.comp
    ${SHADER_SRC_DIR}/gsplat_sort_prefix.comp
    ${SHADER_SRC_DIR}/gsplat_sort_scatter.comp
    ${SHADER_SRC_DIR}/gsplat.vert
    ${SHADER_SRC_DIR}/gsplat.frag
)

foreach(SHADER ${SHADERS})
    get_filename_component(NAME ${SHADER} NAME)
    set(SPV ${SHADER_BIN_DIR}/${NAME}.spv)
    add_custom_command(
        OUTPUT  ${SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
        COMMAND ${GLSLC} ${SHADER} -o ${SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${NAME}"
    )
    list(APPEND SPV_OUTPUTS ${SPV})
endforeach()

add_custom_target(shaders DEPENDS ${SPV_OUTPUTS})

# --- Configure shader path ---------------------------------------------------
set(SHADER_OUTPUT_PATH "${SHADER_BIN_DIR}")
set(TEXTURE_PATH "${CMAKE_SOURCE_DIR}/textures")
if(ANDROID)
    set(ANDROID_ASSET_OUTPUT_DIR
        "${CMAKE_BINARY_DIR}/android_assets"
        CACHE PATH "Directory staged as Android APK assets"
    )
    set(PAK_FILE_PATH "${ANDROID_ASSET_OUTPUT_DIR}/assets.pak")
else()
    set(PAK_FILE_PATH "${CMAKE_BINARY_DIR}/assets.pak")
endif()
get_filename_component(PAK_FILE_DIR "${PAK_FILE_PATH}" DIRECTORY)
set(MODEL_PATH "${CMAKE_SOURCE_DIR}/models")
configure_file(src/config.h.in ${CMAKE_BINARY_DIR}/generated/config.h @ONLY)

# --- Executable --------------------------------------------------------------
file(GLOB_RECURSE SRC_FILES "src/*.cpp")
file(GLOB_RECURSE HEADER_FILES "src/*.h" "src/*.hpp")

source_group(TREE ${CMAKE_SOURCE_DIR}/src PREFIX "Source Files" FILES ${SRC_FILES})
source_group(TREE ${CMAKE_SOURCE_DIR}/src PREFIX "Header Files" FILES ${HEADER_FILES})

if(ANDROID)
    list(APPEND SRC_FILES
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    )
    add_library(vulkanwork SHARED ${SRC_FILES} ${HEADER_FILES})
else()
    add_executable(vulkanwork ${SRC_FILES} ${HEADER_FILES})
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT vulkanwork)

add_dependencies(vulkanwork shaders)

target_include_directories(vulkanwork PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${Stb_INCLUDE_DIR}
    ${TINYGLTF_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/vendor/ImGuiFileDialog
)

if(ANDROID)
    target_include_directories(vulkanwork PRIVATE
        ${ANDROID_NDK}/sources/android/native_app_glue
    )
endif()

target_link_libraries(vulkanwork PRIVATE
    Vulkan::Vulkan
    glm::glm
    imgui::imgui
    imguizmo::imguizmo
    nlohmann_json::nlohmann_json
    packfile
)

if(NOT ANDROID)
    target_link_libraries(vulkanwork PRIVATE glfw)
else()
    target_link_libraries(vulkanwork PRIVATE android log)
endif()

# --- Pack file library -------------------------------------------------------
add_library(packfile STATIC src/pak/packfile.cpp)
set_target_properties(packfile PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(packfile PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(packfile PUBLIC lz4::lz4)

# --- Packer tool -------------------------------------------------------------
if(NOT ANDROID)
    add_executable(pak_packer tools/packer/main.cpp)
    target_link_libraries(pak_packer PRIVATE packfile)
    set(PAK_PACKER_COMMAND pak_packer)
    set(PAK_PACKER_TARGET_DEPENDS pak_packer)
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(PAK_PACKER_COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/packer/pack_uncompressed.py
    )
    set(PAK_PACKER_TARGET_DEPENDS)
endif()

set(PAK_ASSET_ARGS
    shaders/cube.vert.spv=${SHADER_BIN_DIR}/cube.vert.spv
    shaders/cube.frag.spv=${SHADER_BIN_DIR}/cube.frag.spv
    shaders/pbr.vert.spv=${SHADER_BIN_DIR}/pbr.vert.spv
    shaders/pbr.frag.spv=${SHADER_BIN_DIR}/pbr.frag.spv
    shaders/light_cull.comp.spv=${SHADER_BIN_DIR}/light_cull.comp.spv
    shaders/debug_heatmap.vert.spv=${SHADER_BIN_DIR}/debug_heatmap.vert.spv
    shaders/debug_heatmap.frag.spv=${SHADER_BIN_DIR}/debug_heatmap.frag.spv
    shaders/debug_lines.vert.spv=${SHADER_BIN_DIR}/debug_lines.vert.spv
    shaders/debug_lines.frag.spv=${SHADER_BIN_DIR}/debug_lines.frag.spv
    shaders/gsplat_preprocess.comp.spv=${SHADER_BIN_DIR}/gsplat_preprocess.comp.spv
    shaders/gsplat_sort_hist.comp.spv=${SHADER_BIN_DIR}/gsplat_sort_hist.comp.spv
    shaders/gsplat_sort_prefix.comp.spv=${SHADER_BIN_DIR}/gsplat_sort_prefix.comp.spv
    shaders/gsplat_sort_scatter.comp.spv=${SHADER_BIN_DIR}/gsplat_sort_scatter.comp.spv
    shaders/gsplat.vert.spv=${SHADER_BIN_DIR}/gsplat.vert.spv
    shaders/gsplat.frag.spv=${SHADER_BIN_DIR}/gsplat.frag.spv
    textures/grids/1024/BlueGrid.png=${CMAKE_SOURCE_DIR}/textures/grids/1024/BlueGrid.png
)

# --- Pack assets after shaders compile ---------------------------------------
if(ANDROID)
    add_custom_command(
        OUTPUT ${PAK_FILE_PATH}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PAK_FILE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ANDROID_ASSET_OUTPUT_DIR}/models
        COMMAND ${PAK_PACKER_COMMAND} -o ${PAK_FILE_PATH} ${PAK_ASSET_ARGS}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/models/DamagedHelmet.glb
                ${ANDROID_ASSET_OUTPUT_DIR}/models/DamagedHelmet.glb
        DEPENDS shaders ${PAK_PACKER_TARGET_DEPENDS}
                ${SPV_OUTPUTS}
                ${CMAKE_SOURCE_DIR}/textures/grids/1024/BlueGrid.png
                ${CMAKE_SOURCE_DIR}/models/DamagedHelmet.glb
        COMMENT "Packing Android assets"
    )
else()
    add_custom_command(
        OUTPUT ${PAK_FILE_PATH}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PAK_FILE_DIR}
        COMMAND ${PAK_PACKER_COMMAND} -o ${PAK_FILE_PATH} ${PAK_ASSET_ARGS}
        COMMAND ${PAK_PACKER_COMMAND} -v ${PAK_FILE_PATH}
        DEPENDS shaders ${PAK_PACKER_TARGET_DEPENDS}
                ${SPV_OUTPUTS}
                ${CMAKE_SOURCE_DIR}/textures/grids/1024/BlueGrid.png
        COMMENT "Packing and validating assets.pak"
    )
endif()
add_custom_target(pack_assets DEPENDS ${PAK_FILE_PATH})
add_dependencies(vulkanwork pack_assets)
