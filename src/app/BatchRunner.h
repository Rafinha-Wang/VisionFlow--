#pragma once

#include <string>

namespace vfapp {

std::string defaultBatchReportPath(const std::string& csvPath);
std::string resolveBatchReportPath(const std::string& csvPath, const std::string& reportPathArg);
int runBatchQueue(const std::string& csvPath, const std::string& reportPathArg, const char* argv0);

} // namespace vfapp
