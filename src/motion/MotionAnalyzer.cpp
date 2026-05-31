#include "motion/MotionAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "cv/ImageNormalize.h"
#include "io/BmpWriter.h"
#include "io/PathUtils.h"

namespace vf {
namespace {

std::string formatIndex(int index) {
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << index;
    return oss.str();
}

void checkSameSize(const GrayImage& a, const GrayImage& b) {
    if (a.empty() || b.empty()) {
        throw std::runtime_error("MotionAnalyzer expects non-empty gray images.");
    }
    if (a.width != b.width || a.height != b.height) {
        throw std::runtime_error("MotionAnalyzer frame size mismatch.");
    }
}

std::pair<int, int> estimateGlobalShift(
    const GrayImage& a,
    const GrayImage& b,
    int searchRadius,
    int sampleStep,
    float& bestError) {

    checkSameSize(a, b);
    searchRadius = std::max(0, searchRadius);
    sampleStep = std::max(1, sampleStep);

    const int w = a.width;
    const int h = a.height;
    const int margin = searchRadius + 1;

    bestError = std::numeric_limits<float>::max();
    int bestDx = 0;
    int bestDy = 0;

    for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
        for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
            double sum = 0.0;
            int count = 0;

            for (int y = margin; y < h - margin; y += sampleStep) {
                const int y2 = y + dy;
                if (y2 < 0 || y2 >= h) {
                    continue;
                }
                for (int x = margin; x < w - margin; x += sampleStep) {
                    const int x2 = x + dx;
                    if (x2 < 0 || x2 >= w) {
                        continue;
                    }
                    sum += std::abs(a.at(x, y) - b.at(x2, y2));
                    ++count;
                }
            }

            if (count == 0) {
                continue;
            }

            const float error = static_cast<float>(sum / count);
            if (error < bestError) {
                bestError = error;
                bestDx = dx;
                bestDy = dy;
            }
        }
    }

    if (bestError == std::numeric_limits<float>::max()) {
        bestError = 0.0f;
    }
    return {bestDx, bestDy};
}

MotionFrameStats computeStats(
    const GrayImage& diff,
    const GrayImage& a,
    const GrayImage& b,
    int pairIndex,
    const MotionAnalyzerConfig& config) {

    MotionFrameStats stats;
    stats.pairIndex = pairIndex;

    double sum = 0.0;
    double weight = 0.0;
    double weightedX = 0.0;
    double weightedY = 0.0;
    int changed = 0;
    const int total = diff.width * diff.height;

    for (int y = 0; y < diff.height; ++y) {
        for (int x = 0; x < diff.width; ++x) {
            const float d = diff.at(x, y);
            sum += d;
            if (d > config.diffThreshold) {
                ++changed;
                weight += d;
                weightedX += x * d;
                weightedY += y * d;
            }
        }
    }

    stats.meanAbsDiff = total > 0 ? static_cast<float>(sum / total) : 0.0f;
    stats.changedRatio = total > 0 ? static_cast<float>(changed * 100.0 / total) : 0.0f;

    // This is a display score, not a physical unit.
    // A mean difference of 45 or above is treated as very strong motion.
    stats.motionScore = std::min(100.0f, stats.meanAbsDiff / 45.0f * 100.0f);

    if (weight > 1e-6) {
        stats.centerX = static_cast<float>(weightedX / weight);
        stats.centerY = static_cast<float>(weightedY / weight);
    } else {
        stats.centerX = diff.width * 0.5f;
        stats.centerY = diff.height * 0.5f;
    }

    float bestError = 0.0f;
    const auto shift = estimateGlobalShift(a, b, config.searchRadius, config.sampleStep, bestError);
    stats.globalDx = shift.first;
    stats.globalDy = shift.second;
    stats.globalError = bestError;

    return stats;
}

} // namespace

GrayImage MotionAnalyzer::absoluteDifference(const GrayImage& a, const GrayImage& b) {
    checkSameSize(a, b);

    GrayImage diff(a.width, a.height);
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            diff.at(x, y) = std::abs(a.at(x, y) - b.at(x, y));
        }
    }
    return diff;
}

MotionAnalysisResult MotionAnalyzer::analyze(
    const std::vector<GrayImage>& grayFrames,
    const std::string& diffOutputDir,
    const MotionAnalyzerConfig& config) {

    MotionAnalysisResult result;
    if (grayFrames.size() < 2) {
        if (!grayFrames.empty()) {
            result.heatmap = GrayImage(grayFrames.front().width, grayFrames.front().height);
        }
        return result;
    }

    const int width = grayFrames.front().width;
    const int height = grayFrames.front().height;
    GrayImage heatAccum(width, height);

    double sumScore = 0.0;
    double sumChangedRatio = 0.0;
    double sumDx = 0.0;
    double sumDy = 0.0;

    for (std::size_t i = 0; i + 1 < grayFrames.size(); ++i) {
        const GrayImage& a = grayFrames[i];
        const GrayImage& b = grayFrames[i + 1];
        checkSameSize(a, b);

        GrayImage diff = absoluteDifference(a, b);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                heatAccum.at(x, y) += diff.at(x, y);
            }
        }

        MotionFrameStats stats = computeStats(diff, a, b, static_cast<int>(i + 1), config);
        result.stats.push_back(stats);

        sumScore += stats.motionScore;
        sumChangedRatio += stats.changedRatio;
        sumDx += stats.globalDx;
        sumDy += stats.globalDy;

        if (stats.motionScore > result.peakMotionScore) {
            result.peakMotionScore = stats.motionScore;
            result.peakPairIndex = stats.pairIndex;
        }

        const std::string diffName = "diff_" + formatIndex(static_cast<int>(i + 1)) + ".bmp";
        result.diffFrameNames.push_back(diffName);
        BmpWriter::writeGray(PathUtils::join(diffOutputDir, diffName), diff);
    }

    const int pairCount = static_cast<int>(result.stats.size());
    if (pairCount > 0) {
        result.averageMotionScore = static_cast<float>(sumScore / pairCount);
        result.averageChangedRatio = static_cast<float>(sumChangedRatio / pairCount);
        result.averageGlobalDx = static_cast<float>(sumDx / pairCount);
        result.averageGlobalDy = static_cast<float>(sumDy / pairCount);
    }

    result.heatmap = ImageNormalize::normalizeToByteRange(heatAccum);
    return result;
}

void MotionAnalyzer::writeCsv(const std::string& path, const MotionAnalysisResult& result) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot create motion CSV: " + path);
    }

    out << "pair_index,mean_abs_diff,changed_ratio_percent,motion_score,center_x,center_y,global_dx,global_dy,global_error\n";
    out << std::fixed << std::setprecision(4);
    for (const MotionFrameStats& s : result.stats) {
        out << s.pairIndex << ','
            << s.meanAbsDiff << ','
            << s.changedRatio << ','
            << s.motionScore << ','
            << s.centerX << ','
            << s.centerY << ','
            << s.globalDx << ','
            << s.globalDy << ','
            << s.globalError << '\n';
    }
}

void MotionAnalyzer::writeSvgCurve(const std::string& path, const MotionAnalysisResult& result,
                                   int width, int height) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot create motion SVG: " + path);
    }

    const int left = 56;
    const int right = 28;
    const int top = 28;
    const int bottom = 46;
    const int plotW = std::max(1, width - left - right);
    const int plotH = std::max(1, height - top - bottom);
    const int x0 = left;
    const int y0 = top + plotH;

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' ' << height << "\">\n";
    out << "<rect width=\"100%\" height=\"100%\" rx=\"18\" fill=\"#f8fafc\"/>\n";
    out << "<text x=\"" << left << "\" y=\"22\" font-family=\"Segoe UI, Arial\" font-size=\"16\" font-weight=\"700\" fill=\"#1f2937\">Motion Score Curve</text>\n";

    for (int i = 0; i <= 4; ++i) {
        const float value = i * 25.0f;
        const int y = static_cast<int>(y0 - (value / 100.0f) * plotH);
        out << "<line x1=\"" << x0 << "\" y1=\"" << y << "\" x2=\"" << (x0 + plotW)
            << "\" y2=\"" << y << "\" stroke=\"#e5e7eb\" stroke-width=\"1\"/>\n";
        out << "<text x=\"12\" y=\"" << (y + 5) << "\" font-family=\"Segoe UI, Arial\" font-size=\"12\" fill=\"#64748b\">" << static_cast<int>(value) << "</text>\n";
    }

    out << "<line x1=\"" << x0 << "\" y1=\"" << top << "\" x2=\"" << x0 << "\" y2=\"" << y0 << "\" stroke=\"#94a3b8\"/>\n";
    out << "<line x1=\"" << x0 << "\" y1=\"" << y0 << "\" x2=\"" << (x0 + plotW) << "\" y2=\"" << y0 << "\" stroke=\"#94a3b8\"/>\n";

    if (!result.stats.empty()) {
        out << "<polyline fill=\"none\" stroke=\"#2563eb\" stroke-width=\"3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" points=\"";
        const std::size_t n = result.stats.size();
        for (std::size_t i = 0; i < n; ++i) {
            const float xRatio = n == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(n - 1);
            const float score = std::max(0.0f, std::min(100.0f, result.stats[i].motionScore));
            const int x = static_cast<int>(x0 + xRatio * plotW);
            const int y = static_cast<int>(y0 - (score / 100.0f) * plotH);
            out << x << ',' << y << ' ';
        }
        out << "\"/>\n";

        for (std::size_t i = 0; i < n; ++i) {
            const float xRatio = n == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(n - 1);
            const float score = std::max(0.0f, std::min(100.0f, result.stats[i].motionScore));
            const int x = static_cast<int>(x0 + xRatio * plotW);
            const int y = static_cast<int>(y0 - (score / 100.0f) * plotH);
            out << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"3.5\" fill=\"#1d4ed8\"/>\n";
        }
    } else {
        out << "<text x=\"" << (x0 + 20) << "\" y=\"" << (top + plotH / 2)
            << "\" font-family=\"Segoe UI, Arial\" font-size=\"15\" fill=\"#64748b\">No motion data. Need at least two frames.</text>\n";
    }

    out << "<text x=\"" << (x0 + plotW / 2 - 40) << "\" y=\"" << (height - 12)
        << "\" font-family=\"Segoe UI, Arial\" font-size=\"12\" fill=\"#64748b\">Frame Pair Index</text>\n";
    out << "<text x=\"12\" y=\"" << (top + 10)
        << "\" font-family=\"Segoe UI, Arial\" font-size=\"12\" fill=\"#64748b\">Score</text>\n";
    out << "</svg>\n";
}

} // namespace vf
