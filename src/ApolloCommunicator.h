#pragma once

#include <future>
#include <variant>
#include <vector>
#include "SpScQueue.h"

namespace ApolloCommunicator {
    constexpr const char* OUT_MANIFEST_READY = "MANIFEST_READY";

    struct seek {
        int time;
    };

    struct start {
    };

    using apollo_cmd_t = std::variant<start, seek>;

    inline SpScQueue<apollo_cmd_t> apollo_commands;

    std::future<void> startThread();

    void requestShutdown();

    void sendSimple(const char* command, const std::vector<std::string>& data = {});
}
