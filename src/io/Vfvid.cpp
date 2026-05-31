#include "io/Vfvid.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "core/Image.h"
#include "io/BmpReader.h"
#include "io/BmpWriter.h"
#include "io/FrameSequence.h"
#include "io/PathUtils.h"

namespace vf {
namespace {

constexpr std::array<unsigned char, 8> kMagic = {'V', 'F', 'V', 'I', 'D', '0', '1', '\0'};
constexpr std::uint32_t kChannelsRgb24 = 3;

void writeU32(std::ofstream& out, std::uint32_t value) {
    unsigned char b[4] = {
        static_cast<unsigned char>(value & 0xFF),
        static_cast<unsigned char>((value >> 8) & 0xFF),
        static_cast<unsigned char>((value >> 16) & 0xFF),
        static_cast<unsigned char>((value >> 24) & 0xFF)
    };
    out.write(reinterpret_cast<const char*>(b), 4);
}

void writeU64(std::ofstream& out, std::uint64_t value) {
    unsigned char b[8] = {};
    for (int i = 0; i < 8; ++i) {
        b[i] = static_cast<unsigned char>((value >> (i * 8)) & 0xFF);
    }
    out.write(reinterpret_cast<const char*>(b), 8);
}

std::uint32_t readU32(std::ifstream& in) {
    unsigned char b[4] = {};
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) {
        throw std::runtime_error("Failed to read VFVID uint32.");
    }
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

std::uint64_t readU64(std::ifstream& in) {
    unsigned char b[8] = {};
    in.read(reinterpret_cast<char*>(b), 8);
    if (!in) {
        throw std::runtime_error("Failed to read VFVID uint64.");
    }

    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= (static_cast<std::uint64_t>(b[i]) << (i * 8));
    }
    return value;
}

std::string formatFrameName(int index) {
    std::ostringstream oss;
    oss << "frame_" << std::setw(4) << std::setfill('0') << index << ".bmp";
    return oss.str();
}

void writeHeader(std::ofstream& out, const VfvidInfo& info) {
    out.write(reinterpret_cast<const char*>(kMagic.data()), static_cast<std::streamsize>(kMagic.size()));
    writeU32(out, info.width);
    writeU32(out, info.height);
    writeU32(out, info.fps);
    writeU32(out, info.frameCount);
    writeU32(out, info.channels);
    writeU64(out, info.frameBytes);
}

VfvidInfo readHeader(std::ifstream& in, const std::string& path) {
    std::array<unsigned char, 8> magic = {};
    in.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) {
        throw std::runtime_error("Not a VFVID v1 file: " + path);
    }

    VfvidInfo info;
    info.width = readU32(in);
    info.height = readU32(in);
    info.fps = readU32(in);
    info.frameCount = readU32(in);
    info.channels = readU32(in);
    info.frameBytes = readU64(in);

    if (info.width == 0 || info.height == 0 || info.frameCount == 0 || info.channels != kChannelsRgb24) {
        throw std::runtime_error("Unsupported or invalid VFVID header: " + path);
    }

    const std::uint64_t expectedFrameBytes =
        static_cast<std::uint64_t>(info.width) * static_cast<std::uint64_t>(info.height) * info.channels;
    if (info.frameBytes != expectedFrameBytes) {
        throw std::runtime_error("VFVID frame size mismatch: " + path);
    }

    info.dataBytes = info.frameBytes * info.frameCount;
    return info;
}

} // namespace

VfvidInfo Vfvid::readInfo(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open VFVID file: " + path);
    }
    return readHeader(in, path);
}

void Vfvid::packFromBmpFolder(const std::string& inputFolder,
                              const std::string& outputPath,
                              std::uint32_t fps) {
    namespace fs = std::filesystem;
    std::vector<std::string> framePaths = FrameSequence::listBmpFiles(inputFolder);
    if (framePaths.empty()) {
        throw std::runtime_error("Cannot pack VFVID: no BMP frames found in " + inputFolder);
    }
    if (fps == 0) {
        throw std::runtime_error("VFVID fps must be greater than zero.");
    }

    Image first = BmpReader::read(framePaths.front());
    if (first.empty() || first.channels != static_cast<int>(kChannelsRgb24)) {
        throw std::runtime_error("VFVID only supports non-empty RGB24 BMP frames.");
    }

    fs::path outPath(outputPath);
    if (!outPath.parent_path().empty()) {
        fs::create_directories(outPath.parent_path());
    }

    VfvidInfo info;
    info.width = static_cast<std::uint32_t>(first.width);
    info.height = static_cast<std::uint32_t>(first.height);
    info.fps = fps;
    info.frameCount = static_cast<std::uint32_t>(framePaths.size());
    info.channels = kChannelsRgb24;
    info.frameBytes = static_cast<std::uint64_t>(first.data.size());
    info.dataBytes = info.frameBytes * info.frameCount;

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot create VFVID file: " + outputPath);
    }

    writeHeader(out, info);
    out.write(reinterpret_cast<const char*>(first.data.data()), static_cast<std::streamsize>(first.data.size()));

    for (std::size_t i = 1; i < framePaths.size(); ++i) {
        Image frame = BmpReader::read(framePaths[i]);
        if (frame.width != first.width || frame.height != first.height || frame.channels != first.channels) {
            throw std::runtime_error("Cannot pack VFVID: frame size mismatch at " + framePaths[i]);
        }
        out.write(reinterpret_cast<const char*>(frame.data.data()), static_cast<std::streamsize>(frame.data.size()));
    }

    if (!out) {
        throw std::runtime_error("Failed while writing VFVID file: " + outputPath);
    }
}

void Vfvid::unpackToBmpFolder(const std::string& inputPath,
                              const std::string& outputFolder) {
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open VFVID file: " + inputPath);
    }

    const VfvidInfo info = readHeader(in, inputPath);
    PathUtils::ensureDirectory(outputFolder);

    std::vector<unsigned char> buffer(static_cast<std::size_t>(info.frameBytes));
    for (std::uint32_t i = 0; i < info.frameCount; ++i) {
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        if (!in) {
            throw std::runtime_error("Unexpected end of VFVID frame data: " + inputPath);
        }

        Image frame(static_cast<int>(info.width), static_cast<int>(info.height), static_cast<int>(info.channels));
        frame.data = buffer;
        BmpWriter::writeColor(PathUtils::join(outputFolder, formatFrameName(static_cast<int>(i + 1))), frame);
    }
}

} // namespace vf
