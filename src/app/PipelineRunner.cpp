#include "app/PipelineRunner.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/AppTypes.h"
#include "app/AutoModeSelector.h"
#include "app/TuiController.h"
#include "app/VideoIOAdapter.h"
#include "affine/AffineStabilizer.h"
#include "core/GrayImage.h"
#include "core/Image.h"
#include "core/Timer.h"
#include "cv/GrayConvert.h"
#include "cv/Sobel.h"
#include "io/BmpReader.h"
#include "io/BmpWriter.h"
#include "io/FrameSequence.h"
#include "io/PathUtils.h"
#include "io/Vfvid.h"
#include "motion/MotionAnalyzer.h"
#include "report/HtmlReport.h"

namespace vfapp {

vf::AffineStabilizerConfig makeBaseAffineConfig() {
    vf::AffineStabilizerConfig affineConfig;
    affineConfig.blockSize = 32;
    affineConfig.blockStep = 32;
    affineConfig.searchRadius = 12;
    affineConfig.sampleStride = 4;
    affineConfig.smoothRadius = 5;
    affineConfig.textureThreshold = 6.0f;
    affineConfig.ransacIterations = 96;
    affineConfig.ransacThreshold = 3.25f;
    return affineConfig;
}

void applyModeConfig(Mode mode, vf::AffineStabilizerConfig& affineConfig) {
    if (mode == Mode::Statics) {
        std::cout << "[VisionFlow] Using static mode: lock to first viewpoint.\n";
        affineConfig.borderPadding = 10;
        affineConfig.maxCropRatio = 0.24f;
        affineConfig.correctionStrength = 0.96f;
        affineConfig.lockToOriginStrength = 0.88f;
        affineConfig.virtualGimbalEnabled = false;
        affineConfig.smoothCorrections = true;
        affineConfig.correctionSmoothRadius = 1;
        affineConfig.cropSafeCorrection = true;
    } else if (mode == Mode::Motion) {
        std::cout << "[VisionFlow] Using motion mode: virtual gimbal for moving camera.\n";
        affineConfig.borderPadding = 10;
        affineConfig.maxCropRatio = 0.26f;
        affineConfig.correctionStrength = 0.58f;
        affineConfig.lockToOriginStrength = 0.0f;
        affineConfig.virtualGimbalEnabled = true;
        affineConfig.gimbalPositionStiffness = 0.145f;
        affineConfig.gimbalPositionDamping = 0.900f;
        affineConfig.gimbalRotationStiffness = 0.100f;
        affineConfig.gimbalRotationDamping = 0.880f;
        affineConfig.gimbalScaleStiffness = 0.070f;
        affineConfig.gimbalScaleDamping = 0.900f;
        affineConfig.maxGimbalPositionSpeed = 12.0f;
        affineConfig.maxGimbalPositionAccel = 4.0f;
        affineConfig.maxGimbalRotationSpeed = 1.5f;
        affineConfig.maxGimbalRotationAccel = 0.55f;
        affineConfig.maxGimbalScaleSpeed = 0.045f;
        affineConfig.maxGimbalScaleAccel = 0.018f;
        affineConfig.smoothCorrections = true;
        affineConfig.correctionSmoothRadius = 0;
        affineConfig.cropSafeCorrection = true;
    }
}

vf::ReportMetrics toReportMetrics(const vf::StabilizationMetrics& metrics) {
    vf::ReportMetrics out;
    out.averageTranslation = metrics.averageTranslation;
    out.p90Translation = metrics.p90Translation;
    out.jitterRms = metrics.jitterRms;
    out.trajectorySmoothness = metrics.trajectorySmoothness;
    out.sampleCount = metrics.sampleCount;
    return out;
}

std::vector<vf::GrayImage> loadStableGrayFrames(const std::string& stableDir,
                                                const std::vector<std::string>& stableFrameNames) {
    std::vector<vf::GrayImage> grayFrames;
    grayFrames.reserve(stableFrameNames.size());
    for (const std::string& name : stableFrameNames) {
        const vf::Image img = vf::BmpReader::read(vf::PathUtils::join(stableDir, name));
        grayFrames.push_back(vf::GrayConvert::toGray(img));
    }
    return grayFrames;
}

void resetOutputDirectory(const std::string& inputFolder, const std::string& outputDir) {
    namespace fs = std::filesystem;
    if (outputDir.empty() || outputDir == "." || outputDir == "..") {
        throw std::runtime_error("Unsafe output directory: " + outputDir);
    }
    const fs::path inputPath = fs::weakly_canonical(fs::path(inputFolder));
    const fs::path outputPath = fs::weakly_canonical(fs::path(outputDir));
    if (inputPath == outputPath) {
        throw std::runtime_error("Output directory must not be the same as input folder.");
    }
    if (fs::exists(outputDir)) {
        fs::remove_all(outputDir);
    }
}

int runModePipeline(Mode requestedMode, const std::string& inputFolder, const std::string& outputDir) {
    vf::Timer timer;
    ConsoleDashboardState dashboard;
    dashboard.requestedMode = requestedMode;
    dashboard.activeMode = requestedMode;
    dashboard.activeModeKnown = requestedMode != Mode::Auto;
    const std::string dashboardInputPath = liveDashboardInputPath();
    dashboard.inputPath = dashboardInputPath.empty() ? inputFolder : dashboardInputPath;
    dashboard.stage = "Preparing output";
    renderConsoleDashboard(dashboard);

    std::cout << "[VisionFlow] Requested mode: " << modeLabel(requestedMode) << "\n";
    std::cout << "[VisionFlow] Reset output directory: " << outputDir << "\n";
    resetOutputDirectory(inputFolder, outputDir);

    const std::string previewDir = vf::PathUtils::join(outputDir, "preview");
    const std::string grayDir = vf::PathUtils::join(outputDir, "gray_frames");
    const std::string edgeDir = vf::PathUtils::join(outputDir, "edge_frames");
    const std::string motionDir = vf::PathUtils::join(outputDir, "motion");
    const std::string diffDir = vf::PathUtils::join(outputDir, "motion_diff_frames");
    const std::string affineDir = vf::PathUtils::join(outputDir, "affine_stabilization");

    vf::PathUtils::ensureDirectory(outputDir);
    vf::PathUtils::ensureDirectory(previewDir);
    vf::PathUtils::ensureDirectory(grayDir);
    vf::PathUtils::ensureDirectory(edgeDir);
    vf::PathUtils::ensureDirectory(motionDir);
    vf::PathUtils::ensureDirectory(diffDir);
    vf::PathUtils::ensureDirectory(affineDir);

    std::cout << "[1/5] Scanning BMP frame folder: " << inputFolder << "\n";
    std::vector<std::string> framePaths = vf::FrameSequence::listBmpFiles(inputFolder);
    if (framePaths.empty()) {
        throw std::runtime_error("No BMP frames found in folder: " + inputFolder);
    }

    std::cout << "[OK] Found " << framePaths.size() << " BMP frames.\n";
    dashboard.stage = "Loading BMP frames";
    dashboard.totalFrames = static_cast<int>(framePaths.size());
    renderConsoleDashboard(dashboard);

    std::vector<vf::Image> frames;
    frames.reserve(framePaths.size());

    int expectedWidth = 0;
    int expectedHeight = 0;

    for (std::size_t i = 0; i < framePaths.size(); ++i) {
        vf::Image frame = vf::BmpReader::read(framePaths[i]);
        if (i == 0) {
            expectedWidth = frame.width;
            expectedHeight = frame.height;
        } else if (frame.width != expectedWidth || frame.height != expectedHeight) {
            throw std::runtime_error("Frame size mismatch: " + framePaths[i]);
        }
        frames.push_back(std::move(frame));
        dashboard.currentFrame = static_cast<int>(i) + 1;
        if (shouldRenderDashboardFrame(dashboard.currentFrame, dashboard.totalFrames)) {
            renderConsoleDashboard(dashboard);
        }
    }

    const int frameCount = static_cast<int>(frames.size());
    const int middleIndex = frameCount / 2;
    const int lastIndex = frameCount - 1;

    std::cout << "[OK] Sequence size: " << expectedWidth << " x " << expectedHeight << "\n";
    std::cout << "[2/5] Writing preview frames...\n";
    vf::BmpWriter::writeColor(vf::PathUtils::join(previewDir, "first.bmp"), frames[0]);
    vf::BmpWriter::writeColor(vf::PathUtils::join(previewDir, "middle.bmp"), frames[static_cast<std::size_t>(middleIndex)]);
    vf::BmpWriter::writeColor(vf::PathUtils::join(previewDir, "last.bmp"), frames[static_cast<std::size_t>(lastIndex)]);

    std::cout << "[3/5] Preprocessing gray + sobel...\n";
    dashboard.stage = "Preprocessing gray + sobel";
    dashboard.currentFrame = 0;
    renderConsoleDashboard(dashboard);
    std::vector<vf::GrayImage> grayFrames;
    grayFrames.reserve(frames.size());
    std::string middleGrayName;
    std::string middleEdgeName;

    for (int i = 0; i < frameCount; ++i) {
        const std::string index = formatIndex(i + 1);
        const std::string grayName = "gray_" + index + ".bmp";
        const std::string edgeName = "edge_" + index + ".bmp";
        if (i == middleIndex) {
            middleGrayName = grayName;
            middleEdgeName = edgeName;
        }

        vf::GrayImage gray = vf::GrayConvert::toGray(frames[static_cast<std::size_t>(i)]);
        vf::GrayImage edge = vf::Sobel::detect(gray);

        vf::BmpWriter::writeGray(vf::PathUtils::join(grayDir, grayName), gray);
        vf::BmpWriter::writeGray(vf::PathUtils::join(edgeDir, edgeName), edge);
        grayFrames.push_back(std::move(gray));

        if ((i + 1) % 10 == 0 || i + 1 == frameCount) {
            dashboard.currentFrame = i + 1;
            renderConsoleDashboard(dashboard);
            if (!isLiveDashboardEnabled()) {
                std::cout << "  preprocessed " << (i + 1) << " / " << frameCount << " frames [gray + sobel]\n";
            }
        }
    }

    Mode activeMode = requestedMode;
    AutoModeDecision autoDecision;
    const bool autoRequested = requestedMode == Mode::Auto;
    if (autoRequested) {
        std::cout << "[Auto] Estimating camera motion for mode selection...\n";
        dashboard.stage = "Auto mode decision";
        renderConsoleDashboard(dashboard);
        autoDecision = decideAutoMode(grayFrames);
        activeMode = autoDecision.selectedMode;
        dashboard.activeMode = activeMode;
        dashboard.activeModeKnown = true;
        dashboard.avgMotionPx = autoDecision.averageTranslation;
        dashboard.ransacInlierPercent = autoDecision.averageInlierRatio;
        writeAutoDecisionFile(outputDir, autoDecision);
        renderConsoleDashboard(dashboard);
        std::cout << "[Auto] Selected mode: " << modeLabel(activeMode) << "\n";
        std::cout << "[Auto] Reason: " << autoDecision.reason << "\n";
        std::cout << "[Auto] avg_trans=" << std::fixed << std::setprecision(2) << autoDecision.averageTranslation
                  << " px, p90_trans=" << autoDecision.p90Translation
                  << " px, net=" << autoDecision.netTranslation
                  << " px, consistency=" << autoDecision.directionConsistency << "\n";
    }
    if (!autoRequested) {
        dashboard.activeMode = activeMode;
        dashboard.activeModeKnown = true;
        renderConsoleDashboard(dashboard);
    }

    const std::string modeFramesDir = vf::PathUtils::join(outputDir, activeMode == Mode::Statics ? "affine_stable_frames" : "cinematic_frames");
    const std::string modeCompareDir = vf::PathUtils::join(outputDir, activeMode == Mode::Statics ? "affine_compare" : "cinematic_compare");
    vf::PathUtils::ensureDirectory(modeFramesDir);
    vf::PathUtils::ensureDirectory(modeCompareDir);

    std::cout << "[VisionFlow] Active mode: " << modeLabel(activeMode) << " (" << modeName(activeMode) << ")\n";
    std::cout << "[4/5] Running motion analysis...\n";
    dashboard.stage = "Motion analysis";
    renderConsoleDashboard(dashboard);
    vf::MotionAnalyzerConfig motionConfig;
    motionConfig.diffThreshold = 20.0f;
    motionConfig.searchRadius = 8;
    motionConfig.sampleStep = 4;
    vf::MotionAnalysisResult motion = vf::MotionAnalyzer::analyze(grayFrames, diffDir, motionConfig);
    dashboard.avgMotionPx = std::sqrt(motion.averageGlobalDx * motion.averageGlobalDx +
                                      motion.averageGlobalDy * motion.averageGlobalDy);
    renderConsoleDashboard(dashboard);

    const std::string heatmapName = "motion_heatmap.bmp";
    const std::string curveName = "motion_curve.svg";
    const std::string csvName = "motion_vectors.csv";

    if (!motion.heatmap.empty()) {
        vf::BmpWriter::writeGray(vf::PathUtils::join(motionDir, heatmapName), motion.heatmap);
    }
    vf::MotionAnalyzer::writeSvgCurve(vf::PathUtils::join(motionDir, curveName), motion);
    vf::MotionAnalyzer::writeCsv(vf::PathUtils::join(motionDir, csvName), motion);

    std::cout << "[5/5] Running affine output mode...\n";
    dashboard.stage = "Affine stabilization";
    renderConsoleDashboard(dashboard);
    vf::AffineStabilizerConfig affineConfig = makeBaseAffineConfig();
    applyModeConfig(activeMode, affineConfig);

    vf::AffineStabilizationResult affine = vf::AffineStabilizer::stabilize(
        frames,
        grayFrames,
        modeFramesDir,
        modeCompareDir,
        affineConfig);
    dashboard.ransacInlierPercent = affine.averageInlierRatio;
    dashboard.currentFrame = frameCount;
    renderConsoleDashboard(dashboard);

    const std::string affineMotionCsvName = "affine_motion.csv";
    const std::string affinePathCsvName = "affine_path.csv";
    const std::string affineRotationSvgName = "rotation_curve.svg";
    const std::string affineScaleSvgName = "scale_curve.svg";
    const std::string affineInlierSvgName = "ransac_inlier_ratio.svg";

    vf::AffineStabilizer::writeAffineMotionCsv(vf::PathUtils::join(affineDir, affineMotionCsvName), affine);
    vf::AffineStabilizer::writeAffinePathCsv(vf::PathUtils::join(affineDir, affinePathCsvName), affine);
    vf::AffineStabilizer::writeRotationCurveSvg(vf::PathUtils::join(affineDir, affineRotationSvgName), affine);
    vf::AffineStabilizer::writeScaleCurveSvg(vf::PathUtils::join(affineDir, affineScaleSvgName), affine);
    vf::AffineStabilizer::writeInlierCurveSvg(vf::PathUtils::join(affineDir, affineInlierSvgName), affine);


    std::cout << "[VisionFlow] Computing stabilization metrics...\n";
    dashboard.stage = "Computing stabilization metrics";
    renderConsoleDashboard(dashboard);
    const vf::StabilizationMetrics inputMetrics = vf::AffineStabilizer::computeStabilizationMetrics(affine.affineMotion);
    const std::vector<vf::GrayImage> stableGrayFrames = loadStableGrayFrames(modeFramesDir, affine.stableFrameNames);
    const std::vector<vf::AffineMotionSample> outputMotionSamples = vf::AffineStabilizer::estimateMotionSamples(
        stableGrayFrames,
        affineConfig,
        0);
    const vf::StabilizationMetrics outputMetrics = vf::AffineStabilizer::computeStabilizationMetrics(outputMotionSamples);
    dashboard.inputJitter = inputMetrics.jitterRms;
    dashboard.outputJitter = outputMetrics.jitterRms;
    dashboard.improvementPercent = inputMetrics.jitterRms > 1e-9
        ? (inputMetrics.jitterRms - outputMetrics.jitterRms) / inputMetrics.jitterRms * 100.0
        : 0.0;
    dashboard.stage = "Generating report";
    renderConsoleDashboard(dashboard);

    const double elapsedMs = timer.elapsedMs();

    const std::string infoPath = vf::PathUtils::join(outputDir, "frames_info.txt");
    std::ofstream infoFile(infoPath);
    if (!infoFile) {
        throw std::runtime_error("Cannot create frames_info.txt: " + infoPath);
    }
    std::ofstream markerFile(vf::PathUtils::join(outputDir, activeMode == Mode::Statics ? "MODE_STATIC.txt" : "MODE_MOTION.txt"));
    markerFile << modeLabel(activeMode) << "\n";
    if (autoRequested) {
        std::ofstream autoMarker(vf::PathUtils::join(outputDir, activeMode == Mode::Statics ? "MODE_AUTO_SELECTED_STATIC.txt" : "MODE_AUTO_SELECTED_MOTION.txt"));
        autoMarker << "AUTO -> " << modeLabel(activeMode) << "\n";
    }

    infoFile << "VisionFlow Version 8ProSafe Auto Mode Info\n\n";
    infoFile << "Requested mode: " << modeName(requestedMode) << "\n";
    infoFile << "Active mode: " << modeName(activeMode) << "\n";
    if (autoRequested) {
        infoFile << "Auto decision file: AUTO_MODE_DECISION.txt\n";
        infoFile << "Auto reason: " << autoDecision.reason << "\n";
        infoFile << std::fixed << std::setprecision(4);
        infoFile << "Auto average translation: " << autoDecision.averageTranslation << " px / pair\n";
        infoFile << "Auto P90 translation: " << autoDecision.p90Translation << " px / pair\n";
        infoFile << "Auto net translation: " << autoDecision.netTranslation << " px\n";
        infoFile << "Auto direction consistency: " << autoDecision.directionConsistency << "\n";
        infoFile << "Auto average abs rotation: " << autoDecision.averageAbsRotationDeg << " deg / pair\n";
        infoFile << "Auto usable samples: " << autoDecision.usableSamples << " / " << autoDecision.totalSamples << "\n";
    }
    infoFile << "Input folder: " << inputFolder << "\n";
    infoFile << "Frame count: " << frameCount << "\n";
    infoFile << "Frame size: " << expectedWidth << " x " << expectedHeight << "\n";
    infoFile << "Processing time: " << std::fixed << std::setprecision(2) << elapsedMs << " ms\n\n";
    infoFile << "Motion Analysis\n";
    infoFile << "Motion pair count: " << motion.stats.size() << "\n";
    infoFile << "Average motion score: " << std::fixed << std::setprecision(2) << motion.averageMotionScore << " / 100\n";
    infoFile << "Peak motion score: " << std::fixed << std::setprecision(2) << motion.peakMotionScore << " / 100 at pair " << motion.peakPairIndex << "\n";
    infoFile << "Average changed pixels: " << std::fixed << std::setprecision(2) << motion.averageChangedRatio << "%\n";
    infoFile << "Average global shift: dx = " << std::fixed << std::setprecision(2) << motion.averageGlobalDx
             << ", dy = " << motion.averageGlobalDy << " pixels\n";
    infoFile << "Motion CSV: motion/" << csvName << "\n";
    infoFile << "Motion curve: motion/" << curveName << "\n";
    infoFile << "Motion heatmap: motion/" << heatmapName << "\n\n";
    infoFile << "Affine Output\n";
    infoFile << "Average absolute rotation: " << std::fixed << std::setprecision(4) << affine.averageAbsRotationDeg << " deg / frame pair\n";
    infoFile << "Peak absolute rotation: " << std::fixed << std::setprecision(4) << affine.peakAbsRotationDeg << " deg\n";
    infoFile << "Average scale drift: " << std::fixed << std::setprecision(4) << affine.averageScaleDriftPercent << "%\n";
    infoFile << "Average RANSAC inlier ratio: " << std::fixed << std::setprecision(2) << affine.averageInlierRatio << "%\n";
    infoFile << "Fixed canvas crop: " << affine.fixedCropX << " px horizontally, " << affine.fixedCropY << " px vertically\n";
    infoFile << "Auto zoom: " << std::fixed << std::setprecision(2) << affine.autoZoomPercent << "%\n";
    infoFile << "Frame folder: " << (activeMode == Mode::Statics ? "affine_stable_frames" : "cinematic_frames") << "/\n";
    infoFile << "Affine motion CSV: affine_stabilization/" << affineMotionCsvName << "\n";
    infoFile << "Affine path CSV: affine_stabilization/" << affinePathCsvName << "\n";
    infoFile << "Rotation curve: affine_stabilization/" << affineRotationSvgName << "\n";
    infoFile << "Scale curve: affine_stabilization/" << affineScaleSvgName << "\n";
    infoFile << "RANSAC inlier curve: affine_stabilization/" << affineInlierSvgName << "\n";

    infoFile << "\nStabilization Metrics\n";
    infoFile << "Input average translation: " << std::fixed << std::setprecision(4) << inputMetrics.averageTranslation << " px / frame\n";
    infoFile << "Output average translation: " << std::fixed << std::setprecision(4) << outputMetrics.averageTranslation << " px / frame\n";
    infoFile << "Input P90 translation: " << std::fixed << std::setprecision(4) << inputMetrics.p90Translation << " px / frame\n";
    infoFile << "Output P90 translation: " << std::fixed << std::setprecision(4) << outputMetrics.p90Translation << " px / frame\n";
    infoFile << "Input jitter RMS: " << std::fixed << std::setprecision(4) << inputMetrics.jitterRms << "\n";
    infoFile << "Output jitter RMS: " << std::fixed << std::setprecision(4) << outputMetrics.jitterRms << "\n";
    infoFile << "Input trajectory smoothness: " << std::fixed << std::setprecision(4) << inputMetrics.trajectorySmoothness << "\n";
    infoFile << "Output trajectory smoothness: " << std::fixed << std::setprecision(4) << outputMetrics.trajectorySmoothness << "\n";

    std::cout << "[VisionFlow] Generating HTML report for mode: " << modeLabel(activeMode) << "\n";
    vf::SequenceReportInfo reportInfo;
    reportInfo.title = std::string("VisionFlow ") + (autoRequested ? "Auto -> " : "") + modeLabel(activeMode) + " Report";
    reportInfo.modeName = autoRequested ? std::string("AUTO -> ") + modeLabel(activeMode) : modeLabel(activeMode);
    reportInfo.frameCount = frameCount;
    reportInfo.width = expectedWidth;
    reportInfo.height = expectedHeight;
    reportInfo.elapsedMs = elapsedMs;

    reportInfo.firstFrameImage = htmlRelative("preview", "first.bmp");
    reportInfo.middleFrameImage = htmlRelative("preview", "middle.bmp");
    reportInfo.lastFrameImage = htmlRelative("preview", "last.bmp");

    reportInfo.middleGrayImage = htmlRelative("gray_frames", middleGrayName);
    reportInfo.middleEdgeImage = htmlRelative("edge_frames", middleEdgeName);

    reportInfo.motionCurveImage = htmlRelative("motion", curveName);
    reportInfo.motionHeatmapImage = htmlRelative("motion", heatmapName);
    reportInfo.affineRotationSvg = htmlRelative("affine_stabilization", affineRotationSvgName);
    reportInfo.affineScaleSvg = htmlRelative("affine_stabilization", affineScaleSvgName);
    reportInfo.affineInlierSvg = htmlRelative("affine_stabilization", affineInlierSvgName);

    const std::string outputFrameFolder = activeMode == Mode::Statics ? "affine_stable_frames" : "cinematic_frames";
    const std::string compareFolder = activeMode == Mode::Statics ? "affine_compare" : "cinematic_compare";
    reportInfo.firstOutputImage = htmlRelative(outputFrameFolder, affine.firstStableImage);
    reportInfo.middleOutputImage = htmlRelative(outputFrameFolder, affine.middleStableImage);
    reportInfo.lastOutputImage = htmlRelative(outputFrameFolder, affine.lastStableImage);
    reportInfo.firstCompareImage = htmlRelative(compareFolder, affine.firstCompareImage);
    reportInfo.middleCompareImage = htmlRelative(compareFolder, affine.middleCompareImage);
    reportInfo.lastCompareImage = htmlRelative(compareFolder, affine.lastCompareImage);

    if (!motion.diffFrameNames.empty()) {
        const int diffCount = static_cast<int>(motion.diffFrameNames.size());
        const int diffMiddleIndex = diffCount / 2;
        reportInfo.middleDiffImage = htmlRelative("motion_diff_frames", motion.diffFrameNames[static_cast<std::size_t>(diffMiddleIndex)]);
    }

    reportInfo.hasStabilizationMetrics = true;
    reportInfo.inputMetrics = toReportMetrics(inputMetrics);
    reportInfo.outputMetrics = toReportMetrics(outputMetrics);

    const std::string reportPath = vf::PathUtils::join(outputDir, "report.html");
    vf::HtmlReport::generateSequence(reportPath, reportInfo);
    dashboard.stage = "Done";
    renderConsoleDashboard(dashboard);

    std::cout << "[Done] Output directory: " << outputDir << "\n";
    std::cout << "[Done] Open report: " << reportPath << "\n";
    return 0;
}

int runUserPipeline(Mode requestedMode,
                    const std::string& inputPath,
                    const std::string& outputPath,
                    const char* argv0) {
    namespace fs = std::filesystem;

    const bool inputIsVideo = isVideoInputPath(inputPath);
    const bool inputIsVfvid = isVfvidPath(inputPath);
    const bool outputIsMp4 = isMp4OutputPath(outputPath);
    const bool outputIsVfvid = isVfvidPath(outputPath);
    const fs::path pipelineOutputDir = pipelineOutputDirectoryFor(outputPath);

    std::string frameInputFolder = inputPath;
    std::string ffmpeg;

    if (inputIsVideo || outputIsMp4) {
        ffmpeg = findFfmpegExecutable(argv0);
    }

    ScopedDirectoryCleanup sourceFrameCleanup;
    if (inputIsVideo) {
        frameInputFolder = deriveFrameInputFolder(inputPath, true, ffmpeg, sourceFrameCleanup);
    } else if (inputIsVfvid) {
        const fs::path vfvidFrameDir = makeTemporaryVfvidFrameDirectory();
        sourceFrameCleanup.path = vfvidFrameDir;
        sourceFrameCleanup.enabled = true;
        std::cout << "[VisionFlow] VFVID input enabled through native C++ container reader.\n";
        vf::Vfvid::unpackToBmpFolder(inputPath, vfvidFrameDir.string());
        frameInputFolder = vfvidFrameDir.string();
    }

    const int result = runModePipeline(requestedMode, frameInputFolder, pipelineOutputDir.string());
    if (result != 0) {
        return result;
    }

    if (outputIsMp4) {
        encodeBmpFramesToMp4(ffmpeg, pipelineOutputDir.string(), outputPath);
        std::cout << "[Done] Output video: " << outputPath << "\n";
        std::cout << "[Done] Report/work directory: " << pipelineOutputDir.string() << "\n";
    }
    if (outputIsVfvid) {
        const std::string stableDir = findStabilizedFrameDirectory(pipelineOutputDir.string());
        std::cout << "[VisionFlow] Packing stabilized frames to VFVID with native C++ writer.\n";
        vf::Vfvid::packFromBmpFolder(stableDir, outputPath, 30);
        std::cout << "[Done] Output VFVID: " << outputPath << "\n";
        std::cout << "[Done] Report/work directory: " << pipelineOutputDir.string() << "\n";
    }

    return 0;
}

} // namespace vfapp
