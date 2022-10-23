#include "../dsr_route.h"
#include "../utils.h"
#include "../sys_config.h"
#include "../topo.h"
#include "../sdn_cmd.h"
#include <iostream>
#include <string>
#include <random>
#include <thread>
#include <chrono>

using namespace std;

void testLivePacket();

void testNeibTable();

int main(int argc, char** argv)
{
    // testLivePacket();

    // testNeibTable();

    auto printNeibTable = [&]() {
        NeighborTableProbe probe;
        for (int i = 0; i < 100; ++i) {
            sleep_for(seconds(3));
            probe.printNeighborTable();
        }
    };

    NodeConfig& nodeConfig = NodeConfig::getInstance(); // 初始化
    nodeConfig.printNodeConfig();

    NeighborTable& table = NeighborTable::getInstance();
    TopoGraph& topo = TopoGraph::getInstance();
    DsrRouteListener& routeListener = DsrRouteListener::getInstance();
    LiveBroadcast& liveBrd = LiveBroadcast::getInstance();
    NeighborListener& neibListener = NeighborListener::getInstance();
    NeighborReporter& neibReporter = NeighborReporter::getInstance();
    SdnReporter& sdnReporter = SdnReporter::getInstance();
    SdnListener& sdnListener = SdnListener::getInstance();

    auto printNeibMatrix = [&]() {
        
        while (1) {
            sleep_for(seconds(5));

            std::vector<in_addr_t> nodeList;
            std::vector<std::vector<char>> mat;
            topo.toMatrix(nodeList, mat);

            for (size_t i = 0; i < nodeList.size(); ++i) {
                // uint32_t nodeId = (0xFF000000 & nodeList[i]) >> 24;
                uint32_t nodeId = nodeList[i] >> 24;
                cout << '\t' << std::dec << nodeId;
            }
            cout << '\n';

            for (size_t i = 0; i < mat.size(); ++i) {
                // uint32_t nodeId = (0xFF000000 & nodeList[i]) >> 24;
                uint32_t nodeId = nodeList[i] >> 24;
                cout << nodeId << '\t';
                for (size_t j = 0; j < mat[i].size(); ++j) {
                    cout << (int)mat[i][j] << '\t';
                }
                cout << '\n';
            }
            cout << endl;
        }
    };

    std::thread route_listen_thread(routeListener.listenPacket);
    std::thread liveBrd_thread(liveBrd.pktBroadcasting);
    std::thread liveLis_thread(liveBrd.pktListening);
    // std::thread neibTablePrinter_thread(printNeibTable);
    sleep_for(seconds(10));

    std::thread neib_listen_thread(neibListener.neighborListen);
    sleep_for(seconds(3));

    std::thread neib_report_thread(neibReporter.neighborReport);

    if (nodeConfig.getNodeType() == NodeType::sink) {
        sdnReporter.startReport();
        sdnListener.startListen();
        std::thread matPrinter_thread(printNeibMatrix);
        matPrinter_thread.join();
    }

    route_listen_thread.join();
    liveBrd_thread.join();
    liveLis_thread.join();
    neib_listen_thread.join();
    neib_report_thread.join();
    // neibTablePrinter_thread.join();

    return 0;
}

void testLivePacket()
{
    char myIP_s[] = "192.168.28.103";
    char pktBuf[68];
    char pktBuf2[68];
    in_addr_t myIP;
    in_addr tmp;
    
    inet_pton(AF_INET, myIP_s, &tmp);
    myIP = tmp.s_addr;

    double px = 241532.5425654654754;
    double py = 4325324.7867434532;

    LivePacket pkt1(myIP, px, py);

    pkt1.serializeToBuf(pktBuf);

    LivePacket pkt2;
    pkt2.parseFromBuf(pktBuf);

    pkt2.serializeToBuf(pktBuf2);
}

void testNeibTable()
{
    NeighborTableProbe probe;

    auto printer = [&]() {
        while (1) {
            sleep_for(seconds(3));
            probe.printNeighborTable();
        }
    };

    std::thread liveBrd_thread(LiveBroadcast::pktBroadcasting);
    std::thread liveLis_thread(LiveBroadcast::pktListening);
    std::thread printer_thread(printer);
    liveBrd_thread.join();
    liveLis_thread.join();
    printer_thread.join();
}