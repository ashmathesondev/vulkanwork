VCPKG_ROOT  ?= D:/vcpkg
TRIPLET     := x64-windows
TOOLCHAIN   := $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
CMAKE_FLAGS := -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
               -DVCPKG_TARGET_TRIPLET=$(TRIPLET)

# Optional: VS=22 or VS=26 to target a Visual Studio build directory
ifdef VS
  ifeq ($(VS),22)
    BUILD_DIR    := vs22
    GENERATOR    := -G "Visual Studio 17 2022" -A x64
  else ifeq ($(VS),26)
    BUILD_DIR    := vs26
    GENERATOR    := -G "Visual Studio 18 2026" -A x64
  else
    $(error Unsupported VS version: $(VS). Use VS=22 or VS=26)
  endif
else
  BUILD_DIR    := build
  GENERATOR    :=
endif

.PHONY: all build clean rebuild run

all: build

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt vcpkg.json
	cmake -B $(BUILD_DIR) -S . $(GENERATOR) $(CMAKE_FLAGS)

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR)

clean:
	cmake --build $(BUILD_DIR) --target clean

rebuild: clean build

run: build
	./$(BUILD_DIR)/Debug/vulkanwork.exe || ./$(BUILD_DIR)/vulkanwork.exe
