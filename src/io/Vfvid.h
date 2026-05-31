#pragma once

#include <cstdint>
#include <string>

namespace vf {

struct VfvidInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps = 30;
    std::uint32_t frameCount = 0;
    std::uint32_t channels = 3;
    std::uint64_t frameBytes = 0;
    std::uint64_t dataBytes = 0;
};

class Vfvid {
public:
    static VfvidInfo readInfo(const std::string& path);
    static void packFromBmpFolder(const std::string& inputFolder,
                                  const std::string& outputPath,
                                  std::uint32_t fps = 30);
    static void unpackToBmpFolder(const std::string& inputPath,
                                  const std::string& outputFolder);
};

} // namespace vf
