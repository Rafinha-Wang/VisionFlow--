#include "io/BmpWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vf {
namespace {

void writeU16(std::ofstream& out, std::uint16_t value) {
    unsigned char b[2] = {
        static_cast<unsigned char>(value & 0xFF),
        static_cast<unsigned char>((value >> 8) & 0xFF)
    };
    out.write(reinterpret_cast<const char*>(b), 2);
}

void writeU32(std::ofstream& out, std::uint32_t value) {
    unsigned char b[4] = {
        static_cast<unsigned char>(value & 0xFF),
        static_cast<unsigned char>((value >> 8) & 0xFF),
        static_cast<unsigned char>((value >> 16) & 0xFF),
        static_cast<unsigned char>((value >> 24) & 0xFF)
    };
    out.write(reinterpret_cast<const char*>(b), 4);
}

void writeI32(std::ofstream& out, std::int32_t value) {
    writeU32(out, static_cast<std::uint32_t>(value));
}

unsigned char clampToByte(float value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    value = std::max(0.0f, std::min(255.0f, value));
    return static_cast<unsigned char>(value + 0.5f);
}

void writeHeader(std::ofstream& out, int width, int height) {
    const int rowStride = ((width * 3 + 3) / 4) * 4;
    const std::uint32_t pixelDataSize = static_cast<std::uint32_t>(rowStride * height);
    const std::uint32_t fileHeaderSize = 14;
    const std::uint32_t dibHeaderSize = 40;
    const std::uint32_t pixelOffset = fileHeaderSize + dibHeaderSize;
    const std::uint32_t fileSize = pixelOffset + pixelDataSize;

    writeU16(out, 0x4D42);
    writeU32(out, fileSize);
    writeU16(out, 0);
    writeU16(out, 0);
    writeU32(out, pixelOffset);

    writeU32(out, dibHeaderSize);
    writeI32(out, width);
    writeI32(out, height);
    writeU16(out, 1);
    writeU16(out, 24);
    writeU32(out, 0);
    writeU32(out, pixelDataSize);
    writeI32(out, 2835);
    writeI32(out, 2835);
    writeU32(out, 0);
    writeU32(out, 0);
}

} // namespace

void BmpWriter::writeColor(const std::string& path, const Image& image) {
    if (image.empty() || image.channels != 3) {
        throw std::runtime_error("BmpWriter::writeColor expects a non-empty 3-channel image.");
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot create BMP file: " + path);
    }

    writeHeader(out, image.width, image.height);

    const int rowStride = ((image.width * 3 + 3) / 4) * 4;
    std::vector<unsigned char> row(static_cast<std::size_t>(rowStride), static_cast<unsigned char>(0));

    for (int fileY = 0; fileY < image.height; ++fileY) {
        const int y = image.height - 1 - fileY;
        std::fill(row.begin(), row.end(), static_cast<unsigned char>(0));
        for (int x = 0; x < image.width; ++x) {
            const int base = x * 3;
            const unsigned char r = image.at(x, y, 0);
            const unsigned char g = image.at(x, y, 1);
            const unsigned char b = image.at(x, y, 2);
            row[base + 0] = b;
            row[base + 1] = g;
            row[base + 2] = r;
        }
        out.write(reinterpret_cast<const char*>(row.data()), rowStride);
    }
}

void BmpWriter::writeGray(const std::string& path, const GrayImage& image) {
    if (image.empty()) {
        throw std::runtime_error("BmpWriter::writeGray expects a non-empty gray image.");
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot create BMP file: " + path);
    }

    writeHeader(out, image.width, image.height);

    const int rowStride = ((image.width * 3 + 3) / 4) * 4;
    std::vector<unsigned char> row(static_cast<std::size_t>(rowStride), static_cast<unsigned char>(0));

    for (int fileY = 0; fileY < image.height; ++fileY) {
        const int y = image.height - 1 - fileY;
        std::fill(row.begin(), row.end(), static_cast<unsigned char>(0));
        for (int x = 0; x < image.width; ++x) {
            const unsigned char v = clampToByte(image.at(x, y));
            const int base = x * 3;
            row[base + 0] = v;
            row[base + 1] = v;
            row[base + 2] = v;
        }
        out.write(reinterpret_cast<const char*>(row.data()), rowStride);
    }
}

} // namespace vf
