#pragma once

#include <string>
#include <vector>

namespace vf {

class FrameSequence {
public:
    static std::vector<std::string> listBmpFiles(const std::string& folder);
};

} // namespace vf
