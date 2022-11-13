#include "basic_thread.h"
#include "dsr_route.h"
#include "sdn_cmd.h"
#include "topo.h"
#include "utils.h"
#include "video_stream.h"
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using std::cerr;
using std::cout;
using std::endl;
using namespace std::this_thread; // sleep_for, sleep_until
using namespace std::chrono; // nanoseconds, system_clock, seconds

Stoppable* pStoppableList[64];
std::thread* pThreadList[64];
size_t threadCount = 0;

bool exitFlag = false;
std::mutex mtx4Exit;
std::condition_variable cond4Exit;

/* 函数声明 */

void signalHandler(int signum);

void addToStopList(Stoppable& threadObj, std::thread& thd);

void printLogo();

/* 主函数 */

int main(int argc, char* argv[])
{
    printLogo();

    signal(SIGINT, signalHandler);

    // 节点配置初始化
    NodeConfig& nodeConfig = NodeConfig::getInstance();
    NeighborTable& table = NeighborTable::getInstance();
    TopoGraph& topo = TopoGraph::getInstance();
    nodeConfig.printNodeConfig();

    // 获取所有单例
    DsrRouteListener& routeListener = DsrRouteListener::getInstance();
    LiveBroadcast& liveBroadcast = LiveBroadcast::getInstance();
    LiveListen& liveListen = LiveListen::getInstance();
    NeighborListener& neibListener = NeighborListener::getInstance();
    NeighborReporter& neibReporter = NeighborReporter::getInstance();
    SdnReporter& sdnReporter = SdnReporter::getInstance();
    SdnListener& sdnListener = SdnListener::getInstance();
    // VideoPublisher& videoPublisher = VideoPublisher::getInstance();

    // 路由发现
    std::thread routeListenerThread(&DsrRouteListener::run, &routeListener);
    addToStopList(routeListener, routeListenerThread);

    // 存活广播
    std::thread liveBroadcastThread(&LiveBroadcast::run, &liveBroadcast);
    addToStopList(liveBroadcast, liveBroadcastThread);

    std::thread liveListenThread(&LiveListen::run, &liveListen);
    addToStopList(liveListen, liveListenThread);

    sleep_for(seconds(10));

    // 邻居表上报
    std::thread neibListenerThread(&NeighborListener::run, &neibListener);
    addToStopList(neibListener, neibListenerThread);

    sleep_for(seconds(3));

    std::thread neibReporterThread(&NeighborReporter::run, &neibReporter);
    addToStopList(neibReporter, neibReporterThread);

    // SDN 汇报与命令接收
    if (nodeConfig.getNodeType() == NodeType::sink) {
        static std::thread sdnReporterThread(&SdnReporter::run, &sdnReporter);
        addToStopList(sdnReporter, sdnReporterThread);

        static std::thread sdnListenerThread(&SdnListener::run, &sdnListener);
        addToStopList(sdnListener, sdnListenerThread);
    }

    // 视频流
    // std::thread videoPublisherThread(&VideoPublisher::run, &videoPublisher);
    // addToStopList(videoPublisher, videoPublisherThread);

    // 在捕获到SIGINT之前，永久挂起主线程
    std::unique_lock<std::mutex> lock(mtx4Exit);
    while (!exitFlag) {
        cond4Exit.wait(lock);
    }

    // 等待所有子线程结束
    for (int i = threadCount - 1; i >= 0; i--) {
        pThreadList[i]->join();
    }

    cout << "All child threads exit!\n"
         << "Exiting...\n";

    return 0;
}

/* 函数定义 */

void signalHandler(int signum)
{
    cout << "\nIntterupt by Ctrl+C. Shutting down gracefully...\n";

    if (signum == SIGINT) {
        // 退出所有子线程
        for (size_t i = 0; i < threadCount; i++) {
            pStoppableList[i]->stop();
        }
        // 退出主线程
        std::unique_lock<std::mutex> lock(mtx4Exit);
        exitFlag = true;
        cond4Exit.notify_all();
    }
}

void addToStopList(Stoppable& threadObj, std::thread& thd)
{
    pStoppableList[threadCount] = &threadObj;
    pThreadList[threadCount] = &thd;
    threadCount++;
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
