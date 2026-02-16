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
configure_file(src/config.h.in ${CMAKE_BINARY_DIR}/generated/config.h @ONLY)

# --- Executable --------------------------------------------------------------
add_executable(vulkanwork
    src/main.cpp
    src/app.cpp
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT vulkanwork)

add_dependencies(vulkanwork shaders)

target_include_directories(vulkanwork PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${Stb_INCLUDE_DIR}
)

target_link_libraries(vulkanwork PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    imgui::imgui
)
