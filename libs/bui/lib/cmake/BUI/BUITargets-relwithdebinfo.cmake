#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "bui::core" for configuration "RelWithDebInfo"
set_property(TARGET bui::core APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(bui::core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C;CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/bui_core.lib"
  )

list(APPEND _cmake_import_check_targets bui::core )
list(APPEND _cmake_import_check_files_for_bui::core "${_IMPORT_PREFIX}/lib/bui_core.lib" )

# Import target "bui::gpu" for configuration "RelWithDebInfo"
set_property(TARGET bui::gpu APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(bui::gpu PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/bui_gpu.lib"
  )

list(APPEND _cmake_import_check_targets bui::gpu )
list(APPEND _cmake_import_check_files_for_bui::gpu "${_IMPORT_PREFIX}/lib/bui_gpu.lib" )

# Import target "bui::kit" for configuration "RelWithDebInfo"
set_property(TARGET bui::kit APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(bui::kit PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C;CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/bui_kit.lib"
  )

list(APPEND _cmake_import_check_targets bui::kit )
list(APPEND _cmake_import_check_files_for_bui::kit "${_IMPORT_PREFIX}/lib/bui_kit.lib" )

# Import target "bui::api" for configuration "RelWithDebInfo"
set_property(TARGET bui::api APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(bui::api PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/bui_api.lib"
  )

list(APPEND _cmake_import_check_targets bui::api )
list(APPEND _cmake_import_check_files_for_bui::api "${_IMPORT_PREFIX}/lib/bui_api.lib" )

# Import target "bui::imgui" for configuration "RelWithDebInfo"
set_property(TARGET bui::imgui APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(bui::imgui PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/bui_imgui.lib"
  )

list(APPEND _cmake_import_check_targets bui::imgui )
list(APPEND _cmake_import_check_files_for_bui::imgui "${_IMPORT_PREFIX}/lib/bui_imgui.lib" )

# Import target "bui::cimgui" for configuration "RelWithDebInfo"
set_property(TARGET bui::cimgui APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(bui::cimgui PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/bui_cimgui.lib"
  )

list(APPEND _cmake_import_check_targets bui::cimgui )
list(APPEND _cmake_import_check_files_for_bui::cimgui "${_IMPORT_PREFIX}/lib/bui_cimgui.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
