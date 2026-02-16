#pragma once

#include "pak_format.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pak {

class PackFile {
public:
    explicit PackFile(const std::filesystem::path& pak_path);

    bool              contains(std::string_view name) const;
    std::vector<char> read(std::string_view name) const;
    size_t            original_size(std::string_view name) const;
    std::vector<std::string> list_assets() const;

private:
    std::filesystem::path path_;
    std::unordered_map<std::string, TocEntry> toc_;
};

} // namespace pak
