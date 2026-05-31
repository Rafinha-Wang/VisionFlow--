#include "core/Image.h"

namespace vf {

Image::Image(int w, int h, int c) : width(w), height(h), channels(c) {
    if (w <= 0 || h <= 0 || c <= 0) {
        throw std::invalid_argument("Image dimensions and channels must be positive.");
    }
    data.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(c), 0);
}

bool Image::empty() const {
    return width <= 0 || height <= 0 || channels <= 0 || data.empty();
}


unsigned char& Image::at(int x, int y, int c) {
    return data[index(x, y, c)];
}

unsigned char Image::at(int x, int y, int c) const {
    return data[index(x, y, c)];
}

std::size_t Image::index(int x, int y, int c) const {
    if (x < 0 || x >= width || y < 0 || y >= height || c < 0 || c >= channels) {
        throw std::out_of_range("Image pixel index out of range.");
    }
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x))
           * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c);
}

} // namespace vf
