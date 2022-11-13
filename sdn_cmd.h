#ifndef _SDN_CMD_H
#define _SDN_CMD_H

#include "sys_config.h"
#include "topo.h"
#include "utils.h"
#include "basic_thread.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <vector>

#define PORT_SDN 7777
#define TOPO_PKT_MAX_LEN 512
#define SDN_CMD_MAX_LEN 64
#define DEFAULT_TOPO_REPORT_SEC 8

using std::cerr;
using std::cout;
using std::endl;
using namespace std::this_thread; // sleep_for, sleep_until
using namespace std::chrono; // nanoseconds, system_clock, seconds

typedef struct Polar {
    double dis; // 距离
    double arg; // 弧度
} Polar;

/**
 * @brief 向控制器汇报拓扑信息等
 */
class SdnReporter : public Stoppable
{
private:
    int runCount;
    size_t reportInterval; // 拓扑汇报的时间间隔，单位为秒
    std::vector<in_addr_t> nodeList;
    std::vector<std::vector<char>> mat;
    std::map<in_addr_t, Position> posList;

private:
    SdnReporter();
    SdnReporter(const SdnReporter&) = delete;
    SdnReporter& operator=(const SdnReporter&) = delete;

    size_t getReportInterval() {
        return reportInterval;
    }

    void setReportInterval(size_t sec) {
        reportInterval = sec;
    }

    void setPosListFromTopo();

    size_t serializeTopo(char* buf);

public:
    ~SdnReporter();

    static SdnReporter& getInstance() {
        static SdnReporter instance;
        return instance;
    }

    void run();
};

enum SdnCmdType : char {
    unknown = 0,
    startVideo = 1,
    endVideo = 2
};

/**
 * @brief 接收控制器的SDN指令
 */
class SdnListener : public Stoppable
{
private:
    int runCount;
    in_addr_t networkIP;

private:
    SdnListener();
    SdnListener(const SdnListener&) = delete;
    SdnListener& operator=(const SdnListener&) = delete;

    SdnCmdType checkCmdType(char* buf);

    in_addr_t numstr2IP(char* buf);

public:
    ~SdnListener();

    static SdnListener& getInstance() {
        static SdnListener instance;
        return instance;
    }

    void run();
};

#endif