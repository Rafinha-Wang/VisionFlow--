#include "app/VideoIOAdapter.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "app/AppTypes.h"
#include "io/FrameSequence.h"
#include "io/PathUtils.h"
#include "io/Vfvid.h"

#ifdef _WIN32
#define NOMINMAX
#include <process.h>
#endif

namespace vfapp {

ScopedDirectoryCleanup::~ScopedDirectoryCleanup() {
    if (!enabled || path.empty()) {
        return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

std::string quoteCommandArg(const std::string& value) {
    if (value.find('"') != std::string::npos) {
        throw std::runtime_error("Paths used in external commands must not contain quote characters: " + value);
    }
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
#ifdef _WIN32
        if (ch == '%') {
            escaped += "%%";
            continue;
        }
#endif
        escaped += ch;
    }
    return "\"" + escaped + "\"";
}

void runProcessOrThrow(const std::vector<std::string>& args, const std::string& action) {
    if (args.empty()) {
        throw std::runtime_error("Cannot run empty external command.");
    }

    std::cout << "[VisionFlow] " << action << "\n";
#ifdef _WIN32
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);
    const intptr_t rc = _spawnvp(_P_WAIT, args.front().c_str(), argv.data());
#else
    std::ostringstream command;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            command << ' ';
        }
        command << quoteCommandArg(args[i]);
    }
    const int rc = std::system(command.str().c_str());
#endif
    if (rc != 0) {
        throw std::runtime_error(action + " failed. Make sure ffmpeg.exe is beside VisionFlow.exe or available in PATH.");
    }
}

std::string executableDirectory(const char* argv0) {
    namespace fs = std::filesystem;
    if (argv0 == nullptr || std::string(argv0).empty()) {
        return fs::current_path().string();
    }

    fs::path exePath(argv0);
    if (exePath.is_relative()) {
        exePath = fs::absolute(exePath);
    }
    return exePath.parent_path().string();
}

std::string findFfmpegExecutable(const char* argv0) {
    namespace fs = std::filesystem;
    const fs::path exeDir = executableDirectory(argv0);
    const std::vector<fs::path> candidates = {
        exeDir / "ffmpeg.exe",
        fs::current_path() / "ffmpeg.exe",
        fs::current_path() / "tools" / "ffmpeg.exe"
    };

    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate.string();
        }
    }
    return "ffmpeg";
}

void resetGeneratedDirectory(const std::string& path) {
    namespace fs = std::filesystem;
    if (path.empty() || path == "." || path == "..") {
        throw std::runtime_error("Unsafe generated directory: " + path);
    }

    const fs::path absolutePath = fs::absolute(fs::path(path)).lexically_normal();
    if (absolutePath == absolutePath.root_path()) {
        throw std::runtime_error("Refusing to reset filesystem root: " + absolutePath.string());
    }

    if (fs::exists(absolutePath)) {
        fs::remove_all(absolutePath);
    }
    fs::create_directories(absolutePath);
}

std::filesystem::path deriveWorkDirectory(const std::string& outputVideoPath) {
    namespace fs = std::filesystem;
    fs::path outputPath(outputVideoPath);
    fs::path parent = outputPath.parent_path();
    if (parent.empty()) {
        parent = fs::current_path();
    }
    return parent / (outputPath.stem().string() + "_visionflow");
}

std::filesystem::path makeTemporarySourceFrameDirectory() {
    namespace fs = std::filesystem;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("visionflow_source_frames_" + std::to_string(ticks));
}

std::filesystem::path makeTemporaryVfvidFrameDirectory() {
    namespace fs = std::filesystem;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("visionflow_vfvid_frames_" + std::to_string(ticks));
}

std::string deriveFrameInputFolder(const std::string& inputPath,
                                   bool inputIsVideo,
                                   const std::string& ffmpeg,
                                   ScopedDirectoryCleanup& cleanup) {
    if (!inputIsVideo) {
        return inputPath;
    }

    const std::filesystem::path sourceFrameDir = makeTemporarySourceFrameDirectory();
    cleanup.path = sourceFrameDir;
    cleanup.enabled = true;
    extractVideoToBmpFrames(ffmpeg, inputPath, sourceFrameDir.string());
    return sourceFrameDir.string();
}

void extractVideoToBmpFrames(const std::string& ffmpeg,
                             const std::string& inputVideo,
                             const std::string& frameDir) {
    resetGeneratedDirectory(frameDir);
    const std::string pattern = (std::filesystem::path(frameDir) / "frame_%04d.bmp").string();

    std::cout << "[VisionFlow] MP4/video input enabled through external ffmpeg.exe.\n";
    std::cout << "[VisionFlow] ffmpeg is used only for format conversion, not image processing.\n";
    runProcessOrThrow(
        {ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", inputVideo, pattern},
        "Decoding video to BMP frame sequence");
}

std::string findStabilizedFrameDirectory(const std::string& outputDir) {
    const std::string staticDir = vf::PathUtils::join(outputDir, "affine_stable_frames");
    const std::string motionDir = vf::PathUtils::join(outputDir, "cinematic_frames");

    if (std::filesystem::exists(staticDir) && !vf::FrameSequence::listBmpFiles(staticDir).empty()) {
        return staticDir;
    }
    if (std::filesystem::exists(motionDir) && !vf::FrameSequence::listBmpFiles(motionDir).empty()) {
        return motionDir;
    }
    throw std::runtime_error("No stabilized BMP frames found for MP4 encoding.");
}

std::string resolveVfvidOutputPath(const std::string& inputFolder, const std::string& outputVfvid) {
    namespace fs = std::filesystem;
    fs::path inputPath(inputFolder);
    std::string stem = inputPath.filename().string();
    if (stem.empty()) {
        stem = "output";
    }

    if (outputVfvid.empty()) {
        return (fs::current_path() / (stem + ".vfvid")).string();
    }

    fs::path outputPath(outputVfvid);
    if ((fs::exists(outputPath) && fs::is_directory(outputPath)) || outputPath.extension().empty()) {
        if (!fs::exists(outputPath) && outputPath.extension().empty() && outputPath.has_filename()) {
            outputPath.replace_extension(".vfvid");
            return outputPath.string();
        }
        return (outputPath / (stem + ".vfvid")).string();
    }

    if (lowerString(outputPath.extension().string()) != ".vfvid") {
        outputPath.replace_extension(".vfvid");
    }
    return outputPath.string();
}

void encodeBmpFramesToMp4(const std::string& ffmpeg,
                          const std::string& pipelineOutputDir,
                          const std::string& outputVideo,
                          int frameRate) {
    namespace fs = std::filesystem;
    fs::path outputPath(outputVideo);
    if (!outputPath.parent_path().empty()) {
        fs::create_directories(outputPath.parent_path());
    }

    const std::string stableDir = findStabilizedFrameDirectory(pipelineOutputDir);
    const std::string pattern = (fs::path(stableDir) / "affine_stable_%04d.bmp").string();

    runProcessOrThrow(
        {ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-framerate", std::to_string(frameRate),
         "-start_number", "1", "-i", pattern, "-pix_fmt", "yuv420p", "-movflags", "+faststart", outputVideo},
        "Encoding stabilized BMP frames to MP4");
}

void openPathInSystemViewer(const std::string& path) {
#ifdef _WIN32
    const std::string command = "start \"\" " + quoteCommandArg(path);
#elif __APPLE__
    const std::string command = "open " + quoteCommandArg(path);
#else
    const std::string command = "xdg-open " + quoteCommandArg(path);
#endif
    (void)std::system(command.c_str());
}

int runPackCommand(const std::string& inputFolder, const std::string& outputVfvid, std::uint32_t fps) {
    const std::string resolvedOutput = resolveVfvidOutputPath(inputFolder, outputVfvid);
    std::cout << "[VFVID] Packing BMP frames...\n";
    vf::Vfvid::packFromBmpFolder(inputFolder, resolvedOutput, fps);
    const vf::VfvidInfo info = vf::Vfvid::readInfo(resolvedOutput);
    std::cout << "[Done] VFVID saved: " << resolvedOutput << "\n";
    std::cout << "[Info] " << info.width << " x " << info.height
              << ", fps=" << info.fps
              << ", frames=" << info.frameCount << "\n";
    return 0;
}

int runUnpackCommand(const std::string& inputVfvid, const std::string& outputFolder) {
    std::cout << "[VFVID] Unpacking to BMP frames...\n";
    vf::Vfvid::unpackToBmpFolder(inputVfvid, outputFolder);
    const vf::VfvidInfo info = vf::Vfvid::readInfo(inputVfvid);
    std::cout << "[Done] Frames saved to: " << outputFolder << "\n";
    std::cout << "[Info] " << info.width << " x " << info.height
              << ", fps=" << info.fps
              << ", frames=" << info.frameCount << "\n";
    return 0;
}

int runVfvidInfoCommand(const std::string& inputVfvid) {
    const vf::VfvidInfo info = vf::Vfvid::readInfo(inputVfvid);
    std::cout << "VFVID File Info\n";
    std::cout << "File        : " << inputVfvid << "\n";
    std::cout << "Magic       : VFVID01\n";
    std::cout << "Version     : 1\n";
    std::cout << "Width       : " << info.width << "\n";
    std::cout << "Height      : " << info.height << "\n";
    std::cout << "FPS         : " << info.fps << "\n";
    std::cout << "Frame count : " << info.frameCount << "\n";
    std::cout << "Pixel format: RGB24\n";
    std::cout << "Data size   : " << std::fixed << std::setprecision(2)
              << (static_cast<double>(info.dataBytes) / (1024.0 * 1024.0)) << " MB\n";
    return 0;
}

} // namespace vfapp
