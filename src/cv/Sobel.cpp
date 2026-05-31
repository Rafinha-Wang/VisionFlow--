#include "cv/Sobel.h"

#include "cv/ImageNormalize.h"

#include <cmath>
#include <stdexcept>

namespace vf {

GrayImage Sobel::detect(const GrayImage& gray) {
    if (gray.empty()) {
        throw std::runtime_error("Sobel::detect expects a non-empty gray image.");
    }

    GrayImage magnitude(gray.width, gray.height);

    const int kx[3][3] = {
        {-1, 0, 1},
        {-2, 0, 2},
        {-1, 0, 1}
    };

    const int ky[3][3] = {
        {-1, -2, -1},
        { 0,  0,  0},
        { 1,  2,  1}
    };

    for (int y = 1; y < gray.height - 1; ++y) {
        for (int x = 1; x < gray.width - 1; ++x) {
            float gx = 0.0f;
            float gy = 0.0f;

            for (int j = -1; j <= 1; ++j) {
                for (int i = -1; i <= 1; ++i) {
                    const float p = gray.at(x + i, y + j);
                    gx += static_cast<float>(kx[j + 1][i + 1]) * p;
                    gy += static_cast<float>(ky[j + 1][i + 1]) * p;
                }
            }

            magnitude.at(x, y) = std::sqrt(gx * gx + gy * gy);
        }
    }

    return ImageNormalize::normalizeToByteRange(magnitude);
}

} // namespace vf
