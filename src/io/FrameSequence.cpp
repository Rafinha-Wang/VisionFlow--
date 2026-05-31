#include "io/FrameSequence.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace vf {
namespace {

std::string lowerString(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

} // namespace

std::vector<std::string> FrameSequence::listBmpFiles(const std::string& folder) {
    namespace fs = std::filesystem;

    fs::path folderPath(folder);
    if (!fs::exists(folderPath)) {
        throw std::runtime_error("Frame folder does not exist: " + folder);
    }
    if (!fs::is_directory(folderPath)) {
        throw std::runtime_error("Input path is not a folder: " + folder);
    }

    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(folderPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string extension = lowerString(entry.path().extension().string());
        if (extension == ".bmp") {
            files.push_back(entry.path().string());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

} // namespace vf
