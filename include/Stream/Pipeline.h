//
// Created by nudelerde on 11.02.23.
//

#pragma once

#include <string>
#include <memory>
#include <optional>
#include <expected>

#include <gst/gst.h>
#include "SpScQueue.h"

#include "ApolloCommunicator.h"

namespace gst {

struct Pipeline {
    struct DeletePipeline {
        void operator()(GstElement* pipe);
    };
    using error_t = int;

    Pipeline(std::string  fileUri, SpScQueue<ApolloCommunicator::apollo_out_cmd_t>& outputQueue);

    std::expected<void, error_t> prepare(bool forceSoftware);
    std::expected<void, error_t> start();
    std::expected<void, error_t> loop();

    void dumpPipelineDebugGraph(GstState new_state);
    std::expected<gint64, error_t> getPosition();
    std::expected<gint64, error_t> getDuration();

    bool doesHlsManifestExist();
    bool doesHlsManifestLookBroken();

private:
    bool checkManifest();
    std::string buildPipelineDescription(bool forceSoftware);

    SpScQueue<ApolloCommunicator::apollo_out_cmd_t>& outputQueue;
    std::string fileUri;
    std::unique_ptr<GstElement, DeletePipeline> pipeline;
    bool manifestReady = false;

    std::string audioConvertDescription;
};

}