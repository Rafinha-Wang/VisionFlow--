#pragma once

#include <chrono>

namespace vf {

class Timer {
public:
    Timer();
    void reset();
    double elapsedMs() const;

private:
    std::chrono::high_resolution_clock::time_point start_;
};

} // namespace vf
