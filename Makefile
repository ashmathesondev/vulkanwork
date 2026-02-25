SHELL       := powershell.exe
.SHELLFLAGS := -NoProfile -Command
VCPKG_ROOT  ?= E:/vcpkg
TRIPLET     := x64-windows
TOOLCHAIN   := $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
CMAKE_FLAGS := -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
               -DVCPKG_TARGET_TRIPLET=$(TRIPLET)

# Optional: VS=22 or VS=26 to target a Visual Studio build directory
ifdef VS
  ifeq ($(VS),22)
    BUILD_DIR    := build/vs22
    GENERATOR    := -G "Visual Studio 17 2022" -A x64
  else ifeq ($(VS),26)
    BUILD_DIR    := build/vs26
    GENERATOR    := -G "Visual Studio 18 2026" -A x64
  else
    $(error Unsupported VS version: $(VS). Use VS=22 or VS=26)
  endif
else
  BUILD_DIR    := build/generic
  GENERATOR    :=
endif

.PHONY: all build clean rebuild run

all: build

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt vcpkg.json
	cmake -B $(BUILD_DIR) -S . $(GENERATOR) $(CMAKE_FLAGS)

build: $(BUILD_DIR)/CMakeCache.txt
	@Write-Host '========================================'
	@Write-Host ' Build Directory : $(BUILD_DIR)'
	@Write-Host ' Generator       : $(or $(GENERATOR),default)'
	@Write-Host ' Triplet         : $(TRIPLET)'
	@Write-Host '========================================'
	@$$sw = [System.Diagnostics.Stopwatch]::StartNew(); cmake --build $(BUILD_DIR) --verbose; $$rc = $$LASTEXITCODE; $$sw.Stop(); Write-Host ''; Write-Host '========================================'; Write-Host ' BUILD REPORT'; Write-Host '========================================'; $$cache = '$(BUILD_DIR)/CMakeCache.txt'; $$compiler = ((Select-String -Pattern 'CMAKE_CXX_COMPILER:' -Path $$cache).Line -split '=',2)[1]; $$gen = ((Select-String -Pattern 'CMAKE_GENERATOR:' -Path $$cache).Line -split '=',2)[1]; Write-Host (' Compiler  : ' + $$compiler); Write-Host (' Generator : ' + $$gen); Write-Host (' Duration  : ' + [math]::Round($$sw.Elapsed.TotalSeconds,1).ToString() + 's'); $$exe = Get-ChildItem -Path '$(BUILD_DIR)' -Filter 'vulkanwork.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1; if ($$exe) { Write-Host (' Executable: ' + $$exe.FullName + ' (' + $$exe.Length + ' bytes)') } else { Write-Host ' Executable: not found' }; Write-Host '========================================'; exit $$rc

clean:
	cmake --build $(BUILD_DIR) --target clean

rebuild: clean build

run: build
	@if (Test-Path '$(BUILD_DIR)/Debug/vulkanwork.exe') { & './$(BUILD_DIR)/Debug/vulkanwork.exe' } else { & './$(BUILD_DIR)/vulkanwork.exe' }
