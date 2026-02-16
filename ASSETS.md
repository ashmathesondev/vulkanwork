# Asset Pack System

At build time, shaders and textures are compressed and bundled into a single `assets.pak` file. At runtime the application reads all assets from this pack instead of loading loose files from disk.

## Tech Stack

| Component | Role |
|---|---|
| [LZ4](https://github.com/lz4/lz4) | Per-entry compression / decompression |
| `pak_packer` (custom CLI tool) | Builds `.pak` files from source assets |
| `pak::PackFile` (C++ library) | Reads and decompresses entries at runtime |

LZ4 was chosen for its very fast decompression speed, which keeps application startup snappy while still providing meaningful size reduction for textures.

## Build Pipeline

CMake orchestrates the full pipeline automatically:

1. **Compile shaders** — `glslc` compiles `.vert` / `.frag` into SPIR-V `.spv` files
2. **Pack assets** — `pak_packer` bundles the compiled shaders and source textures into `build/<preset>/assets.pak`
3. **Build executable** — the app links against the `packfile` static library and reads from `assets.pak` at runtime

A rebuild of `assets.pak` is triggered whenever a shader source file or a referenced texture changes.

## Binary Format (`.pak` v1)

```
Offset 0                : FileHeader       (24 bytes)
Offset 24               : TocEntry[0..N-1] (280 bytes each)
Offset 24 + N*280       : compressed data blobs (back to back)
```

**FileHeader** (24 bytes):

| Field | Type | Description |
|---|---|---|
| `magic` | `uint32` | `0x50414B31` ("PAK1") |
| `version` | `uint32` | Format version (currently 1) |
| `entry_count` | `uint32` | Number of TOC entries |
| `flags` | `uint32` | Reserved, always 0 |
| `toc_offset` | `uint64` | Byte offset to the TOC (always 24 for v1) |

**TocEntry** (280 bytes):

| Field | Type | Description |
|---|---|---|
| `name` | `char[256]` | Null-terminated asset name, forward slashes |
| `data_offset` | `uint64` | Byte offset from start of file to compressed data |
| `compressed_size` | `uint64` | Size of the LZ4-compressed blob |
| `original_size` | `uint64` | Size after decompression |

The structs are defined in `src/pak_format.h` and shared between the packer tool and the runtime reader.

## Packer CLI (`pak_packer`)

The `pak_packer` tool has three modes: pack, list, and validate.

### Pack mode (default)

```
pak_packer -o <output.pak> [options] [entries...]
```

| Flag | Description |
|---|---|
| `-o <path>` | Output `.pak` file path (required) |
| `-b <path>` | Base directory for computing relative asset names (default: cwd) |

Each positional argument is an entry to pack. Two forms are supported:

- **`<name>=<filepath>`** — pack the file at `<filepath>` under the explicit asset name `<name>`
- **`<filepath>`** — pack the file and derive the asset name relative to the `-b` base directory

Example:

```powershell
pak_packer -o build/assets.pak `
    shaders/cube.vert.spv=build/shaders/cube.vert.spv `
    shaders/cube.frag.spv=build/shaders/cube.frag.spv `
    textures/grids/1024/BlueGrid.png=textures/grids/1024/BlueGrid.png
```

### List mode (`-l`)

Reads a `.pak` file, validates the header, and prints every entry with its original size, compressed size, and compression ratio.

```
pak_packer -l <file.pak>
```

Example output:

```
PAK1 v1 — 3 entries

  Name                                         Original   Compressed  Ratio
  ----                                         --------   ----------  -----
  shaders/cube.vert.spv                          1840 B       1206 B  65.5%
  shaders/cube.frag.spv                           868 B        614 B  70.7%
  textures/grids/1024/BlueGrid.png              44215 B      40268 B  91.1%

  TOTAL                                         46923 B      42088 B  89.7%
```

### Validate mode (`-v`)

Reads a `.pak` file, validates the header, and test-decompresses every entry to confirm the data is intact. Returns a non-zero exit code on failure, making it suitable for use as a build validation step.

```
pak_packer -v <file.pak>
```

Example output:

```
Header OK (PAK1 v1, 3 entries)
  OK: shaders/cube.vert.spv
  OK: shaders/cube.frag.spv
  OK: textures/grids/1024/BlueGrid.png

All 3 entries validated OK
```

Validation checks:
- Magic number and version in the header
- Each entry's data range fits within the file
- LZ4 decompression succeeds for every entry
- Decompressed size matches the declared `original_size`

### Build integration

CMake automatically runs both pack and validate as part of the `pack_assets` target. If validation fails, the build stops with an error before compiling the main executable.

## Runtime Reader (`pak::PackFile`)

The reader API in `src/packfile.h`:

```cpp
namespace pak {
class PackFile {
public:
    explicit PackFile(const std::filesystem::path& pak_path);
    bool              contains(std::string_view name) const;
    std::vector<char> read(std::string_view name) const;
    size_t            original_size(std::string_view name) const;
    std::vector<std::string> list_assets() const;
};
}
```

- The constructor validates the header and loads the TOC into memory
- `read()` opens the file, seeks to the entry's data offset, and decompresses via `LZ4_decompress_safe()`
- Asset names use forward slashes (e.g. `shaders/cube.vert.spv`, `textures/grids/1024/BlueGrid.png`)

## Current Pack Contents

| Asset Name | Source |
|---|---|
| `shaders/cube.vert.spv` | Compiled vertex shader |
| `shaders/cube.frag.spv` | Compiled fragment shader |
| `textures/grids/1024/BlueGrid.png` | Cube face texture |
