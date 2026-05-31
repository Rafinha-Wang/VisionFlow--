#include "report/HtmlReport.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace vf {
namespace {

std::string htmlPath(std::string path) {
    for (char& ch : path) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    return path;
}

bool hasPath(const std::string& path) {
    return !path.empty();
}

template <typename T>
std::string numberToString(T value, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

void writeStyle(std::ofstream& out) {
    out << "  <style>\n";
    out << "    body{margin:0;font-family:Segoe UI,Arial,sans-serif;background:#f6f7fb;color:#111827;}\n";
    out << "    header{padding:28px 44px;background:linear-gradient(135deg,#111827,#2563eb);color:white;}\n";
    out << "    header h1{margin:0;font-size:32px;} header p{margin:8px 0 0;opacity:.82;font-size:14px;}\n";
    out << "    main{padding:28px 44px 48px;} .card{background:white;border-radius:18px;padding:20px;margin-bottom:22px;box-shadow:0 12px 32px rgba(15,23,42,.08);}\n";
    out << "    h2{margin:0 0 16px;font-size:22px;} .grid4{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px;}\n";
    out << "    .grid3{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:18px;} .grid2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px;}\n";
    out << "    .stat{border:1px solid #e5e7eb;border-radius:14px;padding:14px;background:#f8fafc;} .label{font-size:12px;color:#64748b;} .value{margin-top:5px;font-size:22px;font-weight:800;color:#1d4ed8;}\n";
    out << "    .metric-table{width:100%;border-collapse:collapse;overflow:hidden;border-radius:14px;border:1px solid #e5e7eb;}\n";
    out << "    .metric-table th{background:#eff6ff;color:#1e3a8a;text-align:left;font-size:13px;padding:12px;border-bottom:1px solid #dbeafe;}\n";
    out << "    .metric-table td{padding:12px;border-bottom:1px solid #e5e7eb;font-size:13px;vertical-align:top;} .metric-table tr:last-child td{border-bottom:none;}\n";
    out << "    .metric-good{color:#16a34a;font-weight:800;} .metric-bad{color:#dc2626;font-weight:800;} .metric-note{font-size:13px;color:#64748b;margin-top:12px;}\n";
    out << "    .image-card{border:1px solid #e5e7eb;border-radius:14px;padding:12px;background:#fafafa;transition:transform .22s ease,box-shadow .22s ease;}\n";
    out << "    .image-card:hover{transform:translateY(-4px);box-shadow:0 14px 30px rgba(15,23,42,.14);} .image-card h3{margin:0 0 10px;font-size:15px;color:#334155;}\n";
    out << "    img{width:100%;height:auto;border-radius:10px;background:#111827;transition:filter .22s ease,transform .22s ease,box-shadow .22s ease;}\n";
    out << "    img:hover{filter:brightness(1.18) contrast(1.05);transform:scale(1.012);box-shadow:0 10px 24px rgba(0,0,0,.18);}\n";
    out << "    .hint{color:#64748b;font-size:13px;margin:0 0 14px;} @media(max-width:950px){.grid4,.grid3,.grid2{grid-template-columns:1fr;}main,header{padding-left:20px;padding-right:20px;}}\n";
    out << "  </style>\n";
}

void writeHead(std::ofstream& out, const std::string& title) {
    out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    out << "  <meta charset=\"UTF-8\">\n";
    out << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    out << "  <title>" << title << "</title>\n";
    writeStyle(out);
    out << "</head>\n<body>\n";
}

void writeImageCard(std::ofstream& out, const std::string& title, const std::string& imagePath, const std::string& alt) {
    if (!hasPath(imagePath)) {
        return;
    }
    out << "        <div class=\"image-card\"><h3>" << title << "</h3><img src=\"" << htmlPath(imagePath) << "\" alt=\"" << alt << "\"></div>\n";
}

void writeStat(std::ofstream& out, const std::string& label, const std::string& value) {
    out << "      <div class=\"stat\"><div class=\"label\">" << label << "</div><div class=\"value\">" << value << "</div></div>\n";
}


std::string formatMetric(double value, const std::string& suffix, int precision = 2) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    if (!suffix.empty()) {
        oss << " " << suffix;
    }
    return oss.str();
}

std::string formatMetricChange(double before, double after) {
    if (before <= 1e-9) {
        return "N/A";
    }
    const double change = (before - after) / before * 100.0;
    std::ostringstream oss;
    if (change >= 0.0) {
        oss << "<span class=\"metric-good\">&#8595;&nbsp;" << std::fixed << std::setprecision(1) << change << "%</span>";
    } else {
        oss << "<span class=\"metric-bad\">&#8593;&nbsp;" << std::fixed << std::setprecision(1) << -change << "%</span>";
    }
    return oss.str();
}

void writeMetricRow(std::ofstream& out,
                    const std::string& name,
                    const std::string& inputValue,
                    const std::string& outputValue,
                    const std::string& change,
                    const std::string& meaning) {
    out << "      <tr><td><strong>" << name << "</strong></td><td>" << inputValue << "</td><td>" << outputValue
        << "</td><td>" << change << "</td><td>" << meaning << "</td></tr>\n";
}

void writeMetricsSection(std::ofstream& out, const SequenceReportInfo& info) {
    if (!info.hasStabilizationMetrics) {
        return;
    }
    out << "    <section class=\"card\"><h2>4. Stabilization Metrics</h2>\n";
    out << "    <p class=\"hint\">This table compares estimated camera motion before and after stabilization. Lower values usually mean less shake or a smoother camera path.</p>\n";
    out << "    <table class=\"metric-table\">\n";
    out << "      <tr><th>Metric</th><th>Input</th><th>Output</th><th>Change</th><th>Meaning</th></tr>\n";
    writeMetricRow(out,
                   "Average Translation",
                   formatMetric(info.inputMetrics.averageTranslation, "px/frame"),
                   formatMetric(info.outputMetrics.averageTranslation, "px/frame"),
                   formatMetricChange(info.inputMetrics.averageTranslation, info.outputMetrics.averageTranslation),
                   "Average frame-to-frame camera movement. It is most useful for static fixed-camera clips.");
    writeMetricRow(out,
                   "P90 Translation",
                   formatMetric(info.inputMetrics.p90Translation, "px/frame"),
                   formatMetric(info.outputMetrics.p90Translation, "px/frame"),
                   formatMetricChange(info.inputMetrics.p90Translation, info.outputMetrics.p90Translation),
                   "90th percentile movement. Lower means most sudden frame jumps are reduced.");
    writeMetricRow(out,
                   "Jitter RMS",
                   formatMetric(info.inputMetrics.jitterRms, ""),
                   formatMetric(info.outputMetrics.jitterRms, ""),
                   formatMetricChange(info.inputMetrics.jitterRms, info.outputMetrics.jitterRms),
                   "High-frequency trajectory shake. It is especially useful for motion / virtual-gimbal mode.");
    writeMetricRow(out,
                   "Trajectory Smoothness",
                   formatMetric(info.inputMetrics.trajectorySmoothness, ""),
                   formatMetric(info.outputMetrics.trajectorySmoothness, ""),
                   formatMetricChange(info.inputMetrics.trajectorySmoothness, info.outputMetrics.trajectorySmoothness),
                   "Second-order path variation. Lower means the camera path changes more smoothly.");
    out << "    </table>\n";
    out << "    <p class=\"metric-note\">For motion mode, average translation may stay similar because the intended camera movement is preserved. Jitter RMS and trajectory smoothness are the more important indicators.</p>\n";
    out << "    </section>\n";
}

} // namespace

void HtmlReport::generateSequence(const std::string& path, const SequenceReportInfo& info) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot create sequence HTML report: " + path);
    }

    writeHead(out, info.title.empty() ? "VisionFlow Report" : info.title);
    out << "  <header><h1>" << (info.title.empty() ? "VisionFlow" : info.title) << "</h1><p>Visual-first report.</p></header>\n";
    out << "  <main>\n";

    out << "    <section class=\"card\"><h2>Summary</h2><div class=\"grid4\">\n";
    writeStat(out, "Frames", std::to_string(info.frameCount));
    writeStat(out, "Size", std::to_string(info.width) + " x " + std::to_string(info.height));
    writeStat(out, "Time", numberToString(info.elapsedMs / 1000.0, 2) + " s");
    writeStat(out, "Mode", info.modeName.empty() ? "Video" : info.modeName);
    out << "    </div></section>\n";

    out << "    <section class=\"card\"><h2>1. Original Frames</h2><div class=\"grid3\">\n";
    writeImageCard(out, "First", info.firstFrameImage, "first frame");
    writeImageCard(out, "Middle", info.middleFrameImage, "middle frame");
    writeImageCard(out, "Last", info.lastFrameImage, "last frame");
    out << "    </div></section>\n";

    out << "    <section class=\"card\"><h2>2. Final Output</h2><div class=\"grid3\">\n";
    writeImageCard(out, "First", info.firstOutputImage, "first final output");
    writeImageCard(out, "Middle", info.middleOutputImage, "middle final output");
    writeImageCard(out, "Last", info.lastOutputImage, "last final output");
    out << "    </div></section>\n";

    if (hasPath(info.firstCompareImage)) {
        out << "    <section class=\"card\"><h2>3. Before / After</h2><p class=\"hint\">Left: input. Right: processed output.</p><div class=\"grid3\">\n";
        writeImageCard(out, "First", info.firstCompareImage, "first compare");
        writeImageCard(out, "Middle", info.middleCompareImage, "middle compare");
        writeImageCard(out, "Last", info.lastCompareImage, "last compare");
        out << "    </div></section>\n";
    }

    writeMetricsSection(out, info);

    if (hasPath(info.motionCurveImage) || hasPath(info.motionHeatmapImage)) {
        out << "    <section class=\"card\"><h2>5. Motion Overview</h2><div class=\"grid2\">\n";
        writeImageCard(out, "Motion Curve", info.motionCurveImage, "motion curve");
        writeImageCard(out, "Motion Heatmap", info.motionHeatmapImage, "motion heatmap");
        out << "    </div></section>\n";
    }

    if (hasPath(info.affineRotationSvg) || hasPath(info.affineScaleSvg) || hasPath(info.affineInlierSvg)) {
        out << "    <section class=\"card\"><h2>6. Camera Curves</h2><div class=\"grid3\">\n";
        writeImageCard(out, "Rotation", info.affineRotationSvg, "rotation curve");
        writeImageCard(out, "Scale", info.affineScaleSvg, "scale curve");
        writeImageCard(out, "Inliers", info.affineInlierSvg, "inlier curve");
        out << "    </div></section>\n";
    }

    if (hasPath(info.middleGrayImage) || hasPath(info.middleEdgeImage) || hasPath(info.middleDiffImage)) {
        out << "    <section class=\"card\"><h2>7. Preprocessing Samples</h2><div class=\"grid3\">\n";
        writeImageCard(out, "Gray", info.middleGrayImage, "gray sample");
        writeImageCard(out, "Sobel", info.middleEdgeImage, "edge sample");
        writeImageCard(out, "Diff", info.middleDiffImage, "diff sample");
        out << "    </div></section>\n";
    }

    out << "  </main>\n</body>\n</html>\n";
}

} // namespace vf
