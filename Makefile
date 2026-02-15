BUILD_DIR   := build
VS22_DIR    := vs22
VS26_DIR    := vs26
VCPKG_ROOT  ?= D:/vcpkg
TRIPLET     := x64-windows
TOOLCHAIN   := $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
CMAKE_FLAGS := -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
               -DVCPKG_TARGET_TRIPLET=$(TRIPLET)

.PHONY: all build clean rebuild run vs22 vs26

all: build

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt vcpkg.json
	cmake -B $(BUILD_DIR) -S . $(CMAKE_FLAGS)

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR)

clean:
	cmake --build $(BUILD_DIR) --target clean

rebuild: clean build

run: build
	./$(BUILD_DIR)/Debug/vulkanwork.exe || ./$(BUILD_DIR)/vulkanwork.exe

vs22:
	cmake -B $(VS22_DIR) -S . -G "Visual Studio 17 2022" -A x64 $(CMAKE_FLAGS)

vs26:
	cmake -B $(VS26_DIR) -S . -G "Visual Studio 18 2026" -A x64 $(CMAKE_FLAGS)
