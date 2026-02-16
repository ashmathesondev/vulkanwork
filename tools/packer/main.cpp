#include <lz4.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "pak_format.h"

namespace fs = std::filesystem;

struct AssetEntry
{
	std::string name;	// name inside the pack (forward slashes)
	fs::path filepath;	// path on disk
};

static std::string normalize_name(const std::string& name)
{
	std::string out = name;
	for (auto& c : out)
		if (c == '\\') c = '/';
	return out;
}

static void print_usage(const char* argv0)
{
	std::fprintf(
		stderr,
		"Usage: %s -o <output.pak> [options] [entries...]\n"
		"       %s -l <file.pak>\n"
		"       %s -v <file.pak>\n"
		"\n"
		"Modes:\n"
		"  (default)   Pack entries into a .pak file\n"
		"  -l <path>   List contents of an existing .pak file\n"
		"  -v <path>   Validate a .pak file (header + decompress every entry)\n"
		"\n"
		"Each entry is:\n"
		"  <name>=<filepath>    explicit asset name\n"
		"  or just <filepath>   name computed relative to -b base_dir\n"
		"\n"
		"Pack options:\n"
		"  -o <path>   output .pak file (required)\n"
		"  -b <path>   base directory for relative name computation (default: "
		"cwd)\n",
		argv0, argv0, argv0);
}

static int list_pak(const std::string& pak_path)
{
	std::ifstream f(pak_path, std::ios::binary);
	if (!f.is_open())
	{
		std::fprintf(stderr, "Error: cannot open '%s'\n", pak_path.c_str());
		return 1;
	}

	pak::FileHeader header{};
	f.read(reinterpret_cast<char*>(&header), sizeof(header));
	if (!f)
	{
		std::fprintf(stderr, "Error: failed to read header from '%s'\n",
					 pak_path.c_str());
		return 1;
	}

	if (header.magic != pak::MAGIC)
	{
		std::fprintf(stderr, "Error: invalid magic 0x%08X (expected 0x%08X)\n",
					 header.magic, pak::MAGIC);
		return 1;
	}
	if (header.version != pak::VERSION)
	{
		std::fprintf(stderr, "Error: unsupported version %u (expected %u)\n",
					 header.version, pak::VERSION);
		return 1;
	}

	std::printf("PAK1 v%u — %u entries\n\n", header.version,
				header.entry_count);
	std::printf("  %-40s %12s %12s %6s\n", "Name", "Original", "Compressed",
				"Ratio");
	std::printf("  %-40s %12s %12s %6s\n", "----", "--------", "----------",
				"-----");

	f.seekg(static_cast<std::streamoff>(header.toc_offset));

	uint64_t total_original = 0;
	uint64_t total_compressed = 0;

	for (uint32_t i = 0; i < header.entry_count; ++i)
	{
		pak::TocEntry entry{};
		f.read(reinterpret_cast<char*>(&entry), sizeof(entry));
		if (!f)
		{
			std::fprintf(stderr, "Error: failed to read TOC entry %u\n", i);
			return 1;
		}

		double ratio = entry.original_size > 0
						   ? 100.0 *
								 static_cast<double>(entry.compressed_size) /
								 static_cast<double>(entry.original_size)
						   : 0.0;

		std::printf("  %-40s %10llu B %10llu B %5.1f%%\n", entry.name,
					static_cast<unsigned long long>(entry.original_size),
					static_cast<unsigned long long>(entry.compressed_size),
					ratio);

		total_original += entry.original_size;
		total_compressed += entry.compressed_size;
	}

	double total_ratio = total_original > 0
							 ? 100.0 * static_cast<double>(total_compressed) /
								   static_cast<double>(total_original)
							 : 0.0;

	std::printf("\n  %-40s %10llu B %10llu B %5.1f%%\n", "TOTAL",
				static_cast<unsigned long long>(total_original),
				static_cast<unsigned long long>(total_compressed), total_ratio);

	return 0;
}

static int validate_pak(const std::string& pak_path)
{
	std::ifstream f(pak_path, std::ios::binary);
	if (!f.is_open())
	{
		std::fprintf(stderr, "FAIL: cannot open '%s'\n", pak_path.c_str());
		return 1;
	}

	pak::FileHeader header{};
	f.read(reinterpret_cast<char*>(&header), sizeof(header));
	if (!f)
	{
		std::fprintf(stderr, "FAIL: cannot read header\n");
		return 1;
	}

	if (header.magic != pak::MAGIC)
	{
		std::fprintf(stderr, "FAIL: invalid magic 0x%08X (expected 0x%08X)\n",
					 header.magic, pak::MAGIC);
		return 1;
	}
	if (header.version != pak::VERSION)
	{
		std::fprintf(stderr, "FAIL: unsupported version %u (expected %u)\n",
					 header.version, pak::VERSION);
		return 1;
	}

	std::printf("Header OK (PAK1 v%u, %u entries)\n", header.version,
				header.entry_count);

	f.seekg(static_cast<std::streamoff>(header.toc_offset));

	std::vector<pak::TocEntry> toc(header.entry_count);
	for (uint32_t i = 0; i < header.entry_count; ++i)
	{
		f.read(reinterpret_cast<char*>(&toc[i]), sizeof(pak::TocEntry));
		if (!f)
		{
			std::fprintf(stderr, "FAIL: cannot read TOC entry %u\n", i);
			return 1;
		}
	}

	// Get file size for bounds checking
	f.seekg(0, std::ios::end);
	auto file_size = static_cast<uint64_t>(f.tellg());

	int failures = 0;
	for (uint32_t i = 0; i < header.entry_count; ++i)
	{
		const auto& entry = toc[i];

		// Bounds check
		if (entry.data_offset + entry.compressed_size > file_size)
		{
			std::fprintf(stderr, "  FAIL: %s — data range exceeds file size\n",
						 entry.name);
			++failures;
			continue;
		}

		// Read compressed data
		f.seekg(static_cast<std::streamoff>(entry.data_offset));
		std::vector<char> compressed(entry.compressed_size);
		f.read(compressed.data(),
			   static_cast<std::streamsize>(entry.compressed_size));
		if (!f)
		{
			std::fprintf(stderr, "  FAIL: %s — cannot read compressed data\n",
						 entry.name);
			++failures;
			continue;
		}

		// Test decompression
		std::vector<char> decompressed(entry.original_size);
		int result =
			LZ4_decompress_safe(compressed.data(), decompressed.data(),
								static_cast<int>(entry.compressed_size),
								static_cast<int>(entry.original_size));

		if (result < 0)
		{
			std::fprintf(stderr, "  FAIL: %s — LZ4 decompression error (%d)\n",
						 entry.name, result);
			++failures;
		}
		else if (static_cast<uint64_t>(result) != entry.original_size)
		{
			std::fprintf(
				stderr, "  FAIL: %s — size mismatch (expected %llu, got %d)\n",
				entry.name,
				static_cast<unsigned long long>(entry.original_size), result);
			++failures;
		}
		else
		{
			std::printf("  OK: %s\n", entry.name);
		}
	}

	if (failures > 0)
	{
		std::fprintf(stderr, "\nValidation FAILED (%d/%u entries)\n", failures,
					 header.entry_count);
		return 1;
	}

	std::printf("\nAll %u entries validated OK\n", header.entry_count);
	return 0;
}

int main(int argc, char* argv[])
{
	std::string output_path;
	fs::path base_dir = fs::current_path();
	std::vector<AssetEntry> entries;

	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];

		if ((arg == "-l" || arg == "--list") && i + 1 < argc)
		{
			return list_pak(argv[++i]);
		}
		else if ((arg == "-v" || arg == "--validate") && i + 1 < argc)
		{
			return validate_pak(argv[++i]);
		}
		else if (arg == "-o" && i + 1 < argc)
		{
			output_path = argv[++i];
		}
		else if (arg == "-b" && i + 1 < argc)
		{
			base_dir = argv[++i];
		}
		else if (arg == "-h" || arg == "--help")
		{
			print_usage(argv[0]);
			return 0;
		}
		else
		{
			AssetEntry entry;
			auto eq = arg.find('=');
			if (eq != std::string::npos)
			{
				entry.name = normalize_name(arg.substr(0, eq));
				entry.filepath = arg.substr(eq + 1);
			}
			else
			{
				entry.filepath = arg;
				entry.name = normalize_name(
					fs::relative(entry.filepath, base_dir).string());
			}
			entries.push_back(std::move(entry));
		}
	}

	if (output_path.empty())
	{
		std::fprintf(stderr, "Error: -o <output.pak> is required\n");
		print_usage(argv[0]);
		return 1;
	}

	if (entries.empty())
	{
		std::fprintf(stderr, "Error: no entries specified\n");
		return 1;
	}

	// Read and compress all entries
	struct CompressedEntry
	{
		std::string name;
		std::vector<char> compressed;
		uint64_t original_size;
	};
	std::vector<CompressedEntry> compressed_entries;
	compressed_entries.reserve(entries.size());

	for (const auto& entry : entries)
	{
		std::ifstream f(entry.filepath, std::ios::ate | std::ios::binary);
		if (!f.is_open())
		{
			std::fprintf(stderr, "Error: cannot open '%s'\n",
						 entry.filepath.string().c_str());
			return 1;
		}

		auto file_size = static_cast<size_t>(f.tellg());
		std::vector<char> raw(file_size);
		f.seekg(0);
		f.read(raw.data(), static_cast<std::streamsize>(file_size));

		int max_compressed = LZ4_compressBound(static_cast<int>(file_size));
		std::vector<char> comp(static_cast<size_t>(max_compressed));
		int compressed_size =
			LZ4_compress_default(raw.data(), comp.data(),
								 static_cast<int>(file_size), max_compressed);

		if (compressed_size <= 0)
		{
			std::fprintf(stderr, "Error: LZ4 compression failed for '%s'\n",
						 entry.name.c_str());
			return 1;
		}
		comp.resize(static_cast<size_t>(compressed_size));

		compressed_entries.push_back({entry.name, std::move(comp), file_size});
	}

	// Compute offsets
	uint64_t data_start = sizeof(pak::FileHeader) +
						  sizeof(pak::TocEntry) * compressed_entries.size();
	uint64_t offset = data_start;

	std::vector<pak::TocEntry> toc(compressed_entries.size());
	for (size_t i = 0; i < compressed_entries.size(); ++i)
	{
		auto& te = toc[i];
		std::memset(&te, 0, sizeof(te));

		const auto& name = compressed_entries[i].name;
		if (name.size() >= pak::MAX_ASSET_NAME)
		{
			std::fprintf(stderr, "Error: asset name too long: '%s'\n",
						 name.c_str());
			return 1;
		}
		std::strncpy(te.name, name.c_str(), pak::MAX_ASSET_NAME - 1);

		te.data_offset = offset;
		te.compressed_size = compressed_entries[i].compressed.size();
		te.original_size = compressed_entries[i].original_size;

		offset += te.compressed_size;
	}

	// Write the pack file
	std::ofstream out(output_path, std::ios::binary);
	if (!out.is_open())
	{
		std::fprintf(stderr, "Error: cannot create '%s'\n",
					 output_path.c_str());
		return 1;
	}

	pak::FileHeader header{};
	header.magic = pak::MAGIC;
	header.version = pak::VERSION;
	header.entry_count = static_cast<uint32_t>(compressed_entries.size());
	header.flags = 0;
	header.toc_offset = sizeof(pak::FileHeader);

	out.write(reinterpret_cast<const char*>(&header), sizeof(header));
	out.write(reinterpret_cast<const char*>(toc.data()),
			  static_cast<std::streamsize>(sizeof(pak::TocEntry) * toc.size()));

	for (const auto& ce : compressed_entries)
		out.write(ce.compressed.data(),
				  static_cast<std::streamsize>(ce.compressed.size()));

	out.close();

	std::printf("Packed %zu assets into %s (%.1f KB)\n",
				compressed_entries.size(), output_path.c_str(),
				static_cast<double>(offset) / 1024.0);

	return 0;
}
