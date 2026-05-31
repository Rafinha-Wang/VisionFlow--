#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace vfapp {

enum class Mode {
    Auto,
    Statics,
    Motion
};

struct AutoModeDecision {
    Mode selectedMode = Mode::Motion;
    std::string reason;
    int totalSamples = 0;
    int usableSamples = 0;
    float averageTranslation = 0.0f;
    float medianTranslation = 0.0f;
    float p90Translation = 0.0f;
    float pathTranslation = 0.0f;
    float netTranslation = 0.0f;
    float directionConsistency = 0.0f;
    float averageAbsRotationDeg = 0.0f;
    float jitterRatio = 0.0f;
    float averageInlierRatio = 0.0f;
    float averagePointCount = 0.0f;
    float frameDiagonal = 0.0f;
};

struct ProcessingSummary {
    bool found = false;
    int frameCount = 0;
    double inputJitter = 0.0;
    double outputJitter = 0.0;
    double improvementPercent = 0.0;
    std::string activeMode;
    std::string reportPath;
};

void printUsage();

std::string lowerString(std::string s);
bool parseMode(const std::string& value, Mode& mode);
Mode parseModeOrThrow(const std::string& value);
bool isModeWord(const std::string& value);

std::string pathExtensionLower(const std::string& path);
bool isVideoInputPath(const std::string& path);
bool isMp4OutputPath(const std::string& path);
bool isVfvidPath(const std::string& path);
bool isContainerOutputPath(const std::string& path);

std::string trimString(std::string value);
std::string trimPromptPath(std::string value);
bool startsWith(const std::string& value, const std::string& prefix);
std::string htmlEscape(const std::string& value);

std::string reportPathForOutput(const std::string& outputPath);
std::filesystem::path pipelineOutputDirectoryFor(const std::string& outputPath);
ProcessingSummary readProcessingSummary(const std::string& outputPath);

std::string formatIndex(int index);
std::string htmlRelative(const std::string& folder, const std::string& fileName);
const char* modeName(Mode mode);
const char* modeLabel(Mode mode);
std::string formatFixed(double value, int precision);
std::string shortenMiddle(const std::string& value, std::size_t maxLength);
std::string padRight(std::string value, std::size_t width);

} // namespace vfapp
