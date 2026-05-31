#pragma once

#include <string>

#include "app/AppTypes.h"

namespace vfapp {

struct ConsoleDashboardState {
    Mode requestedMode = Mode::Auto;
    Mode activeMode = Mode::Auto;
    bool activeModeKnown = false;
    std::string inputPath;
    std::string stage = "Starting";
    int currentFrame = 0;
    int totalFrames = 0;
    double avgMotionPx = 0.0;
    double ransacInlierPercent = 0.0;
    double inputJitter = 0.0;
    double outputJitter = 0.0;
    double improvementPercent = 0.0;
};

struct LiveDashboardScope {
    bool previousEnabled = false;
    std::string previousInputPath;

    LiveDashboardScope(bool enabled, const std::string& inputPath);
    ~LiveDashboardScope();
};

void setConsoleColor(unsigned short color);
void resetConsoleColor();
bool isLiveDashboardEnabled();
std::string liveDashboardInputPath();
void renderConsoleDashboard(const ConsoleDashboardState& state);
bool shouldRenderDashboardFrame(int currentFrame, int totalFrames);
int runTui(const char* argv0);

} // namespace vfapp
