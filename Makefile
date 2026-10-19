# vulkanwork build driver.
#
#   make [build|run|rebuild|clean|package|help] [COMPILER=...] [VS=22|26]
#        [CONFIG=Debug|Release|RelWithDebInfo|MinSizeRel] [TRIPLET=...]
#
# Windows: COMPILER=msvc (default) or clang. Clang uses Ninja + clang-cl, or
#          the VS ClangCL toolset when VS= is given. Recipes run in PowerShell.
# Linux:   COMPILER=gcc (default) or clang. Recipes run in /bin/sh.

ifndef VCPKG_ROOT
  $(error VCPKG_ROOT is not set. Please set it to your vcpkg installation directory.)
endif

CONFIG   ?= Debug
DIST_DIR := dist

ifeq ($(OS),Windows_NT)
  SHELL       := powershell.exe
  .SHELLFLAGS := -NoProfile -Command
  COMPILER    ?= msvc
  TRIPLET     ?= x64-windows
  EXE         := vulkanwork.exe

  # Optional: VS=22 or VS=26 to target a Visual Studio build directory
  ifdef VS
    ifeq ($(VS),22)
      BUILD_DIR := build/vs22
      GENERATOR := -G "Visual Studio 17 2022" -A x64
    else ifeq ($(VS),26)
      BUILD_DIR := build/vs26
      GENERATOR := -G "Visual Studio 18 2026" -A x64
    else
      $(error Unsupported VS version: $(VS). Use VS=22 or VS=26)
    endif
  else
    BUILD_DIR := build/generic
    GENERATOR :=
  endif

  ifeq ($(COMPILER),msvc)
    COMPILER_FLAGS :=
  else ifeq ($(COMPILER),clang)
    ifdef VS
      # Needs the "C++ Clang tools for Windows" VS component.
      BUILD_DIR      := $(BUILD_DIR)-clang
      COMPILER_FLAGS := -T ClangCL
    else
      # Ninja + clang-cl from PATH. Falls back to the VS-bundled Ninja.
      NINJA ?= $(shell $$n = (Get-Command ninja -ErrorAction SilentlyContinue).Source; if (-not $$n) { $$vw = Join-Path $${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'; if (Test-Path $$vw) { $$n = & $$vw -latest -prerelease -products * -find '**/Ninja/ninja.exe' | Select-Object -First 1 } }; $$n)
      ifeq ($(strip $(NINJA)),)
        $(error Ninja not found. Add it to PATH or pass NINJA=path/to/ninja.exe)
      endif
      BUILD_BASE     := build/clang
      BUILD_DIR      := $(BUILD_BASE)-$(CONFIG)
      GENERATOR      := -G Ninja "-DCMAKE_MAKE_PROGRAM=$(NINJA)"
      COMPILER_FLAGS := -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
    endif
  else ifeq ($(COMPILER),gcc)
    $(error COMPILER=gcc is supported on Linux only. Use msvc or clang on Windows)
  else
    $(error Unsupported COMPILER: $(COMPILER). Use msvc or clang)
  endif
else
  SHELL    := /bin/sh
  COMPILER ?= gcc
  TRIPLET  ?= x64-linux
  EXE      := vulkanwork

  ifeq ($(COMPILER),gcc)
    CC_BIN  := gcc
    CXX_BIN := g++
  else ifeq ($(COMPILER),clang)
    CC_BIN  := clang
    CXX_BIN := clang++
  else
    $(error Unsupported COMPILER: $(COMPILER). Use gcc or clang)
  endif

  # Single-config generator: keep one build tree per configuration.
  BUILD_BASE     := build/$(COMPILER)
  BUILD_DIR      := $(BUILD_BASE)-$(CONFIG)
  GENERATOR      := $(if $(shell which ninja 2>/dev/null),-G Ninja,)
  COMPILER_FLAGS := -DCMAKE_C_COMPILER=$(CC_BIN) -DCMAKE_CXX_COMPILER=$(CXX_BIN)
endif

BUILD_BASE  ?= $(BUILD_DIR)
PKG_DIR     := $(BUILD_BASE)-package
TOOLCHAIN   := $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
CMAKE_FLAGS := -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
               -DVCPKG_TARGET_TRIPLET=$(TRIPLET) \
               $(COMPILER_FLAGS)

.PHONY: all build clean rebuild run package help

all: build

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt vcpkg.json
	cmake -B $(BUILD_DIR) -S . $(GENERATOR) $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=$(CONFIG)

ifeq ($(OS),Windows_NT)

build: $(BUILD_DIR)/CMakeCache.txt
	@Write-Host '========================================'
	@Write-Host ' Build Directory : $(BUILD_DIR)'
	@Write-Host ' Generator       : $(or $(GENERATOR),default)'
	@Write-Host ' Compiler        : $(COMPILER)'
	@Write-Host ' Config          : $(CONFIG)'
	@Write-Host ' Triplet         : $(TRIPLET)'
	@Write-Host '========================================'
	@$$sw = [System.Diagnostics.Stopwatch]::StartNew(); cmake --build $(BUILD_DIR) --config $(CONFIG) --verbose; $$rc = $$LASTEXITCODE; $$sw.Stop(); Write-Host ''; Write-Host '========================================'; Write-Host ' BUILD REPORT'; Write-Host '========================================'; $$cache = '$(BUILD_DIR)/CMakeCache.txt'; $$compiler = ((Select-String -Pattern 'CMAKE_CXX_COMPILER:' -Path $$cache).Line -split '=',2)[1]; $$gen = ((Select-String -Pattern 'CMAKE_GENERATOR:' -Path $$cache).Line -split '=',2)[1]; Write-Host (' Compiler  : ' + $$compiler); Write-Host (' Generator : ' + $$gen); Write-Host (' Duration  : ' + [math]::Round($$sw.Elapsed.TotalSeconds,1).ToString() + 's'); $$exe = Get-Item -Path '$(BUILD_DIR)/$(CONFIG)/$(EXE)','$(BUILD_DIR)/$(EXE)' -ErrorAction SilentlyContinue | Select-Object -First 1; if ($$exe) { Write-Host (' Executable: ' + $$exe.FullName + ' (' + $$exe.Length + ' bytes)') } else { Write-Host ' Executable: not found' }; Write-Host '========================================'; exit $$rc

clean:
	if (Test-Path '$(BUILD_DIR)') { Get-ChildItem -Path '$(BUILD_DIR)' -Exclude '.vs' | Remove-Item -Recurse -Force }
	if (Test-Path '$(PKG_DIR)') { Remove-Item -Path '$(PKG_DIR)' -Recurse -Force }
	if (Test-Path '$(DIST_DIR)') { Remove-Item -Path '$(DIST_DIR)' -Recurse -Force }

run: build
	@if (Test-Path '$(BUILD_DIR)/$(CONFIG)/$(EXE)') { & './$(BUILD_DIR)/$(CONFIG)/$(EXE)' } else { & './$(BUILD_DIR)/$(EXE)' }

else

build: $(BUILD_DIR)/CMakeCache.txt
	@echo '========================================'
	@echo ' Build Directory : $(BUILD_DIR)'
	@echo ' Generator       : $(or $(GENERATOR),default)'
	@echo ' Compiler        : $(COMPILER)'
	@echo ' Config          : $(CONFIG)'
	@echo ' Triplet         : $(TRIPLET)'
	@echo '========================================'
	@start=$$(date +%s); \
	cmake --build $(BUILD_DIR) --config $(CONFIG) --verbose; rc=$$?; \
	end=$$(date +%s); cache='$(BUILD_DIR)/CMakeCache.txt'; \
	echo ''; \
	echo '========================================'; \
	echo ' BUILD REPORT'; \
	echo '========================================'; \
	echo " Compiler  : $$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$$cache")"; \
	echo " Generator : $$(sed -n 's/^CMAKE_GENERATOR:[^=]*=//p' "$$cache")"; \
	echo " Duration  : $$((end - start))s"; \
	exe='$(BUILD_DIR)/$(EXE)'; \
	if [ -f "$$exe" ]; then \
		echo " Executable: $$(cd "$$(dirname "$$exe")" && pwd)/$(EXE) ($$(wc -c < "$$exe" | tr -d ' ') bytes)"; \
	else \
		echo ' Executable: not found'; \
	fi; \
	echo '========================================'; \
	exit $$rc

clean:
	rm -rf '$(BUILD_DIR)' '$(PKG_DIR)' '$(DIST_DIR)'

run: build
	./$(BUILD_DIR)/$(EXE)

endif

rebuild: clean build

# Relocatable Release archive in dist/ (ZIP on Windows, TGZ on Linux).
package:
	cmake -B $(PKG_DIR) -S . $(GENERATOR) $(CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release -DVW_PORTABLE=ON
	cmake --build $(PKG_DIR) --config Release
	cpack --config $(PKG_DIR)/CPackConfig.cmake -C Release -B $(DIST_DIR)
	cmake -E rm -rf $(DIST_DIR)/_CPack_Packages

help:
	@echo 'Targets: build run rebuild clean package help'
	@echo 'Options: COMPILER=$(COMPILER) CONFIG=$(CONFIG) TRIPLET=$(TRIPLET) VS=22|26 (Windows)'
	@echo 'Build dir: $(BUILD_DIR)  Package dir: $(PKG_DIR)'
