#include "affine/AffineStabilizer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>

#include "io/BmpWriter.h"
#include "io/PathUtils.h"

namespace vf {
namespace {

struct MatchPoint {
    float x = 0.0f;
    float y = 0.0f;
    float xp = 0.0f;
    float yp = 0.0f;
    float error = 0.0f;
    float texture = 0.0f;
};

std::string formatIndex(int index) {
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << index;
    return oss.str();
}

constexpr float PI_F = 3.14159265358979323846f;

void checkSameSize(const GrayImage& a, const GrayImage& b) {
    if (a.empty() || b.empty()) {
        throw std::runtime_error("AffineStabilizer expects non-empty gray frames.");
    }
    if (a.width != b.width || a.height != b.height) {
        throw std::runtime_error("AffineStabilizer frame size mismatch.");
    }
}

float blockTexture(const GrayImage& img, int x0, int y0, int blockSize, int sampleStride) {
    double sum = 0.0;
    double sumSq = 0.0;
    int count = 0;
    for (int y = 0; y < blockSize; y += sampleStride) {
        for (int x = 0; x < blockSize; x += sampleStride) {
            const float v = img.at(x0 + x, y0 + y);
            sum += v;
            sumSq += v * v;
            ++count;
        }
    }
    if (count <= 1) {
        return 0.0f;
    }
    const double mean = sum / count;
    const double variance = std::max(0.0, sumSq / count - mean * mean);
    return static_cast<float>(std::sqrt(variance));
}

float blockSad(const GrayImage& a, const GrayImage& b, int x0, int y0, int dx, int dy,
               int blockSize, int sampleStride) {
    double sum = 0.0;
    int count = 0;
    for (int y = 0; y < blockSize; y += sampleStride) {
        for (int x = 0; x < blockSize; x += sampleStride) {
            const float va = a.at(x0 + x, y0 + y);
            const float vb = b.at(x0 + dx + x, y0 + dy + y);
            sum += std::abs(va - vb);
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / count) : std::numeric_limits<float>::max();
}

std::vector<MatchPoint> collectMatches(const GrayImage& a, const GrayImage& b, const AffineStabilizerConfig& config) {
    checkSameSize(a, b);
    AffineStabilizerConfig cfg = config;
    cfg.blockSize = std::max(8, cfg.blockSize);
    cfg.blockStep = std::max(4, cfg.blockStep);
    cfg.searchRadius = std::max(0, cfg.searchRadius);
    cfg.sampleStride = std::max(1, cfg.sampleStride);

    const int w = a.width;
    const int h = a.height;
    const int margin = cfg.searchRadius + 2;

    std::vector<MatchPoint> points;
    for (int y = margin; y + cfg.blockSize + margin < h; y += cfg.blockStep) {
        for (int x = margin; x + cfg.blockSize + margin < w; x += cfg.blockStep) {
            const float texture = blockTexture(a, x, y, cfg.blockSize, cfg.sampleStride);
            if (texture < cfg.textureThreshold) {
                continue;
            }

            float bestError = std::numeric_limits<float>::max();
            int bestDx = 0;
            int bestDy = 0;
            for (int dy = -cfg.searchRadius; dy <= cfg.searchRadius; ++dy) {
                const int y2 = y + dy;
                if (y2 < 0 || y2 + cfg.blockSize >= h) {
                    continue;
                }
                for (int dx = -cfg.searchRadius; dx <= cfg.searchRadius; ++dx) {
                    const int x2 = x + dx;
                    if (x2 < 0 || x2 + cfg.blockSize >= w) {
                        continue;
                    }
                    const float error = blockSad(a, b, x, y, dx, dy, cfg.blockSize, cfg.sampleStride);
                    if (error < bestError) {
                        bestError = error;
                        bestDx = dx;
                        bestDy = dy;
                    }
                }
            }

            if (bestError < std::numeric_limits<float>::max()) {
                const float cx = static_cast<float>(x) + 0.5f * static_cast<float>(cfg.blockSize);
                const float cy = static_cast<float>(y) + 0.5f * static_cast<float>(cfg.blockSize);
                points.push_back(MatchPoint{cx, cy, cx + static_cast<float>(bestDx), cy + static_cast<float>(bestDy), bestError, texture});
            }
        }
    }
    return points;
}

bool solve3x3(float a[3][4], float out[3]) {
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        float best = std::abs(a[col][col]);
        for (int row = col + 1; row < 3; ++row) {
            const float v = std::abs(a[row][col]);
            if (v > best) {
                best = v;
                pivot = row;
            }
        }
        if (best < 1e-6f) {
            return false;
        }
        if (pivot != col) {
            for (int k = col; k < 4; ++k) {
                std::swap(a[col][k], a[pivot][k]);
            }
        }
        const float div = a[col][col];
        for (int k = col; k < 4; ++k) {
            a[col][k] /= div;
        }
        for (int row = 0; row < 3; ++row) {
            if (row == col) {
                continue;
            }
            const float factor = a[row][col];
            for (int k = col; k < 4; ++k) {
                a[row][k] -= factor * a[col][k];
            }
        }
    }
    out[0] = a[0][3];
    out[1] = a[1][3];
    out[2] = a[2][3];
    return true;
}

bool fitAffineLeastSquares(const std::vector<MatchPoint>& points,
                           const std::vector<int>& indices,
                           AffineTransform2D& transform) {
    if (indices.size() < 3) {
        return false;
    }

    double ata[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    double atx[3] = {0.0, 0.0, 0.0};
    double aty[3] = {0.0, 0.0, 0.0};

    for (int idx : indices) {
        const MatchPoint& p = points[static_cast<std::size_t>(idx)];
        const double v[3] = {p.x, p.y, 1.0};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                ata[r][c] += v[r] * v[c];
            }
            atx[r] += v[r] * p.xp;
            aty[r] += v[r] * p.yp;
        }
    }

    float augX[3][4];
    float augY[3][4];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            augX[r][c] = static_cast<float>(ata[r][c]);
            augY[r][c] = static_cast<float>(ata[r][c]);
        }
        augX[r][3] = static_cast<float>(atx[r]);
        augY[r][3] = static_cast<float>(aty[r]);
    }

    float sx[3] = {0.0f, 0.0f, 0.0f};
    float sy[3] = {0.0f, 0.0f, 0.0f};
    if (!solve3x3(augX, sx) || !solve3x3(augY, sy)) {
        return false;
    }

    transform.a = sx[0];
    transform.b = sx[1];
    transform.tx = sx[2];
    transform.c = sy[0];
    transform.d = sy[1];
    transform.ty = sy[2];
    return true;
}

bool fitAffineFromThree(const std::vector<MatchPoint>& points, int i0, int i1, int i2, AffineTransform2D& transform) {
    std::vector<int> idx = {i0, i1, i2};
    return fitAffineLeastSquares(points, idx, transform);
}

float reprojectionError(const AffineTransform2D& t, const MatchPoint& p) {
    const float px = t.a * p.x + t.b * p.y + t.tx;
    const float py = t.c * p.x + t.d * p.y + t.ty;
    const float dx = px - p.xp;
    const float dy = py - p.yp;
    return std::sqrt(dx * dx + dy * dy);
}

void decomposeAffine(const AffineTransform2D& t,
                     int width,
                     int height,
                     float& tx,
                     float& ty,
                     float& rotationDeg,
                     float& scale) {
    // The least-squares affine matrix is estimated in image coordinates whose
    // origin is the top-left corner: p' = A * p + t.  Later we render the
    // stabilization as a rotation/scale around the image center.  Reusing
    // t.tx/t.ty directly is wrong whenever A contains rotation or scale,
    // because that top-left translation also includes the center offset.
    // Convert it to the displacement of the image center first.
    const float cx = (static_cast<float>(width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(height) - 1.0f) * 0.5f;
    const float mappedCx = t.a * cx + t.b * cy + t.tx;
    const float mappedCy = t.c * cx + t.d * cy + t.ty;
    tx = mappedCx - cx;
    ty = mappedCy - cy;

    rotationDeg = std::atan2(t.c, t.a) * 180.0f / PI_F;
    const float sx = std::sqrt(t.a * t.a + t.c * t.c);
    const float sy = std::sqrt(t.b * t.b + t.d * t.d);
    scale = std::max(0.25f, 0.5f * (sx + sy));
}

AffineMotionSample estimateAffinePair(const GrayImage& a,
                                      const GrayImage& b,
                                      int pairIndex,
                                      const AffineStabilizerConfig& config) {
    std::vector<MatchPoint> points = collectMatches(a, b, config);

    AffineMotionSample sample;
    sample.pairIndex = pairIndex;
    sample.pointCount = static_cast<int>(points.size());
    if (points.size() < 3) {
        return sample;
    }

    // Sort by matching error and keep the stronger half for RANSAC. This is a
    // simple confidence prior before geometric verification.
    std::sort(points.begin(), points.end(), [](const MatchPoint& lhs, const MatchPoint& rhs) {
        return lhs.error < rhs.error;
    });
    const std::size_t keep = std::max<std::size_t>(3, points.size() * 3 / 4);
    points.resize(keep);
    sample.pointCount = static_cast<int>(points.size());

    std::mt19937 rng(static_cast<unsigned int>(1337 + pairIndex * 17));
    std::uniform_int_distribution<int> dist(0, static_cast<int>(points.size()) - 1);

    int bestInliers = -1;
    float bestMeanError = std::numeric_limits<float>::max();
    AffineTransform2D bestTransform;
    std::vector<int> bestIndices;

    const int iterations = std::max(16, config.ransacIterations);
    for (int iter = 0; iter < iterations; ++iter) {
        int i0 = dist(rng);
        int i1 = dist(rng);
        int i2 = dist(rng);
        if (i0 == i1 || i0 == i2 || i1 == i2) {
            continue;
        }
        AffineTransform2D candidate;
        if (!fitAffineFromThree(points, i0, i1, i2, candidate)) {
            continue;
        }

        std::vector<int> inliers;
        double errorSum = 0.0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const float err = reprojectionError(candidate, points[i]);
            if (err <= config.ransacThreshold) {
                inliers.push_back(static_cast<int>(i));
                errorSum += err;
            }
        }
        const int inlierCount = static_cast<int>(inliers.size());
        const float meanError = inlierCount > 0 ? static_cast<float>(errorSum / inlierCount) : std::numeric_limits<float>::max();
        if (inlierCount > bestInliers || (inlierCount == bestInliers && meanError < bestMeanError)) {
            bestInliers = inlierCount;
            bestMeanError = meanError;
            bestTransform = candidate;
            bestIndices = std::move(inliers);
        }
    }

    if (bestIndices.size() >= 3) {
        AffineTransform2D refined;
        if (fitAffineLeastSquares(points, bestIndices, refined)) {
            bestTransform = refined;
            double errorSum = 0.0;
            for (int idx : bestIndices) {
                errorSum += reprojectionError(bestTransform, points[static_cast<std::size_t>(idx)]);
            }
            bestMeanError = static_cast<float>(errorSum / static_cast<double>(bestIndices.size()));
        }
    } else {
        std::vector<int> all(points.size());
        std::iota(all.begin(), all.end(), 0);
        if (!fitAffineLeastSquares(points, all, bestTransform)) {
            return sample;
        }
        bestIndices = std::move(all);
        double errorSum = 0.0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            errorSum += reprojectionError(bestTransform, points[i]);
        }
        bestMeanError = static_cast<float>(errorSum / static_cast<double>(points.size()));
    }

    sample.transform = bestTransform;
    sample.reprojectionError = bestMeanError;
    sample.inlierCount = static_cast<int>(bestIndices.size());
    sample.inlierRatio = points.empty() ? 0.0f : static_cast<float>(bestIndices.size()) / static_cast<float>(points.size());
    decomposeAffine(bestTransform, a.width, a.height, sample.translationX, sample.translationY, sample.rotationDeg, sample.scale);
    return sample;
}

float averageStepMagnitude(const std::vector<float>& xs, const std::vector<float>& ys) {
    if (xs.size() < 2 || ys.size() < 2 || xs.size() != ys.size()) {
        return 0.0f;
    }
    double sum = 0.0;
    for (std::size_t i = 1; i < xs.size(); ++i) {
        const float dx = xs[i] - xs[i - 1];
        const float dy = ys[i] - ys[i - 1];
        sum += std::sqrt(dx * dx + dy * dy);
    }
    return static_cast<float>(sum / static_cast<double>(xs.size() - 1));
}

float clampDelta(float value, float limit) {
    limit = std::max(0.0f, limit);
    if (value > limit) return limit;
    if (value < -limit) return -limit;
    return value;
}

float limitValueStep(float previous, float current, float maxStep) {
    return previous + clampDelta(current - previous, maxStep);
}

void limitVelocityPair(float& vx, float& vy, float maxSpeed) {
    maxSpeed = std::max(0.0f, maxSpeed);
    const float mag = std::sqrt(vx * vx + vy * vy);
    if (mag > maxSpeed && mag > 1e-6f) {
        const float scale = maxSpeed / mag;
        vx *= scale;
        vy *= scale;
    }
}

std::vector<float> movingAverage1D(const std::vector<float>& values, int radius) {
    if (values.empty()) return {};
    radius = std::max(0, radius);
    std::vector<float> out(values.size(), 0.0f);
    for (int i = 0; i < static_cast<int>(values.size()); ++i) {
        const int from = std::max(0, i - radius);
        const int to = std::min(static_cast<int>(values.size()) - 1, i + radius);
        double sum = 0.0;
        int count = 0;
        for (int j = from; j <= to; ++j) {
            sum += values[static_cast<std::size_t>(j)];
            ++count;
        }
        out[static_cast<std::size_t>(i)] = count == 0 ? values[static_cast<std::size_t>(i)] : static_cast<float>(sum / count);
    }
    return out;
}

void smoothCorrectionsInPlace(std::vector<AffinePathPoint>& path, int radius) {
    if (path.empty() || radius <= 0) return;
    std::vector<float> xs;
    std::vector<float> ys;
    std::vector<float> rs;
    std::vector<float> ss;
    xs.reserve(path.size());
    ys.reserve(path.size());
    rs.reserve(path.size());
    ss.reserve(path.size());
    for (const AffinePathPoint& p : path) {
        xs.push_back(p.correctionX);
        ys.push_back(p.correctionY);
        rs.push_back(p.correctionRotationDeg);
        ss.push_back(std::log(std::max(0.05f, p.correctionScale)));
    }
    xs = movingAverage1D(xs, radius);
    ys = movingAverage1D(ys, radius);
    rs = movingAverage1D(rs, radius);
    ss = movingAverage1D(ss, radius);
    for (std::size_t i = 0; i < path.size(); ++i) {
        path[i].correctionX = xs[i];
        path[i].correctionY = ys[i];
        path[i].correctionRotationDeg = rs[i];
        path[i].correctionScale = std::exp(ss[i]);
    }
}

void enforceCropSafeCorrections(std::vector<AffinePathPoint>& path,
                                int width,
                                int height,
                                const AffineStabilizerConfig& config) {
    if (!config.cropSafeCorrection || path.empty() || width <= 0 || height <= 0) return;

    const float radius = 0.5f * std::sqrt(static_cast<float>(width * width + height * height));
    const float maxAllowedX = static_cast<float>(width) * std::max(0.0f, config.maxCropRatio);
    const float maxAllowedY = static_cast<float>(height) * std::max(0.0f, config.maxCropRatio);
    const float availableX = std::max(0.0f, maxAllowedX - static_cast<float>(std::max(0, config.borderPadding)));
    const float availableY = std::max(0.0f, maxAllowedY - static_cast<float>(std::max(0, config.borderPadding)));

    for (AffinePathPoint& p : path) {
        const float rotRad = std::abs(p.correctionRotationDeg) * PI_F / 180.0f;
        const float scaleLog = std::log(std::max(0.05f, p.correctionScale));
        const float scaleDelta = std::abs(std::exp(scaleLog) - 1.0f);
        const float affineMargin = std::sin(rotRad) * radius + scaleDelta * radius;
        const float needX = std::abs(p.correctionX) + affineMargin;
        const float needY = std::abs(p.correctionY) + affineMargin;

        float alpha = 1.0f;
        if (needX > availableX && needX > 1e-4f) {
            alpha = std::min(alpha, availableX / needX);
        }
        if (needY > availableY && needY > 1e-4f) {
            alpha = std::min(alpha, availableY / needY);
        }

        alpha = std::max(0.0f, std::min(1.0f, alpha));
        if (alpha < 1.0f) {
            p.correctionX *= alpha;
            p.correctionY *= alpha;
            p.correctionRotationDeg *= alpha;
            p.correctionScale = std::exp(scaleLog * alpha);
        }
    }
}

std::vector<AffinePathPoint> buildAffinePath(const std::vector<AffineMotionSample>& motion,
                                             int frameCount,
                                             int smoothRadius,
                                             const AffineStabilizerConfig& config) {
    std::vector<AffinePathPoint> path(static_cast<std::size_t>(frameCount));
    if (frameCount <= 0) {
        return path;
    }
    path[0].frameIndex = 1;
    for (int i = 1; i < frameCount; ++i) {
        path[static_cast<std::size_t>(i)].frameIndex = i + 1;
        const AffineMotionSample& m = motion[static_cast<std::size_t>(i - 1)];
        path[static_cast<std::size_t>(i)].rawX = path[static_cast<std::size_t>(i - 1)].rawX + m.translationX;
        path[static_cast<std::size_t>(i)].rawY = path[static_cast<std::size_t>(i - 1)].rawY + m.translationY;
        path[static_cast<std::size_t>(i)].rawRotationDeg = path[static_cast<std::size_t>(i - 1)].rawRotationDeg + m.rotationDeg;
        path[static_cast<std::size_t>(i)].rawLogScale = path[static_cast<std::size_t>(i - 1)].rawLogScale + std::log(std::max(0.25f, m.scale));
    }

    smoothRadius = std::max(0, smoothRadius);
    for (int i = 0; i < frameCount; ++i) {
        const int from = std::max(0, i - smoothRadius);
        const int to = std::min(frameCount - 1, i + smoothRadius);
        double sx = 0.0;
        double sy = 0.0;
        double sr = 0.0;
        double ss = 0.0;
        int count = 0;
        for (int j = from; j <= to; ++j) {
            const AffinePathPoint& p = path[static_cast<std::size_t>(j)];
            sx += p.rawX;
            sy += p.rawY;
            sr += p.rawRotationDeg;
            ss += p.rawLogScale;
            ++count;
        }
        AffinePathPoint& p = path[static_cast<std::size_t>(i)];
        p.smoothX = static_cast<float>(sx / count);
        p.smoothY = static_cast<float>(sy / count);
        p.smoothRotationDeg = static_cast<float>(sr / count);
        p.smoothLogScale = static_cast<float>(ss / count);
    }

    if (config.virtualGimbalEnabled && frameCount > 0) {
        float gx = path[0].rawX;
        float gy = path[0].rawY;
        float gr = path[0].rawRotationDeg;
        float gs = path[0].rawLogScale;
        float vx = 0.0f;
        float vy = 0.0f;
        float vr = 0.0f;
        float vs = 0.0f;

        float prevVx = 0.0f;
        float prevVy = 0.0f;
        float prevVr = 0.0f;
        float prevVs = 0.0f;

        for (int i = 0; i < frameCount; ++i) {
            const AffinePathPoint& raw = path[static_cast<std::size_t>(i)];

            float desiredVx = vx + (raw.rawX - gx) * config.gimbalPositionStiffness;
            float desiredVy = vy + (raw.rawY - gy) * config.gimbalPositionStiffness;
            desiredVx *= config.gimbalPositionDamping;
            desiredVy *= config.gimbalPositionDamping;

            desiredVx = limitValueStep(prevVx, desiredVx, config.maxGimbalPositionAccel);
            desiredVy = limitValueStep(prevVy, desiredVy, config.maxGimbalPositionAccel);
            limitVelocityPair(desiredVx, desiredVy, config.maxGimbalPositionSpeed);
            vx = desiredVx;
            vy = desiredVy;
            gx += vx;
            gy += vy;
            prevVx = vx;
            prevVy = vy;

            float desiredVr = vr + (raw.rawRotationDeg - gr) * config.gimbalRotationStiffness;
            desiredVr *= config.gimbalRotationDamping;
            desiredVr = limitValueStep(prevVr, desiredVr, config.maxGimbalRotationAccel);
            desiredVr = clampDelta(desiredVr, config.maxGimbalRotationSpeed);
            vr = desiredVr;
            gr += vr;
            prevVr = vr;

            float desiredVs = vs + (raw.rawLogScale - gs) * config.gimbalScaleStiffness;
            desiredVs *= config.gimbalScaleDamping;
            desiredVs = limitValueStep(prevVs, desiredVs, config.maxGimbalScaleAccel);
            desiredVs = clampDelta(desiredVs, config.maxGimbalScaleSpeed);
            vs = desiredVs;
            gs += vs;
            prevVs = vs;

            AffinePathPoint& p = path[static_cast<std::size_t>(i)];
            p.smoothX = gx;
            p.smoothY = gy;
            p.smoothRotationDeg = gr;
            p.smoothLogScale = gs;
        }
    } else {
        for (int i = 0; i < frameCount; ++i) {
            AffinePathPoint& p = path[static_cast<std::size_t>(i)];
            p.smoothX *= (1.0f - config.lockToOriginStrength);
            p.smoothY *= (1.0f - config.lockToOriginStrength);
            p.smoothRotationDeg *= (1.0f - config.lockToOriginStrength);
            p.smoothLogScale *= (1.0f - config.lockToOriginStrength);
        }
    }

    for (int i = 0; i < frameCount; ++i) {
        AffinePathPoint& p = path[static_cast<std::size_t>(i)];
        p.correctionX = (p.smoothX - p.rawX) * config.correctionStrength;
        p.correctionY = (p.smoothY - p.rawY) * config.correctionStrength;
        p.correctionRotationDeg = (p.smoothRotationDeg - p.rawRotationDeg) * config.correctionStrength;
        const float correctedLogScale = p.rawLogScale + (p.smoothLogScale - p.rawLogScale) * config.correctionStrength;
        p.correctionScale = std::exp(correctedLogScale - p.rawLogScale);
    }
    if (config.smoothCorrections) {
        smoothCorrectionsInPlace(path, config.correctionSmoothRadius);
    }
    return path;
}

unsigned char sampleBilinearChannel(const Image& image, float x, float y, int c) {
    if (x < 0.0f || y < 0.0f || x > static_cast<float>(image.width - 1) || y > static_cast<float>(image.height - 1)) {
        return 0;
    }
    const int x0 = std::max(0, std::min(static_cast<int>(std::floor(x)), image.width - 1));
    const int y0 = std::max(0, std::min(static_cast<int>(std::floor(y)), image.height - 1));
    const int x1 = std::min(x0 + 1, image.width - 1);
    const int y1 = std::min(y0 + 1, image.height - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const float v00 = static_cast<float>(image.at(x0, y0, c));
    const float v10 = static_cast<float>(image.at(x1, y0, c));
    const float v01 = static_cast<float>(image.at(x0, y1, c));
    const float v11 = static_cast<float>(image.at(x1, y1, c));
    const float top = v00 * (1.0f - fx) + v10 * fx;
    const float bottom = v01 * (1.0f - fx) + v11 * fx;
    const float v = top * (1.0f - fy) + bottom * fy;
    return static_cast<unsigned char>(std::max(0.0f, std::min(255.0f, v)) + 0.5f);
}

Image warpAffineAroundCenter(const Image& image, float shiftX, float shiftY, float rotationDeg, float scale) {
    Image output(image.width, image.height, image.channels);
    const float cx = (static_cast<float>(image.width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(image.height) - 1.0f) * 0.5f;
    const float radians = rotationDeg * PI_F / 180.0f;
    const float cs = std::cos(radians);
    const float sn = std::sin(radians);
    scale = std::max(0.05f, scale);

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const float dx = static_cast<float>(x) - cx - shiftX;
            const float dy = static_cast<float>(y) - cy - shiftY;
            // Inverse of dest = center + scale * R * (src-center) + t.
            const float ux = dx / scale;
            const float uy = dy / scale;
            const float srcX = cx + cs * ux + sn * uy;
            const float srcY = cy - sn * ux + cs * uy;
            for (int c = 0; c < image.channels; ++c) {
                output.at(x, y, c) = sampleBilinearChannel(image, srcX, srcY, c);
            }
        }
    }
    return output;
}

void makeUniformRelativeCrop(int width, int height, int& cropX, int& cropY) {
    cropX = std::max(0, cropX);
    cropY = std::max(0, cropY);
    if (width <= 0 || height <= 0) {
        cropX = 0;
        cropY = 0;
        return;
    }

    // Keep the crop rectangle in the same aspect ratio as the original frame.
    // The previous implementation used cropX and cropY independently, so a
    // 512x288 frame could be cropped much more in X than Y and then stretched
    // back to 512x288 with different scale factors.  That produced visible
    // non-uniform compression.  Here we choose one relative crop ratio for both
    // axes: cropX / width == cropY / height.
    const float rx = static_cast<float>(cropX) / static_cast<float>(width);
    const float ry = static_cast<float>(cropY) / static_cast<float>(height);
    float r = std::max(rx, ry);

    const float maxRx = static_cast<float>(std::max(0, (width - 16) / 2)) / static_cast<float>(width);
    const float maxRy = static_cast<float>(std::max(0, (height - 16) / 2)) / static_cast<float>(height);
    r = std::min(r, std::min(maxRx, maxRy));

    cropX = static_cast<int>(std::ceil(static_cast<float>(width) * r));
    cropY = static_cast<int>(std::ceil(static_cast<float>(height) * r));
    cropX = std::min(cropX, std::max(0, (width - 16) / 2));
    cropY = std::min(cropY, std::max(0, (height - 16) / 2));
}

Image cropResizeToOriginal(const Image& image, int cropX, int cropY) {
    makeUniformRelativeCrop(image.width, image.height, cropX, cropY);
    if (cropX == 0 && cropY == 0) {
        return image;
    }

    const int srcLeft = cropX;
    const int srcTop = cropY;
    const int srcRight = image.width - 1 - cropX;
    const int srcBottom = image.height - 1 - cropY;
    const float srcW = static_cast<float>(std::max(1, srcRight - srcLeft));
    const float srcH = static_cast<float>(std::max(1, srcBottom - srcTop));

    Image output(image.width, image.height, image.channels);
    for (int y = 0; y < output.height; ++y) {
        const float ty = output.height == 1 ? 0.0f : static_cast<float>(y) / static_cast<float>(output.height - 1);
        const float srcY = static_cast<float>(srcTop) + ty * srcH;
        for (int x = 0; x < output.width; ++x) {
            const float tx = output.width == 1 ? 0.0f : static_cast<float>(x) / static_cast<float>(output.width - 1);
            const float srcX = static_cast<float>(srcLeft) + tx * srcW;
            for (int c = 0; c < output.channels; ++c) {
                output.at(x, y, c) = sampleBilinearChannel(image, srcX, srcY, c);
            }
        }
    }
    return output;
}

Image makeSideBySide(const Image& left, const Image& right) {
    if (left.width != right.width || left.height != right.height || left.channels != right.channels) {
        throw std::runtime_error("Cannot make affine side-by-side comparison with different image sizes.");
    }
    const int gap = 8;
    Image out(left.width * 2 + gap, left.height, left.channels);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            for (int c = 0; c < out.channels; ++c) {
                out.at(x, y, c) = 245;
            }
        }
    }
    for (int y = 0; y < left.height; ++y) {
        for (int x = 0; x < left.width; ++x) {
            for (int c = 0; c < left.channels; ++c) {
                out.at(x, y, c) = left.at(x, y, c);
                out.at(x + left.width + gap, y, c) = right.at(x, y, c);
            }
        }
    }
    return out;
}

void writeCurveSvgCommon(const std::string& path,
                         const std::string& title,
                         const std::vector<float>& values,
                         const std::string& color,
                         int width,
                         int height,
                         bool signedAxis) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot create affine SVG: " + path);
    }
    const int left = 62;
    const int right = 30;
    const int top = 38;
    const int bottom = 50;
    const int plotW = std::max(1, width - left - right);
    const int plotH = std::max(1, height - top - bottom);
    float minV = 0.0f;
    float maxV = 1.0f;
    if (!values.empty()) {
        minV = *std::min_element(values.begin(), values.end());
        maxV = *std::max_element(values.begin(), values.end());
        if (signedAxis) {
            const float m = std::max(1.0f, std::ceil(std::max(std::abs(minV), std::abs(maxV))));
            minV = -m;
            maxV = m;
        } else if (std::abs(maxV - minV) < 1e-5f) {
            maxV = minV + 1.0f;
        }
    }

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height
        << "\" viewBox=\"0 0 " << width << ' ' << height << "\">\n";
    out << "<rect width=\"100%\" height=\"100%\" rx=\"18\" fill=\"#f8fafc\"/>\n";
    out << "<text x=\"" << left << "\" y=\"25\" font-family=\"Segoe UI, Arial\" font-size=\"16\" font-weight=\"700\" fill=\"#1f2937\">" << title << "</text>\n";

    for (int i = 0; i <= 4; ++i) {
        const float v = minV + (maxV - minV) * static_cast<float>(i) / 4.0f;
        const int y = static_cast<int>(top + (maxV - v) / (maxV - minV) * plotH);
        out << "<line x1=\"" << left << "\" y1=\"" << y << "\" x2=\"" << (left + plotW) << "\" y2=\"" << y << "\" stroke=\"#e5e7eb\"/>\n";
        out << "<text x=\"10\" y=\"" << (y + 4) << "\" font-family=\"Segoe UI, Arial\" font-size=\"12\" fill=\"#64748b\">" << std::fixed << std::setprecision(2) << v << "</text>\n";
    }

    if (!values.empty()) {
        out << "<polyline fill=\"none\" stroke=\"" << color << "\" stroke-width=\"3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" points=\"";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const float xr = values.size() == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(values.size() - 1);
            const int x = static_cast<int>(left + xr * plotW);
            const int y = static_cast<int>(top + (maxV - values[i]) / (maxV - minV) * plotH);
            out << x << ',' << y << ' ';
        }
        out << "\"/>\n";
    }
    out << "</svg>\n";
}

} // namespace



StabilizationMetrics AffineStabilizer::computeStabilizationMetrics(
    const std::vector<AffineMotionSample>& samples) {

    StabilizationMetrics metrics;
    std::vector<double> translations;
    translations.reserve(samples.size());

    std::vector<double> posX;
    std::vector<double> posY;
    posX.reserve(samples.size() + 1);
    posY.reserve(samples.size() + 1);

    double x = 0.0;
    double y = 0.0;
    posX.push_back(x);
    posY.push_back(y);

    for (const AffineMotionSample& s : samples) {
        const double dx = static_cast<double>(s.translationX);
        const double dy = static_cast<double>(s.translationY);
        if (!std::isfinite(dx) || !std::isfinite(dy)) {
            continue;
        }

        const double mag = std::sqrt(dx * dx + dy * dy);
        translations.push_back(mag);

        x += dx;
        y += dy;
        posX.push_back(x);
        posY.push_back(y);
    }

    metrics.sampleCount = static_cast<int>(translations.size());
    if (translations.empty()) {
        return metrics;
    }

    const double sum = std::accumulate(translations.begin(), translations.end(), 0.0);
    metrics.averageTranslation = sum / static_cast<double>(translations.size());

    std::vector<double> sortedTranslations = translations;
    std::sort(sortedTranslations.begin(), sortedTranslations.end());
    const std::size_t p90Index = static_cast<std::size_t>(
        std::lround(0.90 * static_cast<double>(sortedTranslations.size() - 1)));
    metrics.p90Translation = sortedTranslations[std::min(p90Index, sortedTranslations.size() - 1)];

    double smoothSum = 0.0;
    int smoothCount = 0;
    for (std::size_t i = 2; i < posX.size(); ++i) {
        const double ax = posX[i] - 2.0 * posX[i - 1] + posX[i - 2];
        const double ay = posY[i] - 2.0 * posY[i - 1] + posY[i - 2];
        smoothSum += std::sqrt(ax * ax + ay * ay);
        ++smoothCount;
    }
    metrics.trajectorySmoothness = smoothCount > 0 ? smoothSum / static_cast<double>(smoothCount) : 0.0;

    const int radius = 5;
    double jitterSum = 0.0;
    int jitterCount = 0;
    for (int i = 0; i < static_cast<int>(posX.size()); ++i) {
        const int left = std::max(0, i - radius);
        const int right = std::min(static_cast<int>(posX.size()) - 1, i + radius);

        double avgX = 0.0;
        double avgY = 0.0;
        int count = 0;
        for (int j = left; j <= right; ++j) {
            avgX += posX[static_cast<std::size_t>(j)];
            avgY += posY[static_cast<std::size_t>(j)];
            ++count;
        }
        avgX /= static_cast<double>(std::max(1, count));
        avgY /= static_cast<double>(std::max(1, count));

        const double jx = posX[static_cast<std::size_t>(i)] - avgX;
        const double jy = posY[static_cast<std::size_t>(i)] - avgY;
        jitterSum += jx * jx + jy * jy;
        ++jitterCount;
    }
    metrics.jitterRms = jitterCount > 0 ? std::sqrt(jitterSum / static_cast<double>(jitterCount)) : 0.0;

    return metrics;
}

std::vector<AffineMotionSample> AffineStabilizer::estimateMotionSamples(
    const std::vector<GrayImage>& grayFrames,
    const AffineStabilizerConfig& config,
    int maxPairCount) {

    std::vector<AffineMotionSample> samples;
    if (grayFrames.size() < 2) {
        return samples;
    }

    const int pairCount = static_cast<int>(grayFrames.size()) - 1;
    maxPairCount = maxPairCount <= 0 ? pairCount : std::max(1, std::min(maxPairCount, pairCount));
    samples.reserve(static_cast<std::size_t>(maxPairCount));

    std::vector<int> pairIndices;
    pairIndices.reserve(static_cast<std::size_t>(maxPairCount));
    if (pairCount <= maxPairCount) {
        for (int i = 0; i < pairCount; ++i) {
            pairIndices.push_back(i);
        }
    } else {
        for (int i = 0; i < maxPairCount; ++i) {
            const float t = maxPairCount == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(maxPairCount - 1);
            const int idx = static_cast<int>(std::lround(t * static_cast<float>(pairCount - 1)));
            if (pairIndices.empty() || pairIndices.back() != idx) {
                pairIndices.push_back(idx);
            }
        }
    }

    for (int idx : pairIndices) {
        samples.push_back(estimateAffinePair(
            grayFrames[static_cast<std::size_t>(idx)],
            grayFrames[static_cast<std::size_t>(idx + 1)],
            idx + 1,
            config));
    }
    return samples;
}

AffineStabilizationResult AffineStabilizer::stabilize(
    const std::vector<Image>& frames,
    const std::vector<GrayImage>& grayFrames,
    const std::string& stableOutputDir,
    const std::string& compareOutputDir,
    const AffineStabilizerConfig& config) {

    if (frames.empty()) {
        return AffineStabilizationResult{};
    }
    if (frames.size() != grayFrames.size()) {
        throw std::runtime_error("AffineStabilizer expects color frames and gray frames with the same count.");
    }

    const int frameCount = static_cast<int>(frames.size());
    AffineStabilizationResult result;
    result.affineMotion.reserve(frameCount > 0 ? static_cast<std::size_t>(frameCount - 1) : 0);

    for (int i = 0; i + 1 < frameCount; ++i) {
        result.affineMotion.push_back(estimateAffinePair(
            grayFrames[static_cast<std::size_t>(i)],
            grayFrames[static_cast<std::size_t>(i + 1)],
            i + 1,
            config));
    }

    result.affinePath = buildAffinePath(result.affineMotion, frameCount, config.smoothRadius, config);
    if (!frames.empty()) {
        enforceCropSafeCorrections(result.affinePath, frames.front().width, frames.front().height, config);
    }

    std::vector<float> rawX;
    std::vector<float> rawY;
    std::vector<float> smoothX;
    std::vector<float> smoothY;
    rawX.reserve(result.affinePath.size());
    rawY.reserve(result.affinePath.size());
    smoothX.reserve(result.affinePath.size());
    smoothY.reserve(result.affinePath.size());
    for (const AffinePathPoint& p : result.affinePath) {
        rawX.push_back(p.rawX);
        rawY.push_back(p.rawY);
        smoothX.push_back(p.smoothX);
        smoothY.push_back(p.smoothY);
    }
    result.rawAffineShake = averageStepMagnitude(rawX, rawY);
    result.smoothAffineShake = averageStepMagnitude(smoothX, smoothY);
    if (result.rawAffineShake > 1e-6f) {
        result.affineReductionPercent = std::max(0.0f, (result.rawAffineShake - result.smoothAffineShake) / result.rawAffineShake * 100.0f);
    }

    double rotSum = 0.0;
    double scaleSum = 0.0;
    double inlierSum = 0.0;
    for (const AffineMotionSample& m : result.affineMotion) {
        const float absRot = std::abs(m.rotationDeg);
        rotSum += absRot;
        result.peakAbsRotationDeg = std::max(result.peakAbsRotationDeg, absRot);
        scaleSum += std::abs(m.scale - 1.0f) * 100.0f;
        inlierSum += m.inlierRatio;
    }
    if (!result.affineMotion.empty()) {
        const float n = static_cast<float>(result.affineMotion.size());
        result.averageAbsRotationDeg = static_cast<float>(rotSum / n);
        result.averageScaleDriftPercent = static_cast<float>(scaleSum / n);
        result.averageInlierRatio = static_cast<float>(inlierSum / n) * 100.0f;
    }

    int fixedCropX = 0;
    int fixedCropY = 0;
    if (config.fixedCanvasCrop && !result.affinePath.empty()) {
        float maxAbsX = 0.0f;
        float maxAbsY = 0.0f;
        float maxAbsRot = 0.0f;
        float maxScaleDelta = 0.0f;
        for (const AffinePathPoint& p : result.affinePath) {
            maxAbsX = std::max(maxAbsX, std::abs(p.correctionX));
            maxAbsY = std::max(maxAbsY, std::abs(p.correctionY));
            maxAbsRot = std::max(maxAbsRot, std::abs(p.correctionRotationDeg) * PI_F / 180.0f);
            maxScaleDelta = std::max(maxScaleDelta, std::abs(p.correctionScale - 1.0f));
        }
        const float radius = 0.5f * std::sqrt(static_cast<float>(frames.front().width * frames.front().width + frames.front().height * frames.front().height));
        const int affineMargin = static_cast<int>(std::ceil(std::sin(maxAbsRot) * radius + maxScaleDelta * radius));
        fixedCropX = static_cast<int>(std::ceil(maxAbsX)) + affineMargin + std::max(0, config.borderPadding);
        fixedCropY = static_cast<int>(std::ceil(maxAbsY)) + affineMargin + std::max(0, config.borderPadding);
        const int maxAllowedX = static_cast<int>(std::floor(frames.front().width * std::max(0.0f, config.maxCropRatio)));
        const int maxAllowedY = static_cast<int>(std::floor(frames.front().height * std::max(0.0f, config.maxCropRatio)));
        fixedCropX = std::min(fixedCropX, std::max(0, maxAllowedX));
        fixedCropY = std::min(fixedCropY, std::max(0, maxAllowedY));
        makeUniformRelativeCrop(frames.front().width, frames.front().height, fixedCropX, fixedCropY);
    }
    result.fixedCropX = fixedCropX;
    result.fixedCropY = fixedCropY;
    if (fixedCropX > 0 || fixedCropY > 0) {
        const float zoomX = static_cast<float>(frames.front().width) / static_cast<float>(std::max(1, frames.front().width - 2 * fixedCropX));
        const float zoomY = static_cast<float>(frames.front().height) / static_cast<float>(std::max(1, frames.front().height - 2 * fixedCropY));
        result.autoZoomPercent = (std::max(zoomX, zoomY) - 1.0f) * 100.0f;
    }

    const int middleIndex = frameCount / 2;
    const int lastIndex = frameCount - 1;

    const std::string firstCompareName = "affine_compare_first.bmp";
    const std::string middleCompareName = "affine_compare_middle.bmp";
    const std::string lastCompareName = "affine_compare_last.bmp";

    for (int i = 0; i < frameCount; ++i) {
        const AffinePathPoint& p = result.affinePath[static_cast<std::size_t>(i)];
        Image stable = warpAffineAroundCenter(frames[static_cast<std::size_t>(i)],
                                              p.correctionX,
                                              p.correctionY,
                                              p.correctionRotationDeg,
                                              p.correctionScale);
        stable = cropResizeToOriginal(stable, fixedCropX, fixedCropY);
        const std::string stableName = "affine_stable_" + formatIndex(i + 1) + ".bmp";
        BmpWriter::writeColor(PathUtils::join(stableOutputDir, stableName), stable);
        result.stableFrameNames.push_back(stableName);

        if (i == 0) {
            BmpWriter::writeColor(PathUtils::join(compareOutputDir, firstCompareName),
                                  makeSideBySide(frames[static_cast<std::size_t>(i)], stable));
        }
        if (i == middleIndex) {
            BmpWriter::writeColor(PathUtils::join(compareOutputDir, middleCompareName),
                                  makeSideBySide(frames[static_cast<std::size_t>(i)], stable));
        }
        if (i == lastIndex) {
            BmpWriter::writeColor(PathUtils::join(compareOutputDir, lastCompareName),
                                  makeSideBySide(frames[static_cast<std::size_t>(i)], stable));
        }
    }

    result.firstStableImage = result.stableFrameNames.front();
    result.middleStableImage = result.stableFrameNames[static_cast<std::size_t>(middleIndex)];
    result.lastStableImage = result.stableFrameNames[static_cast<std::size_t>(lastIndex)];
    result.firstCompareImage = firstCompareName;
    result.middleCompareImage = middleCompareName;
    result.lastCompareImage = lastCompareName;
    return result;
}

void AffineStabilizer::writeAffineMotionCsv(const std::string& path, const AffineStabilizationResult& result) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot create affine motion CSV: " + path);
    }
    out << "pair_index,a,b,c,d,tx,ty,translation_x,translation_y,rotation_deg,scale,reprojection_error,inlier_ratio,point_count,inlier_count\n";
    out << std::fixed << std::setprecision(6);
    for (const AffineMotionSample& m : result.affineMotion) {
        out << m.pairIndex << ',' << m.transform.a << ',' << m.transform.b << ',' << m.transform.c << ',' << m.transform.d << ','
            << m.transform.tx << ',' << m.transform.ty << ',' << m.translationX << ',' << m.translationY << ','
            << m.rotationDeg << ',' << m.scale << ',' << m.reprojectionError << ',' << m.inlierRatio << ','
            << m.pointCount << ',' << m.inlierCount << '\n';
    }
}

void AffineStabilizer::writeAffinePathCsv(const std::string& path, const AffineStabilizationResult& result) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot create affine path CSV: " + path);
    }
    out << "frame_index,raw_x,raw_y,raw_rotation_deg,raw_log_scale,smooth_x,smooth_y,smooth_rotation_deg,smooth_log_scale,correction_x,correction_y,correction_rotation_deg,correction_scale\n";
    out << std::fixed << std::setprecision(6);
    for (const AffinePathPoint& p : result.affinePath) {
        out << p.frameIndex << ',' << p.rawX << ',' << p.rawY << ',' << p.rawRotationDeg << ',' << p.rawLogScale << ','
            << p.smoothX << ',' << p.smoothY << ',' << p.smoothRotationDeg << ',' << p.smoothLogScale << ','
            << p.correctionX << ',' << p.correctionY << ',' << p.correctionRotationDeg << ',' << p.correctionScale << '\n';
    }
}

void AffineStabilizer::writeRotationCurveSvg(const std::string& path, const AffineStabilizationResult& result,
                                             int width, int height) {
    std::vector<float> values;
    values.reserve(result.affineMotion.size());
    for (const AffineMotionSample& m : result.affineMotion) {
        values.push_back(m.rotationDeg);
    }
    writeCurveSvgCommon(path, "Affine Rotation per Frame Pair (degrees)", values, "#7c3aed", width, height, true);
}

void AffineStabilizer::writeScaleCurveSvg(const std::string& path, const AffineStabilizationResult& result,
                                          int width, int height) {
    std::vector<float> values;
    values.reserve(result.affineMotion.size());
    for (const AffineMotionSample& m : result.affineMotion) {
        values.push_back((m.scale - 1.0f) * 100.0f);
    }
    writeCurveSvgCommon(path, "Affine Scale Drift per Frame Pair (%)", values, "#059669", width, height, true);
}

void AffineStabilizer::writeInlierCurveSvg(const std::string& path, const AffineStabilizationResult& result,
                                           int width, int height) {
    std::vector<float> values;
    values.reserve(result.affineMotion.size());
    for (const AffineMotionSample& m : result.affineMotion) {
        values.push_back(m.inlierRatio * 100.0f);
    }
    writeCurveSvgCommon(path, "RANSAC Inlier Ratio (%)", values, "#ea580c", width, height, false);
}

} // namespace vf
