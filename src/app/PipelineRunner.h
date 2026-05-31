#pragma once

#include <string>

#include "app/AppTypes.h"

namespace vfapp {

int runModePipeline(Mode requestedMode, const std::string& inputFolder, const std::string& outputDir);
int runUserPipeline(Mode requestedMode,
                    const std::string& inputPath,
                    const std::string& outputPath,
                    const char* argv0);

} // namespace vfapp
