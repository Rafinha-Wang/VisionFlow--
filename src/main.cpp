#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "app/AppTypes.h"
#include "app/BatchRunner.h"
#include "app/PipelineRunner.h"
#include "app/TuiController.h"
#include "app/VideoIOAdapter.h"


using namespace vfapp;

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            return runTui(argv[0]);
        }

        if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
            printUsage();
            return 0;
        }

        if (argc == 2 && (std::string(argv[1]) == "--tui" || std::string(argv[1]) == "menu")) {
            return runTui(argv[0]);
        }

        if ((argc == 3 || argc == 4) && std::string(argv[1]) == "--batch") {
            const std::string reportPath = argc == 4 ? argv[3] : "";
            return runBatchQueue(argv[2], reportPath, argv[0]);
        }

        if ((argc == 4 || argc == 5) && std::string(argv[1]) == "--pack") {
            const std::uint32_t fps = argc == 5 ? static_cast<std::uint32_t>(std::max(1, std::atoi(argv[4]))) : 30U;
            return runPackCommand(argv[2], argv[3], fps);
        }

        if (argc == 4 && std::string(argv[1]) == "--unpack") {
            return runUnpackCommand(argv[2], argv[3]);
        }

        if (argc == 3 && std::string(argv[1]) == "--vfinfo") {
            return runVfvidInfoCommand(argv[2]);
        }

        if (argc == 3) {
            return runUserPipeline(Mode::Auto, argv[1], argv[2], argv[0]);
        }

        if (argc == 5 && std::string(argv[1]) == "--mode") {
            return runUserPipeline(parseModeOrThrow(argv[2]), argv[3], argv[4], argv[0]);
        }

        if (argc == 4 && isModeWord(argv[1])) {
            const std::string modeArg = argv[1];
            return runUserPipeline(parseModeOrThrow(modeArg), argv[2], argv[3], argv[0]);
        }

        printUsage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[Error] " << e.what() << "\n";
        return 1;
    }
}
