#include "cv/GrayConvert.h"

#include <stdexcept>

namespace vf {

GrayImage GrayConvert::toGray(const Image& image) {
    if (image.empty() || image.channels != 3) {
        throw std::runtime_error("GrayConvert::toGray expects a non-empty 3-channel image.");
    }

    GrayImage gray(image.width, image.height);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const float r = static_cast<float>(image.at(x, y, 0));
            const float g = static_cast<float>(image.at(x, y, 1));
            const float b = static_cast<float>(image.at(x, y, 2));
            gray.at(x, y) = 0.299f * r + 0.587f * g + 0.114f * b;
        }
    }
    return gray;
}

} // namespace vf
