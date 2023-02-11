#include "Stream/Processor.h"

#include "ApolloCommunicator.h"
#include "Stream/Pipeline.h"
#include <filesystem>
#include <fstream>
#include <gst/gst.h>
#include <iostream>
#include <string>
#include <vector>

namespace gst {
constexpr std::string HLS_TARGET_LOCATION = "./hls_out/";
const std::string MANIFEST_TARGET_LOCATION = HLS_TARGET_LOCATION + "manifest.m3u8";

bool waitForAppolloStart() {
    using namespace std::chrono_literals;
    auto res = ApolloCommunicator::apollo_commands.pop_blocking(30s);
    if (!res) {
        std::cerr << "timeout – aborting" << std::endl;
        return false;
    }
    if (!std::get_if<ApolloCommunicator::start>(&res.value())) {
        std::cerr << "unexpected command – aborting" << std::endl;
        return false;
    }
    return true;
}

int init(int argc, char* argv[], const std::string& fileUri) {
    gst_init(&argc, &argv);

    auto max_retries = 2;
    for (int i = 0; i < max_retries; ++i) {
        std::filesystem::create_directories(HLS_TARGET_LOCATION);
        std::filesystem::remove(MANIFEST_TARGET_LOCATION);

        Pipeline pipeline(fileUri, ApolloCommunicator::apollo_updates);
        if (!pipeline.prepare(i > 0)) {
            std::cerr << "Failed to prepare pipeline" << std::endl;
            continue;
        }
        std::cerr << "Pipeline ready – waiting for start command..." << std::endl;
        if (!waitForAppolloStart()) {
            continue;
        }
        if (!pipeline.start()) {
            std::cerr << "Failed to start pipeline" << std::endl;
            continue;
        }
        auto res = pipeline.loop();
        if (!res) {
            if (res.error() == -20 && i > 0) {
                std::cerr << "Pipeline has been restarted with software decoding, but still failed..." << std::endl;
            } else {
                std::cerr << "Pipeline failed" << std::endl;
            }
            continue;
        } else {
            return 0;
        }
    }
    return 1;
}
}// namespace gst
