BUI 0.5.0 C/C++ SDK
Platform: Windows x64
Compiler: MSVC 19.51.36260.0
Configuration: RelWithDebInfo

Set CMAKE_PREFIX_PATH to this directory, then use:
  find_package(BUI CONFIG REQUIRED)
  target_link_libraries(myapp PRIVATE bui::kit)
Other targets: bui::core, bui::gpu, bui::api; bui::imgui and bui::cimgui when enabled.

Use a compatible compiler, target architecture and runtime configuration.
MSVC compiler PDBs are packaged beside the .lib files, including symbols
for embedded object libraries. Keep these files together when linking.
Your application's linker generates the application PDB on Windows.

License and third-party notices: share/licenses/BUI/.
