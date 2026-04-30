#include "packfile.h"

#include <lz4.h>

#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace pak
{

namespace
{

uint64_t get_file_size(std::ifstream& f, const std::filesystem::path& path)
{
	f.seekg(0, std::ios::end);
	std::streampos end = f.tellg();
	if (end < 0)
		throw std::runtime_error("Cannot determine pack file size: " +
								 path.string());
	f.seekg(0, std::ios::beg);
	return static_cast<uint64_t>(end);
}

bool range_exceeds_file(uint64_t offset, uint64_t size, uint64_t fileSize)
{
	return offset > fileSize || size > fileSize - offset;
}

std::streamoff checked_streamoff(uint64_t offset,
								 const std::filesystem::path& path)
{
	if (offset >
		static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
		throw std::runtime_error("Pack offset exceeds stream limit: " +
								 path.string());
	return static_cast<std::streamoff>(offset);
}

int checked_lz4_size(uint64_t size, std::string_view assetName)
{
	if (size > static_cast<uint64_t>(std::numeric_limits<int>::max()))
		throw std::runtime_error("Asset too large for LZ4 API: " +
								 std::string(assetName));
	return static_cast<int>(size);
}

}  // namespace

PackFile::PackFile(const std::filesystem::path& pak_path) : path_(pak_path)
{
	std::ifstream f(path_, std::ios::binary);
	if (!f.is_open())
		throw std::runtime_error("Cannot open pack file: " + path_.string());

	uint64_t fileSize = get_file_size(f, path_);
	if (fileSize < sizeof(FileHeader))
		throw std::runtime_error("Pack file is too small: " + path_.string());

	FileHeader header{};
	f.read(reinterpret_cast<char*>(&header), sizeof(header));
	if (!f)
		throw std::runtime_error("Failed to read pack header: " +
								 path_.string());

	if (header.magic != MAGIC)
		throw std::runtime_error("Invalid pack magic in: " + path_.string());
	if (header.version != VERSION)
		throw std::runtime_error("Unsupported pack version in: " +
								 path_.string());

	uint64_t tocSize =
		static_cast<uint64_t>(header.entry_count) * sizeof(TocEntry);
	if (header.toc_offset < sizeof(FileHeader) ||
		range_exceeds_file(header.toc_offset, tocSize, fileSize))
		throw std::runtime_error("Invalid pack TOC range in: " +
								 path_.string());

	f.seekg(checked_streamoff(header.toc_offset, path_));

	for (uint32_t i = 0; i < header.entry_count; ++i)
	{
		TocEntry entry{};
		f.read(reinterpret_cast<char*>(&entry), sizeof(entry));
		if (!f)
			throw std::runtime_error("Failed to read TOC entry " +
									 std::to_string(i));

		if (std::memchr(entry.name, '\0', MAX_ASSET_NAME) == nullptr)
			throw std::runtime_error("Unterminated asset name in TOC entry " +
									 std::to_string(i));

		std::string name(entry.name);
		if (name.empty())
			throw std::runtime_error("Empty asset name in TOC entry " +
									 std::to_string(i));
		if (range_exceeds_file(entry.data_offset, entry.compressed_size,
							   fileSize))
			throw std::runtime_error("Asset data range exceeds pack file: " +
									 name);
		checked_lz4_size(entry.compressed_size, name);
		checked_lz4_size(entry.original_size, name);

		if (!toc_.emplace(std::move(name), entry).second)
			throw std::runtime_error("Duplicate asset name in pack: " +
									 std::string(entry.name));
	}
}

bool PackFile::contains(std::string_view name) const
{
	return toc_.count(std::string(name)) > 0;
}

std::vector<char> PackFile::read(std::string_view name) const
{
	auto it = toc_.find(std::string(name));
	if (it == toc_.end())
		throw std::runtime_error("Asset not found in pack: " +
								 std::string(name));

	const auto& entry = it->second;

	std::ifstream f(path_, std::ios::binary);
	if (!f.is_open())
		throw std::runtime_error("Cannot open pack file: " + path_.string());

	f.seekg(checked_streamoff(entry.data_offset, path_));

	std::vector<char> compressed(entry.compressed_size);
	f.read(compressed.data(),
		   static_cast<std::streamsize>(entry.compressed_size));
	if (!f)
		throw std::runtime_error("Failed to read asset data: " +
								 std::string(name));

	std::vector<char> decompressed(entry.original_size);
	int expectedSize = checked_lz4_size(entry.original_size, name);
	int result = LZ4_decompress_safe(
		compressed.data(), decompressed.data(),
		checked_lz4_size(entry.compressed_size, name), expectedSize);

	if (result < 0)
		throw std::runtime_error("LZ4 decompression failed for: " +
								 std::string(name));
	if (result != expectedSize)
		throw std::runtime_error("LZ4 decompressed size mismatch for: " +
								 std::string(name));

	return decompressed;
}

size_t PackFile::original_size(std::string_view name) const
{
	auto it = toc_.find(std::string(name));
	if (it == toc_.end())
		throw std::runtime_error("Asset not found in pack: " +
								 std::string(name));
	return it->second.original_size;
}

std::vector<std::string> PackFile::list_assets() const
{
	std::vector<std::string> names;
	names.reserve(toc_.size());
	for (const auto& [k, v] : toc_) names.push_back(k);
	return names;
}

}  // namespace pak
