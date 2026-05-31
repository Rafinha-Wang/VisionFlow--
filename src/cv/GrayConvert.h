#pragma once

#include "core/GrayImage.h"
#include "core/Image.h"

namespace vf {

class GrayConvert {
public:
    static GrayImage toGray(const Image& image);
};

} // namespace vf
