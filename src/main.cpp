#include <filesystem>
#include "ApolloCommunicator.h"
#include "GstProcessor.h"

int main(int argc, char* argv[]) {
    auto communicatorThread = ApolloCommunicator::startThread();

    return GstProcessor::init(argc, argv);
}
