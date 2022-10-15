#include "../dsr_route.h"
#include "../utils.h"
#include "../sys_config.h"
#include "../topo.h"
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

    // LiveBroadcast& liveBroadcast = LiveBroadcast::getInstance();

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
