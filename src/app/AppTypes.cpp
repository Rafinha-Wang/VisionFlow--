#include "app/AppTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "app/VideoIOAdapter.h"
#include "io/PathUtils.h"

namespace vfapp {

void printUsage() {
    std::cout << "VisionFlow Version 8ProSafe Auto\n\n";
    std::cout << "Usage:\n";
    std::cout << "  VisionFlow.exe\n";
    std::cout << "  VisionFlow.exe input.mp4 output.mp4\n";
    std::cout << "  VisionFlow.exe input_frames_dir output_dir\n";
    std::cout << "  VisionFlow.exe input.vfvid output.vfvid\n";
    std::cout << "  VisionFlow.exe --mode auto    input output\n";
    std::cout << "  VisionFlow.exe --mode static  input output\n";
    std::cout << "  VisionFlow.exe --mode motion  input output\n";
    std::cout << "  VisionFlow.exe --batch tasks.csv [batch_report.html]\n";
    std::cout << "  VisionFlow.exe --pack input_frames output.vfvid [fps]\n";
    std::cout << "  VisionFlow.exe --unpack input.vfvid output_frames\n";
    std::cout << "  VisionFlow.exe --vfinfo input.vfvid\n";
    std::cout << "  VisionFlow.exe auto    input_frames_dir output_dir\n";
    std::cout << "  VisionFlow.exe static  input_frames_dir output_dir\n";
    std::cout << "  VisionFlow.exe statics input_frames_dir output_dir\n";
    std::cout << "  VisionFlow.exe motion  input_frames_dir output_dir\n\n";
    std::cout << "Modes:\n";
    std::cout << "  auto    = analyze global camera motion first, then choose static or motion\n";
    std::cout << "  static  = lock to the first viewpoint for mostly fixed-camera clips\n";
    std::cout << "  motion  = virtual gimbal mode for intentional camera movement\n\n";
    std::cout << "Input / output:\n";
    std::cout << "  BMP frame folders are the strict competition-safe path.\n";
    std::cout << "  MP4 input/output is optional and calls external ffmpeg.exe only for format conversion.\n\n";
    std::cout << "  VFVID input/output is a native RGB24 container implemented in pure C++.\n\n";
    std::cout << "Examples:\n";
    std::cout << "  VisionFlow.exe case.mp4 result.mp4\n";
    std::cout << "  VisionFlow.exe case.vfvid result.vfvid\n";
    std::cout << "  VisionFlow.exe --mode static case.mp4 result_static.mp4\n";
    std::cout << "  VisionFlow.exe --batch tasks.csv\n";
    std::cout << "  VisionFlow.exe --pack demo\\frames demo.vfvid 30\n";
    std::cout << "  VisionFlow.exe auto demo\\frames output_auto\n";
    std::cout << "  VisionFlow.exe static demo\\frames output_static\n";
    std::cout << "  VisionFlow.exe motion demo\\frames output_motion\n";
}

std::string lowerString(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

bool parseMode(const std::string& value, Mode& mode) {
    const std::string key = lowerString(value);
    if (key == "auto") {
        mode = Mode::Auto;
        return true;
    }
    if (key == "static" || key == "statics") {
        mode = Mode::Statics;
        return true;
    }
    if (key == "motion") {
        mode = Mode::Motion;
        return true;
    }
    return false;
}

Mode parseModeOrThrow(const std::string& value) {
    Mode mode = Mode::Auto;
    if (!parseMode(value, mode)) {
        throw std::runtime_error("Unknown mode: " + value);
    }
    return mode;
}

bool isModeWord(const std::string& value) {
    Mode ignored = Mode::Auto;
    return parseMode(value, ignored);
}

std::string pathExtensionLower(const std::string& path) {
    return lowerString(std::filesystem::path(path).extension().string());
}

bool isVideoInputPath(const std::string& path) {
    const std::string ext = pathExtensionLower(path);
    return ext == ".mp4" || ext == ".m4v" || ext == ".mov" || ext == ".avi" || ext == ".mkv";
}

bool isMp4OutputPath(const std::string& path) {
    return pathExtensionLower(path) == ".mp4";
}

bool isVfvidPath(const std::string& path) {
    return pathExtensionLower(path) == ".vfvid";
}

bool isContainerOutputPath(const std::string& path) {
    return isMp4OutputPath(path) || isVfvidPath(path);
}

std::string trimString(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string trimPromptPath(std::string value) {
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
    value = trimString(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string htmlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += ch; break;
        }
    }
    return out;
}

std::string reportPathForOutput(const std::string& outputPath) {
    if (isContainerOutputPath(outputPath)) {
        return (deriveWorkDirectory(outputPath) / "report.html").string();
    }
    return vf::PathUtils::join(outputPath, "report.html");
}

std::filesystem::path pipelineOutputDirectoryFor(const std::string& outputPath) {
    if (isContainerOutputPath(outputPath)) {
        return deriveWorkDirectory(outputPath);
    }
    return std::filesystem::path(outputPath);
}

double parseDoubleAfterPrefix(const std::string& line, const std::string& prefix) {
    const std::string tail = trimString(line.substr(prefix.size()));
    std::istringstream iss(tail);
    double value = 0.0;
    iss >> value;
    return value;
}

int parseIntAfterPrefix(const std::string& line, const std::string& prefix) {
    const std::string tail = trimString(line.substr(prefix.size()));
    std::istringstream iss(tail);
    int value = 0;
    iss >> value;
    return value;
}

ProcessingSummary readProcessingSummary(const std::string& outputPath) {
    ProcessingSummary summary;
    summary.reportPath = reportPathForOutput(outputPath);
    const std::filesystem::path infoPath = std::filesystem::path(summary.reportPath).parent_path() / "frames_info.txt";

    std::ifstream in(infoPath);
    if (!in) {
        return summary;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (startsWith(line, "Active mode: ")) {
            summary.activeMode = trimString(line.substr(std::string("Active mode: ").size()));
        } else if (startsWith(line, "Frame count: ")) {
            summary.frameCount = parseIntAfterPrefix(line, "Frame count: ");
        } else if (startsWith(line, "Input jitter RMS: ")) {
            summary.inputJitter = parseDoubleAfterPrefix(line, "Input jitter RMS: ");
        } else if (startsWith(line, "Output jitter RMS: ")) {
            summary.outputJitter = parseDoubleAfterPrefix(line, "Output jitter RMS: ");
        }
    }

    if (summary.inputJitter > 1e-9) {
        summary.improvementPercent = (summary.inputJitter - summary.outputJitter) / summary.inputJitter * 100.0;
    }
    summary.found = true;
    return summary;
}

std::string formatIndex(int index) {
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << index;
    return oss.str();
}

std::string htmlRelative(const std::string& folder, const std::string& fileName) {
    return folder + "/" + fileName;
}

const char* modeName(Mode mode) {
    if (mode == Mode::Auto) return "auto";
    return mode == Mode::Statics ? "static" : "motion";
}

const char* modeLabel(Mode mode) {
    if (mode == Mode::Auto) return "AUTO";
    return mode == Mode::Statics ? "STATIC" : "MOTION";
}

std::string formatFixed(double value, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

std::string shortenMiddle(const std::string& value, std::size_t maxLength) {
    if (value.size() <= maxLength) {
        return value;
    }
    if (maxLength <= 3) {
        return value.substr(0, maxLength);
    }
    const std::size_t left = (maxLength - 3) / 2;
    const std::size_t right = maxLength - 3 - left;
    return value.substr(0, left) + "..." + value.substr(value.size() - right);
}

std::string padRight(std::string value, std::size_t width) {
    value = shortenMiddle(value, width);
    if (value.size() < width) {
        value += std::string(width - value.size(), ' ');
    }
    return value;
}

} // namespace vfapp
