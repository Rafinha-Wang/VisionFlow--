#pragma once

#include <string>
#include <vector>

#include "app/AppTypes.h"
#include "core/GrayImage.h"

namespace vfapp {

float percentile(std::vector<float> values, float q);
AutoModeDecision decideAutoMode(const std::vector<vf::GrayImage>& grayFrames);
void writeAutoDecisionFile(const std::string& outputDir, const AutoModeDecision& decision);

} // namespace vfapp
