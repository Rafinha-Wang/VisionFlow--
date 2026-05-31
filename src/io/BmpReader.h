#pragma once

#include <string>

#include "core/Image.h"

namespace vf {

class BmpReader {
public:
    static Image read(const std::string& path);
};

} // namespace vf
