#include <filesystem>
#include <iostream>
#include "ApolloCommunicator.h"
#include "GstProcessor.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::string argv0 = argv[0];
        std::string exeName = argv0.substr(argv0.find_last_of("/\\") + 1);

        std::cerr << "Usage: " << exeName << " <file URI>" << std::endl;
        return 1;
    }

    std::string fileUri = argv[1];
    if (!fileUri.starts_with("file://")) {
        std::cerr << "File URI must start with 'file://'" << std::endl;
        return 1;
    }

    auto communicatorThread = ApolloCommunicator::startThread();

    int exitCode = GstProcessor::init(argc, argv, fileUri);
    std::cout << "GStreamer exited with code " << exitCode << std::endl;
    ApolloCommunicator::requestShutdown();
    std::cout << "Waiting for communicator thread to exit..." << std::endl;
    communicatorThread.wait();
    std::cout << "Communicator thread exited" << std::endl;

    std::cout << "Exiting with code " << exitCode << std::endl;
    return exitCode;
}
