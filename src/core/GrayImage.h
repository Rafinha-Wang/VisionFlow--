#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace vf {

class GrayImage {
public:
    int width = 0;
    int height = 0;
    std::vector<float> data;

    GrayImage() = default;
    GrayImage(int w, int h);

    bool empty() const;
    float& at(int x, int y);
    float at(int x, int y) const;

private:
    std::size_t index(int x, int y) const;
};

} // namespace vf
