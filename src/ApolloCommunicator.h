#pragma once

#include <future>
#include <variant>
#include <vector>
#include "SpScQueue.h"

namespace ApolloCommunicator {
    constexpr const char* OUT_MANIFEST_READY = "MANIFEST_READY";
    constexpr const char* OUT_PIPELINE_STATUS = "PIPELINE_STATUS";

    struct play {
    };

    struct pause {
    };

    struct seek {
        int time;
    };

    using apollo_cmd_t = std::variant<play, pause, seek>;

    inline SpScQueue<apollo_cmd_t> apollo_commands;

    std::future<void> startThread();

    void requestShutdown();

    void sendSimple(const char* command, const std::vector<std::string>& data = {});
}
