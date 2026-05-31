#include "app/AutoModeSelector.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

#include "affine/AffineStabilizer.h"
#include "io/PathUtils.h"

namespace vfapp {
namespace {

vf::AffineStabilizerConfig makeAutoAffineConfig() {
    vf::AffineStabilizerConfig autoConfig;
    autoConfig.blockSize = 32;
    autoConfig.blockStep = 32;
    autoConfig.searchRadius = 16;
    autoConfig.sampleStride = 4;
    autoConfig.smoothRadius = 5;
    autoConfig.textureThreshold = 6.0f;
    autoConfig.ransacIterations = 72;
    autoConfig.ransacThreshold = 3.75f;
    return autoConfig;
}

} // namespace

float percentile(std::vector<float> values, float q) {
    if (values.empty()) {
        return 0.0f;
    }
    q = std::max(0.0f, std::min(1.0f, q));
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(std::lround(q * static_cast<float>(values.size() - 1)));
    return values[std::min(index, values.size() - 1)];
}

AutoModeDecision decideAutoMode(const std::vector<vf::GrayImage>& grayFrames) {
    AutoModeDecision decision;
    if (grayFrames.size() < 2) {
        decision.selectedMode = Mode::Statics;
        decision.reason = "Too few frames. Use static because there is no reliable camera path.";
        return decision;
    }

    vf::AffineStabilizerConfig autoConfig = makeAutoAffineConfig();
    const std::vector<vf::AffineMotionSample> samples = vf::AffineStabilizer::estimateMotionSamples(grayFrames, autoConfig, 80);
    decision.totalSamples = static_cast<int>(samples.size());
    decision.frameDiagonal = std::sqrt(static_cast<float>(grayFrames.front().width * grayFrames.front().width +
                                                           grayFrames.front().height * grayFrames.front().height));

    std::vector<float> translationMagnitudes;
    std::vector<float> dxs;
    std::vector<float> dys;
    translationMagnitudes.reserve(samples.size());
    dxs.reserve(samples.size());
    dys.reserve(samples.size());

    double sumTranslation = 0.0;
    double sumRotation = 0.0;
    double sumInlier = 0.0;
    double sumPoints = 0.0;
    double sumDx = 0.0;
    double sumDy = 0.0;

    for (const vf::AffineMotionSample& m : samples) {
        const float dx = m.translationX;
        const float dy = m.translationY;
        const float mag = std::sqrt(dx * dx + dy * dy);
        const bool finite = std::isfinite(dx) && std::isfinite(dy) && std::isfinite(m.rotationDeg) && std::isfinite(m.scale);
        const bool reliable = finite &&
                              m.pointCount >= 4 &&
                              m.inlierRatio >= 0.10f &&
                              mag <= decision.frameDiagonal * 0.18f &&
                              std::abs(m.rotationDeg) <= 8.0f;
        if (!reliable) {
            continue;
        }

        translationMagnitudes.push_back(mag);
        dxs.push_back(dx);
        dys.push_back(dy);
        sumTranslation += mag;
        sumRotation += std::abs(m.rotationDeg);
        sumInlier += m.inlierRatio;
        sumPoints += static_cast<double>(m.pointCount);
        sumDx += dx;
        sumDy += dy;
    }

    decision.usableSamples = static_cast<int>(translationMagnitudes.size());
    if (decision.usableSamples < 3) {
        decision.selectedMode = Mode::Motion;
        decision.reason = "Low-confidence motion estimation. Use motion mode because it is less aggressive and preserves the original camera path.";
        return decision;
    }

    const float n = static_cast<float>(decision.usableSamples);
    decision.averageTranslation = static_cast<float>(sumTranslation / n);
    decision.medianTranslation = percentile(translationMagnitudes, 0.50f);
    decision.p90Translation = percentile(translationMagnitudes, 0.90f);
    decision.pathTranslation = static_cast<float>(sumTranslation);
    decision.netTranslation = static_cast<float>(std::sqrt(sumDx * sumDx + sumDy * sumDy));
    decision.directionConsistency = decision.pathTranslation > 1e-5f ? decision.netTranslation / decision.pathTranslation : 0.0f;
    decision.averageAbsRotationDeg = static_cast<float>(sumRotation / n);
    decision.averageInlierRatio = static_cast<float>(sumInlier / n) * 100.0f;
    decision.averagePointCount = static_cast<float>(sumPoints / n);

    double jitterSum = 0.0;
    int jitterCount = 0;
    for (std::size_t i = 1; i < dxs.size(); ++i) {
        const float ddx = dxs[i] - dxs[i - 1];
        const float ddy = dys[i] - dys[i - 1];
        jitterSum += std::sqrt(ddx * ddx + ddy * ddy);
        ++jitterCount;
    }
    const float averageJitter = jitterCount > 0 ? static_cast<float>(jitterSum / static_cast<double>(jitterCount)) : 0.0f;
    decision.jitterRatio = averageJitter / std::max(0.25f, decision.averageTranslation);

    const bool deliberateCameraMotion =
        (decision.directionConsistency >= 0.48f &&
         decision.netTranslation >= decision.frameDiagonal * 0.035f &&
         decision.averageTranslation >= 1.05f) ||
        (decision.directionConsistency >= 0.55f && decision.p90Translation >= 3.20f) ||
        (decision.averageAbsRotationDeg >= 0.20f && decision.directionConsistency >= 0.40f);

    const bool mostlyStatic =
        (decision.netTranslation <= decision.frameDiagonal * 0.025f &&
         decision.averageTranslation <= 1.70f &&
         decision.p90Translation <= 3.80f &&
         decision.averageAbsRotationDeg <= 0.14f) ||
        (decision.directionConsistency <= 0.32f &&
         decision.averageTranslation <= 2.30f &&
         decision.p90Translation <= 4.80f);

    if (deliberateCameraMotion && !mostlyStatic) {
        decision.selectedMode = Mode::Motion;
        decision.reason = "Consistent global camera path detected. Keep large camera movement and remove high-frequency shake.";
    } else {
        decision.selectedMode = Mode::Statics;
        decision.reason = "No strong intentional camera path detected. Lock the view close to the first frame.";
    }
    return decision;
}

void writeAutoDecisionFile(const std::string& outputDir, const AutoModeDecision& decision) {
    std::ofstream out(vf::PathUtils::join(outputDir, "AUTO_MODE_DECISION.txt"));
    if (!out) {
        throw std::runtime_error("Cannot create AUTO_MODE_DECISION.txt");
    }
    out << "VisionFlow Auto Mode Decision\n\n";
    out << "Selected mode: " << modeName(decision.selectedMode) << "\n";
    out << "Reason: " << decision.reason << "\n\n";
    out << std::fixed << std::setprecision(4);
    out << "Total sampled frame pairs: " << decision.totalSamples << "\n";
    out << "Usable sampled frame pairs: " << decision.usableSamples << "\n";
    out << "Frame diagonal: " << decision.frameDiagonal << " px\n";
    out << "Average translation: " << decision.averageTranslation << " px / pair\n";
    out << "Median translation: " << decision.medianTranslation << " px / pair\n";
    out << "P90 translation: " << decision.p90Translation << " px / pair\n";
    out << "Path translation: " << decision.pathTranslation << " px over sampled pairs\n";
    out << "Net translation: " << decision.netTranslation << " px over sampled pairs\n";
    out << "Direction consistency: " << decision.directionConsistency << "\n";
    out << "Average abs rotation: " << decision.averageAbsRotationDeg << " deg / pair\n";
    out << "Jitter ratio: " << decision.jitterRatio << "\n";
    out << "Average RANSAC inlier ratio: " << decision.averageInlierRatio << "%\n";
    out << "Average match point count: " << decision.averagePointCount << "\n";
}

} // namespace vfapp
