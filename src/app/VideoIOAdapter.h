#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vfapp {

struct ScopedDirectoryCleanup {
    std::filesystem::path path;
    bool enabled = false;

    ~ScopedDirectoryCleanup();
};

std::string quoteCommandArg(const std::string& value);
void runProcessOrThrow(const std::vector<std::string>& args, const std::string& action);
std::string executableDirectory(const char* argv0);
std::string findFfmpegExecutable(const char* argv0);
void resetGeneratedDirectory(const std::string& path);
std::filesystem::path deriveWorkDirectory(const std::string& outputVideoPath);
std::filesystem::path makeTemporarySourceFrameDirectory();
std::filesystem::path makeTemporaryVfvidFrameDirectory();
std::string deriveFrameInputFolder(const std::string& inputPath,
                                   bool inputIsVideo,
                                   const std::string& ffmpeg,
                                   ScopedDirectoryCleanup& cleanup);
void extractVideoToBmpFrames(const std::string& ffmpeg,
                             const std::string& inputVideo,
                             const std::string& frameDir);
std::string findStabilizedFrameDirectory(const std::string& outputDir);
std::string resolveVfvidOutputPath(const std::string& inputFolder, const std::string& outputVfvid);
void encodeBmpFramesToMp4(const std::string& ffmpeg,
                          const std::string& pipelineOutputDir,
                          const std::string& outputVideo,
                          int frameRate = 30);
void openPathInSystemViewer(const std::string& path);

int runPackCommand(const std::string& inputFolder, const std::string& outputVfvid, std::uint32_t fps);
int runUnpackCommand(const std::string& inputVfvid, const std::string& outputFolder);
int runVfvidInfoCommand(const std::string& inputVfvid);

} // namespace vfapp
