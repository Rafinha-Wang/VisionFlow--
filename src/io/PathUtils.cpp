#include "io/PathUtils.h"

#include <filesystem>
#include <stdexcept>

namespace vf {

void PathUtils::ensureDirectory(const std::string& path) {
    if (path.empty()) {
        throw std::runtime_error("Output directory path is empty.");
    }
    std::filesystem::create_directories(std::filesystem::path(path));
}

std::string PathUtils::join(const std::string& a, const std::string& b) {
    return (std::filesystem::path(a) / std::filesystem::path(b)).string();
}


} // namespace vf
