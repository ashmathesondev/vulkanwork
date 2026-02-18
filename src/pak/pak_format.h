#pragma once

#include <cstddef>
#include <cstdint>

namespace pak
{

inline constexpr uint32_t MAGIC = 0x50414B31;  // "PAK1"
inline constexpr uint32_t VERSION = 1;
inline constexpr size_t MAX_ASSET_NAME = 256;

struct FileHeader
{
	uint32_t magic;	   // MAGIC
	uint32_t version;  // VERSION
	uint32_t entry_count;
	uint32_t flags;		  // reserved, 0
	uint64_t toc_offset;  // always 24 for v1
};
static_assert(sizeof(FileHeader) == 24);

struct TocEntry
{
	char name[MAX_ASSET_NAME];	// null-terminated, forward slashes
	uint64_t data_offset;		// from start of file
	uint64_t compressed_size;
	uint64_t original_size;
};
static_assert(sizeof(TocEntry) == 280);

}  // namespace pak
