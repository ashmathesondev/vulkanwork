#include "plyLoader.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

// =============================================================================
// Helpers
// =============================================================================

size_t PlyReader::type_byte_size(const std::string& t)
{
	if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
	if (t == "short" || t == "ushort" || t == "int16" || t == "uint16")
		return 2;
	if (t == "int" || t == "uint" || t == "int32" || t == "uint32" ||
		t == "float" || t == "float32")
		return 4;
	if (t == "double" || t == "float64" || t == "int64" || t == "uint64")
		return 8;
	return 0;  // unknown
}

// =============================================================================
// open()
// =============================================================================

bool PlyReader::open(const std::string& path)
{
	std::FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) return false;

	// ---- Parse ASCII header -------------------------------------------------
	char line[1024];
	bool inHeader = true;
	bool isBinary = false;
	bool foundVert = false;	 // only the first "element vertex" is used
	bool headerDone = false;

	while (inHeader && std::fgets(line, sizeof(line), f))
	{
		// Strip trailing newline
		size_t len = std::strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		if (std::strncmp(line, "end_header", 10) == 0)
		{
			headerDone = true;
			break;
		}
		if (std::strncmp(line, "format binary_little_endian", 27) == 0)
		{
			isBinary = true;
		}
		if (!foundVert && std::strncmp(line, "element vertex ", 15) == 0)
		{
			elementCount_ = static_cast<uint32_t>(std::atoi(line + 15));
			foundVert = true;
			props_.clear();
			rowSize_ = 0;
		}
		if (foundVert && std::strncmp(line, "property ", 9) == 0)
		{
			// Skip list properties (not supported)
			if (std::strncmp(line + 9, "list", 4) == 0) continue;

			// "property <type> <name>"
			char typeStr[64]{}, nameStr[128]{};
			if (std::sscanf(line + 9, "%63s %127s", typeStr, nameStr) != 2)
				continue;

			PlyProperty p;
			p.type = typeStr;
			p.name = nameStr;
			p.byteSize = type_byte_size(typeStr);
			p.byteOffset = rowSize_;
			rowSize_ += p.byteSize;
			props_.push_back(p);
		}
		// Once we see another "element" after vertex, stop collecting
		// properties
		if (foundVert && std::strncmp(line, "element ", 8) == 0 &&
			std::strncmp(line, "element vertex", 14) != 0)
		{
			// A second element starts — stop reading vertex properties
			// But we still need to skip to end_header; just don't add more
			// props
		}
	}

	if (!isBinary || !headerDone || !foundVert || rowSize_ == 0)
	{
		std::fclose(f);
		return false;
	}

	// ---- Read binary body ---------------------------------------------------
	size_t totalBytes = static_cast<size_t>(elementCount_) * rowSize_;
	data_.resize(totalBytes);
	size_t read = std::fread(data_.data(), 1, totalBytes, f);
	std::fclose(f);

	return read == totalBytes;
}

// =============================================================================
// Accessors
// =============================================================================

const void* PlyReader::row_data(uint32_t i) const
{
	return data_.data() + static_cast<size_t>(i) * rowSize_;
}

size_t PlyReader::property_offset(const std::string& name) const
{
	for (const auto& p : props_)
		if (p.name == name) return p.byteOffset;
	return SIZE_MAX;
}

size_t PlyReader::property_size(const std::string& name) const
{
	for (const auto& p : props_)
		if (p.name == name) return p.byteSize;
	return 0;
}
