#include "io/BmpReader.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vf {
namespace {

std::uint16_t readU16(std::ifstream& in) {
    unsigned char b[2]{};
    in.read(reinterpret_cast<char*>(b), 2);
    if (!in) {
        throw std::runtime_error("Failed to read BMP uint16.");
    }
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

std::uint32_t readU32(std::ifstream& in) {
    unsigned char b[4]{};
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) {
        throw std::runtime_error("Failed to read BMP uint32.");
    }
    return static_cast<std::uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

std::int32_t readI32(std::ifstream& in) {
    return static_cast<std::int32_t>(readU32(in));
}

} // namespace

Image BmpReader::read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open BMP file: " + path);
    }

    const std::uint16_t signature = readU16(in);
    if (signature != 0x4D42) {
        throw std::runtime_error("Not a BMP file: " + path);
    }

    (void)readU32(in); // file size
    (void)readU16(in); // reserved1
    (void)readU16(in); // reserved2
    const std::uint32_t pixelOffset = readU32(in);

    const std::uint32_t dibHeaderSize = readU32(in);
    if (dibHeaderSize < 40) {
        throw std::runtime_error("Unsupported BMP DIB header size. Need BITMAPINFOHEADER or compatible.");
    }

    const std::int32_t width = readI32(in);
    const std::int32_t rawHeight = readI32(in);
    const bool topDown = rawHeight < 0;
    const std::int32_t height = rawHeight < 0 ? -rawHeight : rawHeight;

    const std::uint16_t planes = readU16(in);
    const std::uint16_t bitCount = readU16(in);
    const std::uint32_t compression = readU32(in);
    (void)readU32(in); // image size
    (void)readI32(in); // x pixels per meter
    (void)readI32(in); // y pixels per meter
    (void)readU32(in); // colors used
    (void)readU32(in); // important colors

    if (planes != 1) {
        throw std::runtime_error("Invalid BMP planes. Only planes == 1 is supported.");
    }
    if (bitCount != 24) {
        throw std::runtime_error("Only 24-bit BMP is supported in VisionFlow Day 1.");
    }
    if (compression != 0) {
        throw std::runtime_error("Only uncompressed BMP is supported in VisionFlow Day 1.");
    }
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid BMP width or height.");
    }

    Image image(width, height, 3);
    const int rowStride = ((width * 3 + 3) / 4) * 4;
    std::vector<unsigned char> row(static_cast<std::size_t>(rowStride));

    in.seekg(static_cast<std::streamoff>(pixelOffset), std::ios::beg);
    if (!in) {
        throw std::runtime_error("Failed to seek to BMP pixel data.");
    }

    for (int fileY = 0; fileY < height; ++fileY) {
        in.read(reinterpret_cast<char*>(row.data()), rowStride);
        if (!in) {
            throw std::runtime_error("Failed to read BMP pixel row.");
        }

        const int y = topDown ? fileY : (height - 1 - fileY);
        for (int x = 0; x < width; ++x) {
            const int base = x * 3;
            const unsigned char b = row[base + 0];
            const unsigned char g = row[base + 1];
            const unsigned char r = row[base + 2];
            image.at(x, y, 0) = r;
            image.at(x, y, 1) = g;
            image.at(x, y, 2) = b;
        }
    }

    return image;
}

} // namespace vf
