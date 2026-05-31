#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace vf {

class Image {
public:
    int width = 0;
    int height = 0;
    int channels = 3;
    std::vector<unsigned char> data;

    Image() = default;
    Image(int w, int h, int c = 3);

    bool empty() const;
    unsigned char& at(int x, int y, int c);
    unsigned char at(int x, int y, int c) const;

private:
    std::size_t index(int x, int y, int c) const;
};

} // namespace vf
