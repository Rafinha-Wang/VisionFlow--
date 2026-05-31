#include "cv/ImageNormalize.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vf {

GrayImage ImageNormalize::normalizeToByteRange(const GrayImage& input) {
    if (input.empty()) {
        throw std::runtime_error("Cannot normalize an empty image.");
    }

    float minValue = input.data[0];
    float maxValue = input.data[0];
    for (float v : input.data) {
        if (std::isfinite(v)) {
            minValue = std::min(minValue, v);
            maxValue = std::max(maxValue, v);
        }
    }

    GrayImage output(input.width, input.height);
    const float range = maxValue - minValue;
    if (range < 1e-6f) {
        return output;
    }

    for (int y = 0; y < input.height; ++y) {
        for (int x = 0; x < input.width; ++x) {
            const float normalized = (input.at(x, y) - minValue) / range * 255.0f;
            output.at(x, y) = normalized;
        }
    }
    return output;
}

} // namespace vf
