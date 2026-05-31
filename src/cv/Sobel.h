#pragma once

#include "core/GrayImage.h"

namespace vf {

class Sobel {
public:
    static GrayImage detect(const GrayImage& gray);
};

} // namespace vf
