#pragma once

#include <future>
#include <variant>
#include <vector>
#include <string>
#include "SpScQueue.h"

namespace ApolloCommunicator {
    constexpr const char* OUT_MANIFEST_READY = "MANIFEST_READY";

    struct manifest_ready {
        std::string manifest;
        long duration_seconds;
    };

    struct seek {
        int time;
    };
    struct start {
    };

    using apollo_in_cmd_t = std::variant<start, seek>;
    using apollo_out_cmd_t = std::variant<manifest_ready>;

    inline SpScQueue<apollo_in_cmd_t> apollo_commands;
    inline SpScQueue<apollo_out_cmd_t> apollo_updates;

    std::thread startThread();

    void requestShutdown();

    void sendSimple(const char* command, const std::vector<std::string>& data = {});
}
