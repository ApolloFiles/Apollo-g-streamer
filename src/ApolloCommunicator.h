#pragma once

#include <future>
#include <variant>
#include "SpScQueue.h"

namespace ApolloCommunicator {

    struct seek {
        int time;
    };

    struct start {

    };

    using apollo_cmd_t = std::variant<start, seek>;

    inline SpScQueue<apollo_cmd_t> apollo_commands;

    std::future<void> startThread();
}
