cmake_minimum_required(VERSION 3.20)
project(vulkanwork LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Packages ----------------------------------------------------------------
find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
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
set(PAK_FILE_PATH "${CMAKE_BINARY_DIR}/assets.pak")
set(MODEL_PATH "${CMAKE_SOURCE_DIR}/models")
configure_file(src/config.h.in ${CMAKE_BINARY_DIR}/generated/config.h @ONLY)

# --- Executable --------------------------------------------------------------
file(GLOB_RECURSE SRC_FILES "src/*.cpp")
file(GLOB_RECURSE HEADER_FILES "src/*.h" "src/*.hpp")

source_group(TREE ${CMAKE_SOURCE_DIR}/src PREFIX "Source Files" FILES ${SRC_FILES})
source_group(TREE ${CMAKE_SOURCE_DIR}/src PREFIX "Header Files" FILES ${HEADER_FILES})

add_executable(vulkanwork ${SRC_FILES} ${HEADER_FILES})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT vulkanwork)

add_dependencies(vulkanwork shaders)

target_include_directories(vulkanwork PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${Stb_INCLUDE_DIR}
    ${TINYGLTF_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/vendor/ImGuiFileDialog
)

target_link_libraries(vulkanwork PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    imgui::imgui
    imguizmo::imguizmo
    nlohmann_json::nlohmann_json
    packfile
)

# --- Pack file library -------------------------------------------------------
add_library(packfile STATIC src/pak/packfile.cpp)
target_include_directories(packfile PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(packfile PUBLIC lz4::lz4)

# --- Packer tool -------------------------------------------------------------
add_executable(pak_packer tools/packer/main.cpp)
target_link_libraries(pak_packer PRIVATE packfile)

# --- Pack assets after shaders compile ---------------------------------------
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/assets.pak
    COMMAND pak_packer -o ${CMAKE_BINARY_DIR}/assets.pak
        shaders/cube.vert.spv=${SHADER_BIN_DIR}/cube.vert.spv
        shaders/cube.frag.spv=${SHADER_BIN_DIR}/cube.frag.spv
        shaders/pbr.vert.spv=${SHADER_BIN_DIR}/pbr.vert.spv
        shaders/pbr.frag.spv=${SHADER_BIN_DIR}/pbr.frag.spv
        shaders/light_cull.comp.spv=${SHADER_BIN_DIR}/light_cull.comp.spv
        shaders/debug_heatmap.vert.spv=${SHADER_BIN_DIR}/debug_heatmap.vert.spv
        shaders/debug_heatmap.frag.spv=${SHADER_BIN_DIR}/debug_heatmap.frag.spv
        shaders/debug_lines.vert.spv=${SHADER_BIN_DIR}/debug_lines.vert.spv
        shaders/debug_lines.frag.spv=${SHADER_BIN_DIR}/debug_lines.frag.spv
        textures/grids/1024/BlueGrid.png=${CMAKE_SOURCE_DIR}/textures/grids/1024/BlueGrid.png
    COMMAND pak_packer -v ${CMAKE_BINARY_DIR}/assets.pak
    DEPENDS shaders pak_packer
            ${SPV_OUTPUTS}
            ${CMAKE_SOURCE_DIR}/textures/grids/1024/BlueGrid.png
    COMMENT "Packing and validating assets.pak"
)
add_custom_target(pack_assets DEPENDS ${CMAKE_BINARY_DIR}/assets.pak)
add_dependencies(vulkanwork pack_assets)
