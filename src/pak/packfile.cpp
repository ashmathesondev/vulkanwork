#include "packfile.h"

#include <lz4.h>

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace pak
{

PackFile::PackFile(const std::filesystem::path& pak_path) : path_(pak_path)
{
	std::ifstream f(path_, std::ios::binary);
	if (!f.is_open())
		throw std::runtime_error("Cannot open pack file: " + path_.string());

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

	f.seekg(static_cast<std::streamoff>(header.toc_offset));

	for (uint32_t i = 0; i < header.entry_count; ++i)
	{
		TocEntry entry{};
		f.read(reinterpret_cast<char*>(&entry), sizeof(entry));
		if (!f)
			throw std::runtime_error("Failed to read TOC entry " +
									 std::to_string(i));

		std::string name(entry.name);
		toc_.emplace(std::move(name), entry);
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

	f.seekg(static_cast<std::streamoff>(entry.data_offset));

	std::vector<char> compressed(entry.compressed_size);
	f.read(compressed.data(),
		   static_cast<std::streamsize>(entry.compressed_size));
	if (!f)
		throw std::runtime_error("Failed to read asset data: " +
								 std::string(name));

	std::vector<char> decompressed(entry.original_size);
	int result = LZ4_decompress_safe(compressed.data(), decompressed.data(),
									 static_cast<int>(entry.compressed_size),
									 static_cast<int>(entry.original_size));

	if (result < 0)
		throw std::runtime_error("LZ4 decompression failed for: " +
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
