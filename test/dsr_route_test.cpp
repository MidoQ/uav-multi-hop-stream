#include "../dsr_route.h"
#include "../utils.h"
#include "../sys_config.h"
#include <iostream>
#include <string>
#include <random>

using namespace std;

#define NODE_NUMS 6


// in_addr_t myIP;

void testDsrRoutePacket();

int main(int argc, char** argv)
{
    cout << "dsr_route_test running...\n";

    // testDsrRoutePacket();

    in_addr tmp;
    char myIP_s[INET_ADDRSTRLEN];

    NodeConfig& config = NodeConfig::getInstance();
    config.printNodeConfig();
    in_addr_t myIP = config.getMyIP();
    config.getMyIP_s(myIP_s);

    // 设置目标IP地址列表

    vector<string> dstList_s(NODE_NUMS, string());
    vector<in_addr_t> dstList(NODE_NUMS, 0);

    for (size_t i = 0; i < NODE_NUMS; ++i) {
        dstList_s[i] = "192.168.2." + std::to_string(i + 100);
        inet_pton(AF_INET, dstList_s[i].c_str(), &tmp);
        dstList[i] = tmp.s_addr;
    }

    // 随机数引擎
    std::default_random_engine eng(time(0));
    std::uniform_int_distribution<int> distr(30, 200);

    // 路由请求线程
    auto requester = [&]() {
        DsrRouteGetter getter;
        int timeout_sec = 3;

        for (size_t i = 0; i < dstList.size(); ++i) {
            int msec = distr(eng);
            sleep_for(milliseconds(msec));
            if (dstList[i] != myIP) {
                try {
                    cout << "Requesting route to " << dstList_s[i] << endl;
                    getter.getNextHop(dstList[i], timeout_sec);
                } catch (const char* msg) {
                    cerr << msg << " [ Dst " << dstList_s[i] << " ]" << endl;
                }
            }
        }
    };

    // 路由表打印线程
    auto routeTablePrinter = [&]() {
        routeTableProbe probe;

        while(1) {
            sleep_for(seconds(1));
            probe.printRouteTable();
        }
    };

    // 启动所有线程
    DsrRouteListener& listener = DsrRouteListener::getInstance();
    // listener.startListen();
    thread listen_thread(listener.listenPacket);

    thread listen_thread2(listener.listenPacket);

    sleep_for(seconds(1));

    thread printer(routeTablePrinter);

    if (strcmp(myIP_s, "192.168.2.100") == 0) {
        thread req1(requester);
        thread req2(requester);

        req1.join();
        req2.join();
    }

    listen_thread.join();
    listen_thread2.join();
    printer.join();

    // 主线程永久挂起
    mutex mtx;
    condition_variable cv;
    bool notified = false;
    unique_lock<mutex> lock(mtx);
    while(!notified) {
        cv.wait(lock);
    }

    return 0;
}

void testDsrRoutePacket()
{
    /* 测试 parseFromBuf() */

    // 缓冲区初始化
    char buf[DSR_PKT_MAX_LEN];
    memset(buf, 0, DSR_PKT_MAX_LEN);

    buf[0] = 1;

    uint32_t* cur = nullptr;
    in_addr tmp_addr;

    cur = (uint32_t*)(buf + 1);

    char srcIP_s[] = "192.156.242.131";
    inet_pton(AF_INET, srcIP_s, &tmp_addr);
    *cur = hton32(tmp_addr.s_addr);
    cur++;

    char dstIP_s[] = "112.1.54.29";
    inet_pton(AF_INET, dstIP_s, &tmp_addr);
    *cur = hton32(tmp_addr.s_addr);
    cur++;

    *cur = hton32(378940212); // hop
    cur++;

    *cur = 0x21e52ca9; // reqID
    cur++;

    *cur = hton32(7); // routeListLen
    cur++;

    string str = "192.168.15.1";

    for (int i = 0; i < 7; i++) {
        str[str.size() - 1] = '1' + i;
        inet_pton(AF_INET, str.c_str(), &tmp_addr);
        *cur = hton32(tmp_addr.s_addr);
        cur++;
    }

    // 解析报文
    DsrRoutePacket packet1;
    packet1.printReqInfo();
    packet1.parseFromBuf(buf);
    packet1.printReqInfo();

    /* 测试 serializeToBuf() */

    // 生成报文
    char buf_send[DSR_PKT_MAX_LEN];
    int send_len;

    DsrRoutePacket packet2(packet1);
    send_len = packet2.serializeToBuf(buf_send);

    for (int i = 0; i < send_len; i++) {
        if (buf[i] != buf_send[i]) {
            cout << "Pakcet converted wrong!\n";
            break;
        }
    }

    packet2.printReqInfo();

    /* 测试默认构造函数 */
    DsrRoutePacket packet3(DsrPacketType::request, "192.168.5.6", "10.23.192.235");
    packet3.printReqInfo();

    char src[] = "122.128.57.2";
    char dst[] = "233.123.64.242";
    DsrRoutePacket packet4(DsrPacketType::request, src, dst);
    packet4.printReqInfo();
}
