#include <iostream>
#include <string>
#include "../utils.h"
#include "../dsr_route.h"

using namespace std;

void testDsrRoutePacket() {
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

    *cur = hton32(378940212);   // hop
    cur++;

    *cur = 0x21e52ca9;  // reqID
    cur++;

    *cur = hton32(7);       // routeListLen
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

int main(int, char**) {
    cout << "dsr_route_test running...\n";

    testDsrRoutePacket();

    return 0;
}