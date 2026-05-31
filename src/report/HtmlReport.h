#pragma once

#include <string>

namespace vf {

struct ReportMetrics {
    double averageTranslation = 0.0;
    double p90Translation = 0.0;
    double jitterRms = 0.0;
    double trajectorySmoothness = 0.0;
    int sampleCount = 0;
};

struct SequenceReportInfo {
    std::string title;
    int frameCount = 0;
    int width = 0;
    int height = 0;
    double elapsedMs = 0.0;
    std::string modeName;

    std::string firstFrameImage;
    std::string middleFrameImage;
    std::string lastFrameImage;

    std::string middleGrayImage;
    std::string middleEdgeImage;
    std::string middleDiffImage;

    std::string motionCurveImage;
    std::string motionHeatmapImage;

    std::string affineRotationSvg;
    std::string affineScaleSvg;
    std::string affineInlierSvg;

    std::string firstOutputImage;
    std::string middleOutputImage;
    std::string lastOutputImage;

    std::string firstCompareImage;
    std::string middleCompareImage;
    std::string lastCompareImage;

    bool hasStabilizationMetrics = false;
    ReportMetrics inputMetrics;
    ReportMetrics outputMetrics;
};

class HtmlReport {
public:
    static void generateSequence(const std::string& path, const SequenceReportInfo& info);
};

} // namespace vf
