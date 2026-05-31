#include "core/Timer.h"

namespace vf {

Timer::Timer() {
    reset();
}

void Timer::reset() {
    start_ = std::chrono::high_resolution_clock::now();
}

double Timer::elapsedMs() const {
    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration<double, std::milli>(end - start_);
    return duration.count();
}

} // namespace vf
