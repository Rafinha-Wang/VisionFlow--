#pragma once

#include <string>
#include <vector>

#include "core/GrayImage.h"
#include "core/Image.h"

namespace vf {

struct AffineTransform2D {
    float a = 1.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;
};

struct AffineMotionSample {
    int pairIndex = 0;
    AffineTransform2D transform;
    float translationX = 0.0f;
    float translationY = 0.0f;
    float rotationDeg = 0.0f;
    float scale = 1.0f;
    float reprojectionError = 0.0f;
    float inlierRatio = 0.0f;
    int pointCount = 0;
    int inlierCount = 0;
};


struct StabilizationMetrics {
    double averageTranslation = 0.0;
    double p90Translation = 0.0;
    double jitterRms = 0.0;
    double trajectorySmoothness = 0.0;
    int sampleCount = 0;
};

struct AffinePathPoint {
    int frameIndex = 0;
    float rawX = 0.0f;
    float rawY = 0.0f;
    float rawRotationDeg = 0.0f;
    float rawLogScale = 0.0f;

    float smoothX = 0.0f;
    float smoothY = 0.0f;
    float smoothRotationDeg = 0.0f;
    float smoothLogScale = 0.0f;

    float correctionX = 0.0f;
    float correctionY = 0.0f;
    float correctionRotationDeg = 0.0f;
    float correctionScale = 1.0f;
};

struct AffineStabilizerConfig {
    int blockSize = 32;
    int blockStep = 32;
    int searchRadius = 12;
    int sampleStride = 4;
    int smoothRadius = 5;
    float textureThreshold = 6.0f;
    int ransacIterations = 96;
    float ransacThreshold = 3.25f;
    int borderPadding = 14;
    float maxCropRatio = 0.20f;
    bool fixedCanvasCrop = true;
    float correctionStrength = 1.0f;
    float lockToOriginStrength = 0.0f;
    bool virtualGimbalEnabled = true;
    float gimbalPositionStiffness = 0.070f;
    float gimbalPositionDamping = 0.840f;
    float gimbalRotationStiffness = 0.045f;
    float gimbalRotationDamping = 0.820f;
    float gimbalScaleStiffness = 0.050f;
    float gimbalScaleDamping = 0.850f;
    float maxGimbalPositionSpeed = 3.20f;
    float maxGimbalPositionAccel = 0.85f;
    float maxGimbalRotationSpeed = 0.42f;
    float maxGimbalRotationAccel = 0.12f;
    float maxGimbalScaleSpeed = 0.020f;
    float maxGimbalScaleAccel = 0.006f;
    bool smoothCorrections = true;
    int correctionSmoothRadius = 2;
    bool cropSafeCorrection = true;
};

struct AffineStabilizationResult {
    std::vector<AffineMotionSample> affineMotion;
    std::vector<AffinePathPoint> affinePath;
    std::vector<std::string> stableFrameNames;

    std::string firstStableImage;
    std::string middleStableImage;
    std::string lastStableImage;
    std::string firstCompareImage;
    std::string middleCompareImage;
    std::string lastCompareImage;

    float averageAbsRotationDeg = 0.0f;
    float peakAbsRotationDeg = 0.0f;
    float averageScaleDriftPercent = 0.0f;
    float averageInlierRatio = 0.0f;
    float rawAffineShake = 0.0f;
    float smoothAffineShake = 0.0f;
    float affineReductionPercent = 0.0f;
    int fixedCropX = 0;
    int fixedCropY = 0;
    float autoZoomPercent = 0.0f;
};

class AffineStabilizer {
public:
    static AffineStabilizationResult stabilize(
        const std::vector<Image>& frames,
        const std::vector<GrayImage>& grayFrames,
        const std::string& stableOutputDir,
        const std::string& compareOutputDir,
        const AffineStabilizerConfig& config = AffineStabilizerConfig());

    static std::vector<AffineMotionSample> estimateMotionSamples(
        const std::vector<GrayImage>& grayFrames,
        const AffineStabilizerConfig& config = AffineStabilizerConfig(),
        int maxPairCount = 80);

    static StabilizationMetrics computeStabilizationMetrics(
        const std::vector<AffineMotionSample>& samples);

    static void writeAffineMotionCsv(const std::string& path, const AffineStabilizationResult& result);
    static void writeAffinePathCsv(const std::string& path, const AffineStabilizationResult& result);
    static void writeRotationCurveSvg(const std::string& path, const AffineStabilizationResult& result,
                                      int width = 980, int height = 340);
    static void writeScaleCurveSvg(const std::string& path, const AffineStabilizationResult& result,
                                   int width = 980, int height = 340);
    static void writeInlierCurveSvg(const std::string& path, const AffineStabilizationResult& result,
                                    int width = 980, int height = 340);
};

} // namespace vf
