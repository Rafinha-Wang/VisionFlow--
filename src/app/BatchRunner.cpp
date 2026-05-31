#include "app/BatchRunner.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "app/AppTypes.h"
#include "app/PipelineRunner.h"
#include "app/TuiController.h"

namespace vfapp {
namespace {

struct BatchTask {
    int id = 0;
    Mode mode = Mode::Auto;
    std::string modeText;
    std::string inputPath;
    std::string outputPath;
    std::string status = "WAIT";
    std::string message;
    ProcessingSummary summary;
};

std::vector<std::string> splitCsvSimple(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    std::istringstream ss(line);
    while (std::getline(ss, current, ',')) {
        fields.push_back(trimPromptPath(trimString(current)));
    }
    return fields;
}

std::vector<BatchTask> loadBatchTasks(const std::string& csvPath) {
    std::ifstream in(csvPath);
    if (!in) {
        throw std::runtime_error("Cannot open batch task file: " + csvPath);
    }

    std::vector<BatchTask> tasks;
    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        line = trimString(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const std::vector<std::string> fields = splitCsvSimple(line);
        if (fields.size() < 3) {
            throw std::runtime_error("Invalid batch CSV line " + std::to_string(lineNumber) + ": " + line);
        }
        if (lowerString(fields[0]) == "mode") {
            continue;
        }

        BatchTask task;
        task.id = static_cast<int>(tasks.size()) + 1;
        task.modeText = lowerString(fields[0]);
        task.mode = parseModeOrThrow(task.modeText);
        task.inputPath = fields[1];
        task.outputPath = fields[2];
        tasks.push_back(std::move(task));
    }

    if (tasks.empty()) {
        throw std::runtime_error("Batch file contains no tasks: " + csvPath);
    }
    return tasks;
}

void writeBatchSummaryCsv(const std::string& reportPath, const std::vector<BatchTask>& tasks) {
    std::filesystem::path csvPath(reportPath);
    csvPath.replace_extension(".csv");
    std::ofstream out(csvPath);
    if (!out) {
        throw std::runtime_error("Cannot create batch summary CSV: " + csvPath.string());
    }

    out << "id,mode,input,output,status,frame_count,input_jitter,output_jitter,improvement_percent,message\n";
    out << std::fixed << std::setprecision(4);
    for (const BatchTask& task : tasks) {
        out << task.id << ','
            << task.modeText << ','
            << task.inputPath << ','
            << task.outputPath << ','
            << task.status << ','
            << task.summary.frameCount << ','
            << task.summary.inputJitter << ','
            << task.summary.outputJitter << ','
            << task.summary.improvementPercent << ','
            << task.message << '\n';
    }
}

void writeBatchReportHtml(const std::string& reportPath, const std::vector<BatchTask>& tasks) {
    std::filesystem::path outPath(reportPath);
    if (!outPath.parent_path().empty()) {
        std::filesystem::create_directories(outPath.parent_path());
    }

    writeBatchSummaryCsv(reportPath, tasks);

    std::ofstream out(reportPath);
    if (!out) {
        throw std::runtime_error("Cannot create batch HTML report: " + reportPath);
    }

    int okCount = 0;
    for (const BatchTask& task : tasks) {
        if (task.status == "OK") {
            ++okCount;
        }
    }

    out << "<!doctype html><html><head><meta charset=\"utf-8\">";
    out << "<title>VisionFlow Batch Report</title>";
    out << "<style>";
    out << "body{font-family:Segoe UI,Arial,sans-serif;margin:32px;background:#f8fafc;color:#111827}";
    out << "h1{margin-bottom:4px}.meta{color:#64748b;margin-bottom:24px}";
    out << "table{border-collapse:collapse;width:100%;background:white;border:1px solid #e5e7eb}";
    out << "th,td{padding:10px 12px;border-bottom:1px solid #e5e7eb;text-align:left;font-size:14px}";
    out << "th{background:#eef2ff}.ok{color:#047857;font-weight:700}.failed{color:#b91c1c;font-weight:700}";
    out << "a{color:#2563eb;text-decoration:none}";
    out << "</style></head><body>";
    out << "<h1>VisionFlow Batch Report</h1>";
    out << "<div class=\"meta\">Tasks: " << tasks.size() << ", success: " << okCount
        << ", failed: " << (tasks.size() - static_cast<std::size_t>(okCount)) << "</div>";
    out << "<table><thead><tr>";
    out << "<th>ID</th><th>Mode</th><th>Input</th><th>Output</th><th>Frames</th>";
    out << "<th>Input RMS</th><th>Output RMS</th><th>Improvement</th><th>Status</th><th>Report</th><th>Message</th>";
    out << "</tr></thead><tbody>";

    out << std::fixed << std::setprecision(2);
    for (const BatchTask& task : tasks) {
        out << "<tr>";
        out << "<td>" << task.id << "</td>";
        out << "<td>" << htmlEscape(task.modeText) << "</td>";
        out << "<td>" << htmlEscape(task.inputPath) << "</td>";
        out << "<td>" << htmlEscape(task.outputPath) << "</td>";
        out << "<td>" << task.summary.frameCount << "</td>";
        out << "<td>" << task.summary.inputJitter << "</td>";
        out << "<td>" << task.summary.outputJitter << "</td>";
        out << "<td>" << task.summary.improvementPercent << "%</td>";
        out << "<td class=\"" << (task.status == "OK" ? "ok" : "failed") << "\">" << task.status << "</td>";
        if (task.status == "OK" && std::filesystem::exists(task.summary.reportPath)) {
            out << "<td><a href=\"" << htmlEscape(task.summary.reportPath) << "\">open</a></td>";
        } else {
            out << "<td>-</td>";
        }
        out << "<td>" << htmlEscape(task.message) << "</td>";
        out << "</tr>";
    }

    out << "</tbody></table></body></html>";
}

} // namespace

std::string defaultBatchReportPath(const std::string& csvPath) {
    std::filesystem::path path(csvPath);
    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        parent = std::filesystem::current_path();
    }
    return (parent / "VisionFlow_Batch_Report.html").string();
}

std::string resolveBatchReportPath(const std::string& csvPath, const std::string& reportPathArg) {
    namespace fs = std::filesystem;
    if (reportPathArg.empty()) {
        return defaultBatchReportPath(csvPath);
    }

    fs::path path(reportPathArg);
    const std::string ext = lowerString(path.extension().string());
    if ((fs::exists(path) && fs::is_directory(path)) || ext.empty()) {
        return (path / "VisionFlow_Batch_Report.html").string();
    }
    if (ext != ".html" && ext != ".htm") {
        path.replace_extension(".html");
    }
    return path.string();
}

int runBatchQueue(const std::string& csvPath, const std::string& reportPathArg, const char* argv0) {
    std::vector<BatchTask> tasks = loadBatchTasks(csvPath);
    const std::string reportPath = resolveBatchReportPath(csvPath, reportPathArg);

    std::cout << "[Batch] Loaded " << tasks.size() << " task(s) from " << csvPath << "\n";
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        BatchTask& task = tasks[i];
        std::cout << "\n[Batch] [" << (i + 1) << "/" << tasks.size() << "] "
                  << task.modeText << " | " << task.inputPath << " -> " << task.outputPath << "\n";
        try {
            LiveDashboardScope taskDashboard(isLiveDashboardEnabled(), task.inputPath);
            runUserPipeline(task.mode, task.inputPath, task.outputPath, argv0);
            task.summary = readProcessingSummary(task.outputPath);
            task.status = "OK";
            task.message = "done";
            std::cout << "[Batch] Task " << task.id << " done. Improvement: "
                      << std::fixed << std::setprecision(2) << task.summary.improvementPercent << "%\n";
        } catch (const std::exception& e) {
            task.status = "FAILED";
            task.message = e.what();
            std::cout << "[Batch] Task " << task.id << " failed: " << e.what() << "\n";
        }
    }

    writeBatchReportHtml(reportPath, tasks);
    std::cout << "\n[Batch] Report saved to: " << reportPath << "\n";

    int failed = 0;
    for (const BatchTask& task : tasks) {
        if (task.status != "OK") {
            ++failed;
        }
    }
    return failed == 0 ? 0 : 1;
}

} // namespace vfapp
