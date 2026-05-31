#pragma once

#include <string>

namespace vf {

class PathUtils {
public:
    static void ensureDirectory(const std::string& path);
    static std::string join(const std::string& a, const std::string& b);
};

} // namespace vf
