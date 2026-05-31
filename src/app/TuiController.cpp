#include "app/TuiController.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>

#include "app/BatchRunner.h"
#include "app/PipelineRunner.h"
#include "app/VideoIOAdapter.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace vfapp {
namespace {

bool g_liveDashboardEnabled = false;
std::string g_liveDashboardInputPath;

std::string dashboardProgressBar(int currentFrame, int totalFrames, int width) {
    if (totalFrames <= 0 || width <= 0) {
        return std::string(static_cast<std::size_t>(std::max(0, width)), '-');
    }
    const double ratio = std::max(0.0, std::min(1.0, static_cast<double>(currentFrame) / static_cast<double>(totalFrames)));
    const int filled = static_cast<int>(std::lround(ratio * static_cast<double>(width)));
    return std::string(static_cast<std::size_t>(filled), '#') +
           std::string(static_cast<std::size_t>(std::max(0, width - filled)), '.');
}

std::string dashboardModeText(const ConsoleDashboardState& state) {
    if (state.requestedMode == Mode::Auto) {
        return state.activeModeKnown ? std::string("AUTO -> ") + modeLabel(state.activeMode) : "AUTO -> ...";
    }
    return modeLabel(state.requestedMode);
}

int dashboardPercent(int currentFrame, int totalFrames) {
    if (totalFrames <= 0) {
        return 0;
    }
    const double ratio = std::max(0.0, std::min(1.0, static_cast<double>(currentFrame) / static_cast<double>(totalFrames)));
    return static_cast<int>(std::lround(ratio * 100.0));
}

void clearConsoleScreen() {
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE) {
        return;
    }
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(out, &csbi)) {
        return;
    }
    const DWORD cells = static_cast<DWORD>(csbi.dwSize.X) * static_cast<DWORD>(csbi.dwSize.Y);
    DWORD written = 0;
    const COORD home = {0, 0};
    FillConsoleOutputCharacterA(out, ' ', cells, home, &written);
    FillConsoleOutputAttribute(out, csbi.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(out, home);
#else
    std::cout << "\x1b[2J\x1b[H";
#endif
}

void printTuiHeader() {
#ifdef _WIN32
    setConsoleColor(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
#endif
    std::cout << "========================================\n";
    std::cout << "              VisionFlow\n";
    std::cout << "   Pure C++ Video Motion Stabilizer\n";
    std::cout << "========================================\n";
    resetConsoleColor();
}

std::string promptLine(const std::string& label) {
    std::cout << label;
    std::string value;
    std::getline(std::cin, value);
    return value;
}

int promptInt(const std::string& label, int minValue, int maxValue) {
    while (true) {
        const std::string value = trimString(promptLine(label));
        std::istringstream iss(value);
        int choice = 0;
        if (iss >> choice && choice >= minValue && choice <= maxValue) {
            return choice;
        }
        std::cout << "Please enter a number from " << minValue << " to " << maxValue << ".\n";
    }
}

void pauseTui() {
    (void)promptLine("\nPress Enter to continue...");
}

void showProjectInfo() {
    printTuiHeader();
    std::cout << "\nProject Info\n";
    std::cout << "1. Core algorithm: pure C++ BMP reading, motion analysis, affine stabilization.\n";
    std::cout << "2. MP4 support: external ffmpeg.exe for format conversion only.\n";
    std::cout << "3. VFVID: native RGB24 video container implemented with C++ file I/O.\n";
    std::cout << "4. Batch Queue: CSV-driven multi-task processing with HTML/CSV summary.\n";
    pauseTui();
}

void showTuiError(const std::exception& e) {
    resetConsoleColor();
    std::cout << "\n[Error] " << e.what() << "\n";
    std::cout << "Please check the task CSV path, row format, input path, and output path, then try again.\n";
    pauseTui();
}

int runTuiSingleTask(const char* argv0, std::string& lastReportPath) {
    printTuiHeader();
    std::cout << "\nSingle Task\n\n";
    std::cout << "Input type:\n";
    std::cout << "[1] MP4 video\n";
    std::cout << "[2] BMP frame folder\n";
    std::cout << "[3] VFVID native container\n\n";
    (void)promptInt("Input type: ", 1, 3);

    std::cout << "\nMode:\n";
    std::cout << "[1] Auto\n";
    std::cout << "[2] Static camera\n";
    std::cout << "[3] Moving camera\n\n";
    const int modeChoice = promptInt("Mode: ", 1, 3);
    Mode mode = Mode::Auto;
    if (modeChoice == 2) {
        mode = Mode::Statics;
    } else if (modeChoice == 3) {
        mode = Mode::Motion;
    }

    const std::string inputPath = trimPromptPath(promptLine("\nInput path : "));
    const std::string outputPath = trimPromptPath(promptLine("Output path: "));
    if (inputPath.empty() || outputPath.empty()) {
        throw std::runtime_error("Input path and output path are required.");
    }

    std::cout << "\nReady:\n";
    std::cout << "  Mode  : " << modeName(mode) << "\n";
    std::cout << "  Input : " << inputPath << "\n";
    std::cout << "  Output: " << outputPath << "\n";
    const std::string confirm = lowerString(promptLine("Start processing? [Y/N]: "));
    if (!(confirm == "y" || confirm == "yes")) {
        std::cout << "[VisionFlow] Canceled.\n";
        pauseTui();
        return 0;
    }

    int result = 0;
    {
        LiveDashboardScope dashboard(true, inputPath);
        result = runUserPipeline(mode, inputPath, outputPath, argv0);
    }
    lastReportPath = reportPathForOutput(outputPath);
    if (std::filesystem::exists(lastReportPath)) {
        const std::string open = lowerString(promptLine("Open HTML report now? [Y/N]: "));
        if (open == "y" || open == "yes") {
            openPathInSystemViewer(lastReportPath);
        }
    }
    pauseTui();
    return result;
}

int runTuiBatchQueue(const char* argv0, std::string& lastReportPath) {
    printTuiHeader();
    std::cout << "\nBatch Queue\n\n";
    std::cout << "Batch Queue reads a task-list CSV, not one video path.\n";
    std::cout << "Each row contains the real input path and output path for one task.\n\n";
    std::cout << "Task CSV format:\n";
    std::cout << "mode,input,output\n";
    std::cout << "auto,case(test)\\static\\pen.mp4,out\\pen.mp4\n";
    std::cout << "static,case2.vfvid,out\\case2.vfvid\n\n";

    const std::string csvPath = trimPromptPath(promptLine("Task CSV path: "));
    if (csvPath.empty()) {
        throw std::runtime_error("Task CSV path is required.");
    }
    const std::string reportPath = trimPromptPath(promptLine("Batch report file/folder [Enter = default]: "));

    const std::string confirm = lowerString(promptLine("Load task CSV and run all rows? [Y/N]: "));
    if (!(confirm == "y" || confirm == "yes")) {
        std::cout << "[Batch] Canceled.\n";
        pauseTui();
        return 0;
    }

    int result = 0;
    {
        LiveDashboardScope dashboard(true, csvPath);
        result = runBatchQueue(csvPath, reportPath, argv0);
    }
    lastReportPath = resolveBatchReportPath(csvPath, reportPath);
    const std::string open = lowerString(promptLine("Open batch report now? [Y/N]: "));
    if (open == "y" || open == "yes") {
        openPathInSystemViewer(lastReportPath);
    }
    pauseTui();
    return result;
}

int runTuiVfvidTools() {
    while (true) {
        printTuiHeader();
        std::cout << "\nVFVID Native Container Tools\n\n";
        std::cout << "[1] Pack BMP frames to .vfvid\n";
        std::cout << "[2] Unpack .vfvid to BMP frames\n";
        std::cout << "[3] Show .vfvid file info\n";
        std::cout << "[0] Back\n\n";
        const int choice = promptInt("Select: ", 0, 3);
        if (choice == 0) {
            return 0;
        }

        if (choice == 1) {
            const std::string inputFolder = trimPromptPath(promptLine("Input frame folder: "));
            const std::string outputFile = trimPromptPath(promptLine("Output VFVID file : "));
            const int fps = promptInt("FPS [1-240]: ", 1, 240);
            runPackCommand(inputFolder, outputFile, static_cast<std::uint32_t>(fps));
            pauseTui();
        } else if (choice == 2) {
            const std::string inputFile = trimPromptPath(promptLine("Input VFVID file   : "));
            const std::string outputFolder = trimPromptPath(promptLine("Output frame folder: "));
            runUnpackCommand(inputFile, outputFolder);
            pauseTui();
        } else {
            const std::string inputFile = trimPromptPath(promptLine("Input VFVID file: "));
            runVfvidInfoCommand(inputFile);
            pauseTui();
        }
    }
}

} // namespace

LiveDashboardScope::LiveDashboardScope(bool enabled, const std::string& inputPath)
    : previousEnabled(g_liveDashboardEnabled),
      previousInputPath(g_liveDashboardInputPath) {
    g_liveDashboardEnabled = enabled;
    g_liveDashboardInputPath = inputPath;
}

LiveDashboardScope::~LiveDashboardScope() {
    g_liveDashboardEnabled = previousEnabled;
    g_liveDashboardInputPath = previousInputPath;
}

void setConsoleColor(unsigned short color) {
#ifdef _WIN32
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
#else
    (void)color;
#endif
}

void resetConsoleColor() {
#ifdef _WIN32
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE),
                            FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#endif
}

bool isLiveDashboardEnabled() {
    return g_liveDashboardEnabled;
}

std::string liveDashboardInputPath() {
    return g_liveDashboardInputPath;
}

void renderConsoleDashboard(const ConsoleDashboardState& state) {
    if (!g_liveDashboardEnabled) {
        return;
    }

    clearConsoleScreen();
    constexpr std::size_t contentWidth = 72;
    const std::string title = " VisionFlow Console ";
    const std::size_t railWidth = contentWidth + 2;
    const std::size_t leftRail = (railWidth > title.size()) ? (railWidth - title.size()) / 2 : 0;
    const std::size_t rightRail = (railWidth > title.size()) ? (railWidth - title.size() - leftRail) : 0;

#ifdef _WIN32
    setConsoleColor(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
#endif
    std::cout << '+' << std::string(leftRail, '-') << title << std::string(rightRail, '-') << "+\n";
    resetConsoleColor();

    auto printLine = [](const std::string& text) {
        std::cout << "| " << padRight(text, contentWidth) << " |\n";
    };

    const int percent = dashboardPercent(state.currentFrame, state.totalFrames);
    std::ostringstream framesLine;
    framesLine << "Frames: " << state.currentFrame << " / " << state.totalFrames
               << "        Progress: [" << dashboardProgressBar(state.currentFrame, state.totalFrames, 12)
               << "] " << percent << "%";

    std::ostringstream motionLine;
    motionLine << "Avg motion: " << formatFixed(state.avgMotionPx, 2) << " px"
               << "      RANSAC inlier: " << formatFixed(state.ransacInlierPercent, 1) << "%";

    std::ostringstream jitterLine;
    jitterLine << "Jitter RMS: " << formatFixed(state.inputJitter, 2)
               << " -> " << formatFixed(state.outputJitter, 2)
               << "  Improvement: " << formatFixed(state.improvementPercent, 1) << "%";

    printLine("Mode: " + dashboardModeText(state));
    printLine("Input: " + shortenMiddle(state.inputPath, 65));
    printLine("Stage: " + state.stage);
    printLine(framesLine.str());
    printLine(motionLine.str());
    printLine(jitterLine.str());

#ifdef _WIN32
    setConsoleColor(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
#endif
    std::cout << '+' << std::string(railWidth, '-') << "+\n\n";
    resetConsoleColor();
}

bool shouldRenderDashboardFrame(int currentFrame, int totalFrames) {
    return g_liveDashboardEnabled && (currentFrame == 1 || currentFrame == totalFrames || currentFrame % 10 == 0);
}

int runTui(const char* argv0) {
#ifdef _WIN32
    SetConsoleTitleA("VisionFlow - Pure C++ Video Stabilizer");
#endif
    std::string lastReportPath;
    while (true) {
        printTuiHeader();
        std::cout << "\n[1] Single Task\n";
        std::cout << "    Process one MP4, BMP frame folder, or VFVID file\n\n";
        std::cout << "[2] Batch Queue\n";
        std::cout << "    Process multiple tasks from a CSV file\n\n";
        std::cout << "[3] VFVID Tools\n";
        std::cout << "    Pack / unpack native pure C++ video container\n\n";
        std::cout << "[4] View Last Report\n";
        std::cout << "[5] Project Info\n";
        std::cout << "[0] Exit\n\n";

        const int choice = promptInt("Select: ", 0, 5);
        if (choice == 0) {
            return 0;
        }
        try {
            if (choice == 1) {
                (void)runTuiSingleTask(argv0, lastReportPath);
            } else if (choice == 2) {
                (void)runTuiBatchQueue(argv0, lastReportPath);
            } else if (choice == 3) {
                (void)runTuiVfvidTools();
            } else if (choice == 4) {
                if (!lastReportPath.empty() && std::filesystem::exists(lastReportPath)) {
                    openPathInSystemViewer(lastReportPath);
                } else {
                    std::cout << "No report has been generated in this TUI session.\n";
                    pauseTui();
                }
            } else {
                showProjectInfo();
            }
        } catch (const std::exception& e) {
            showTuiError(e);
        }
    }
}

} // namespace vfapp
