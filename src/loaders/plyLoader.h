#pragma once

#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// PlyReader — minimal binary-little-endian PLY parser
// =============================================================================
// Parses the ASCII header to discover element count, property names and byte
// offsets within each row, then provides random access to raw row bytes.
//
// Usage:
//   PlyReader r;
//   if (!r.open(path)) { /* error */ }
//   size_t off = r.property_offset("x");   // byte offset of "x" in a row
//   size_t row = r.row_size();
//   for (uint32_t i = 0; i < r.element_count(); ++i) {
//       const void* p = r.row_data(i);
//       float x;
//       std::memcpy(&x, static_cast<const char*>(p) + off, sizeof(x));
//   }
// =============================================================================

struct PlyProperty
{
	std::string name;
	std::string type;  // "float", "uchar", "int", …
	size_t byteOffset = 0;
	size_t byteSize = 0;
};

class PlyReader
{
   public:
	bool open(const std::string& path);

	uint32_t element_count() const { return elementCount_; }
	size_t row_size() const { return rowSize_; }
	const void* row_data(uint32_t i) const;

	// Returns SIZE_MAX if property not found.
	size_t property_offset(const std::string& name) const;
	size_t property_size(const std::string& name) const;

	bool has_property(const std::string& name) const
	{
		return property_offset(name) != SIZE_MAX;
	}

   private:
	uint32_t elementCount_ = 0;
	size_t rowSize_ = 0;
	std::vector<PlyProperty> props_;
	std::vector<char> data_;  // raw binary body

	static size_t type_byte_size(const std::string& t);
};
