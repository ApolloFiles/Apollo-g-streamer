#include "GstProcessor.h"

#include <gst/gst.h>
#include <filesystem>
#include <vector>
#include <string>
#include <iostream>
#include "ApolloCommunicator.h"
#include "SpScQueue.h"

namespace GstProcessor {

    static std::string generateFullPipeline(const std::string& srcString, const std::string& sinkString,
                                            const std::vector<std::string>& video,
                                            const std::vector<std::string>& audio);


    int init(int argc, char* argv[]) {
        gst_init(&argc, &argv);

        // TODO: target dir is provided by parent process
        std::filesystem::create_directories("./hls");

        // TODO: input file is provided by parent process (has to start with file:// and is expected to be properly encoded)
        std::string srcDescription = "uridecodebin name=src uri=file:///home/christian/Downloads/apollo-g-streamer/simple.mkv";
        std::string sinkDescription = R"(hlssink2 name=sink location="hls/%d.ts" playlist-location="hls/index.m3u8" max-files=0 playlist-length=0 target-duration=2)";

        // TODO: Auto detect streams; Have them provided/controlled by the parent process
        std::string videoBinDescription = "videoconvert ! videorate ! video/x-raw,format=I420,framerate=24/1 ! x264enc bitrate=8192 key-int-max=24";
        std::string audioBinDescription = "audioconvert ! audiorate ! audio/x-raw,channels=2 ! avenc_aac maxrate=48000";

        std::string pipelineDescription = generateFullPipeline(srcDescription, sinkDescription,
                                                               {videoBinDescription}, {audioBinDescription});

        GError* err = nullptr;
        GstElement* pipeline = gst_parse_launch(pipelineDescription.c_str(), &err);
        if (!pipeline || err != nullptr) {
            g_printerr("Failed to create pipeline %s", err->message);
            return 1;
        }

        auto res = ApolloCommunicator::apollo_commands.pop_blocking(30000);
        if (!std::get_if<ApolloCommunicator::start>(&res)) {
            std::cerr << "unexpected command" << std::endl;
            exit(1);
        }
        g_print("Starting pipeline\n");

        /* Start playing */
        const GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_printerr("Unable to set the pipeline to the playing state.\n");
            return 1;
        }

        bool terminate = false;

        /* Listen to the bus */
        GstBus* bus = gst_element_get_bus(pipeline);
        do {
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                                         static_cast<GstMessageType>(GST_MESSAGE_STATE_CHANGED |
                                                                                     GST_MESSAGE_ERROR |
                                                                                     GST_MESSAGE_EOS));

            /* Parse message */
            if (msg != nullptr) {
                gchar* debug_info;

                switch (GST_MESSAGE_TYPE(msg)) {
                    case GST_MESSAGE_ERROR:
                        gst_message_parse_error(msg, &err, &debug_info);
                        g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
                        g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
                        g_clear_error(&err);
                        g_free(debug_info);
                        terminate = TRUE;
                        break;
                    case GST_MESSAGE_EOS:
                        g_print("End-Of-Stream reached.\n");
                        terminate = TRUE;
                        break;
                    case GST_MESSAGE_STATE_CHANGED:
                        /* We are only interested in state-changed messages from the pipeline */
                        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                            GstState old_state, new_state, pending_state;
                            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                            g_print("Pipeline state changed from %s to %s:\n",
                                    gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));

                            // current system time in millis
                            const std::string newPipelineStateName = gst_element_state_get_name(new_state);
                            const std::string s = "pipeline-state-" + newPipelineStateName + "-" + std::to_string(
                                    std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
                            GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, s.c_str());
                        }
                        break;
                    default:
                        /* We should not reach here */
                        g_printerr("Unexpected message received.\n");
                        break;
                }
                gst_message_unref(msg);
            }
        } while (!terminate);

        return 0;
    }


    static std::string generateFullPipeline(const std::string& srcString, const std::string& sinkString,
                                            const std::vector<std::string>& video, const std::vector<std::string>& audio) {
        std::string result = srcString + " " + sinkString;

        for (const auto& conv: video) {
            result += " src. ! queue ! " + conv + " ! sink.video ";
        }
        for (const auto& conv: audio) {
            result += " src. ! queue ! " + conv + " ! sink.audio ";
        }

        return result;
    }

}
