#pragma once

#include "core/GrayImage.h"

namespace vf {

class ImageNormalize {
public:
    static GrayImage normalizeToByteRange(const GrayImage& input);
};

} // namespace vf
