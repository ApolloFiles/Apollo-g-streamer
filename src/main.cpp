#include <filesystem>
#include <iostream>
#include "ApolloCommunicator.h"
#include "GstProcessor.h"

int main(int argc, char* argv[]) {
    auto communicatorThread = ApolloCommunicator::startThread();

    int exitCode = GstProcessor::init(argc, argv);
    std::cout << "GStreamer exited with code " << exitCode << std::endl;
    ApolloCommunicator::requestShutdown();
    std::cout << "Waiting for communicator thread to exit..." << std::endl;
    communicatorThread.wait();
    std::cout << "Communicator thread exited" << std::endl;

    std::cout << "Exiting with code " << exitCode << std::endl;
    return exitCode;
}
