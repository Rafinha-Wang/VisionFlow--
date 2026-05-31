#include "core/GrayImage.h"

namespace vf {

GrayImage::GrayImage(int w, int h) : width(w), height(h) {
    if (w <= 0 || h <= 0) {
        throw std::invalid_argument("GrayImage dimensions must be positive.");
    }
    data.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0f);
}

bool GrayImage::empty() const {
    return width <= 0 || height <= 0 || data.empty();
}


float& GrayImage::at(int x, int y) {
    return data[index(x, y)];
}

float GrayImage::at(int x, int y) const {
    return data[index(x, y)];
}

std::size_t GrayImage::index(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        throw std::out_of_range("GrayImage pixel index out of range.");
    }
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

} // namespace vf
