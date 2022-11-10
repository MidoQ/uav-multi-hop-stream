#include "basic_thread.h"
#include "dsr_route.h"
#include "utils.h"
#include "video_stream.h"
#include <csignal>
#include <iostream>
#include <string>

using std::cerr;
using std::cout;
using std::endl;

Stoppable* pStoppableArray[64];
size_t stoppableCount = 0;

void signalHandler(int signum);

void printLogo();

int main(int argc, char* argv[])
{
    printLogo();

    signal(SIGINT, signalHandler);

    VideoPublisher& videoPublisher = VideoPublisher::getInstance();
    std::thread ThreadVideoPublisher(&VideoPublisher::run, &videoPublisher);

    pStoppableArray[stoppableCount] = &videoPublisher;
    stoppableCount++;

    ThreadVideoPublisher.join();

    return 0;
}

void signalHandler(int signum)
{
    cout << "Intterupt by Ctrl-C. Process exiting...\n";

    if (signum == SIGINT) {
        for (size_t i = 0; i < stoppableCount; i++) {
            pStoppableArray[i]->stop();
        }
    }
}

void printLogo()
{
    cout
        << R"(   __  _____ _    __   _   ______________)" << '\n'
        << R"(  / / / /   | |  / /  / | / / ____/_  __/)" << '\n'
        << R"( / / / / /| | | / /  /  |/ / __/   / /   )" << '\n'
        << R"(/ /_/ / ___ | |/ /  / /|  / /___  / /    )" << '\n'
        << R"(\____/_/  |_|___/  /_/ |_/_____/ /_/     )" << '\n'
        << endl;
}
