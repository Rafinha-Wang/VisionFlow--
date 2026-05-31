#pragma once

#include <string>
#include <vector>

#include "core/GrayImage.h"

namespace vf {

struct MotionFrameStats {
    int pairIndex = 0;          // 1 means frame_0001 -> frame_0002
    float meanAbsDiff = 0.0f;   // average absolute pixel difference, range roughly 0..255
    float changedRatio = 0.0f;  // percent of pixels above threshold, range 0..100
    float motionScore = 0.0f;   // normalized score, range 0..100
    float centerX = 0.0f;       // weighted motion center x
    float centerY = 0.0f;       // weighted motion center y
    int globalDx = 0;           // rough translational shift from frame A to frame B
    int globalDy = 0;
    float globalError = 0.0f;   // average SAD error for the selected global shift
};

struct MotionAnalysisResult {
    std::vector<MotionFrameStats> stats;
    GrayImage heatmap;
    std::vector<std::string> diffFrameNames;
    float averageMotionScore = 0.0f;
    float peakMotionScore = 0.0f;
    int peakPairIndex = 0;
    float averageChangedRatio = 0.0f;
    float averageGlobalDx = 0.0f;
    float averageGlobalDy = 0.0f;
};

struct MotionAnalyzerConfig {
    float diffThreshold = 20.0f;
    int searchRadius = 8;
    int sampleStep = 4;
};

class MotionAnalyzer {
public:
    static GrayImage absoluteDifference(const GrayImage& a, const GrayImage& b);

    static MotionAnalysisResult analyze(
        const std::vector<GrayImage>& grayFrames,
        const std::string& diffOutputDir,
        const MotionAnalyzerConfig& config = MotionAnalyzerConfig());

    static void writeCsv(const std::string& path, const MotionAnalysisResult& result);
    static void writeSvgCurve(const std::string& path, const MotionAnalysisResult& result,
                              int width = 980, int height = 320);
};

} // namespace vf
