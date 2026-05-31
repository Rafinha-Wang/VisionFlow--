#pragma once

#include <string>

#include "core/GrayImage.h"
#include "core/Image.h"

namespace vf {

class BmpWriter {
public:
    static void writeColor(const std::string& path, const Image& image);
    static void writeGray(const std::string& path, const GrayImage& image);
};

} // namespace vf
