/**********************************************************
 * Author: Q.M.Feng
 * Modified: 2022-10-02
 * Description: DSR路由功能
 **********************************************************/

#ifndef _DSR_ROUTE_H
#define _DSR_ROUTE_H

#include "utils.h"
#include "basic_thread.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define PORT_DSR 9190
#define DSR_REQ_HEADER_LEN 21
#define DSR_PKT_MAX_LEN 400
#define DSR_PKT_GENERAL_LEN 100

// using namespace std;
using std::cerr;
using std::cout;
using std::endl;
using namespace std::this_thread; // sleep_for, sleep_until
using namespace std::chrono; // nanoseconds, system_clock, seconds

enum class DsrPacketType : char {
    request = 1,
    response = 2
};

/**
 * @brief 每个实例代表一个路由请求/回复报文
 */
class DsrRoutePacket {
private:
    // 头部信息 (header)
    DsrPacketType type;
    in_addr_t srcIP;
    in_addr_t dstIP;
    uint32_t hop;
    uint32_t reqID;
    uint32_t routeListLength;

    // 路由记录
    std::vector<in_addr_t> routeList;

public:
    DsrRoutePacket();
    DsrRoutePacket(DsrPacketType pktType, const char* srcIP_s, const char* dstIP_s);
    DsrRoutePacket(DsrPacketType pktType, in_addr_t srcIP, in_addr_t dstIP);
    DsrRoutePacket(const DsrRoutePacket& dsrRouteRequest);
    ~DsrRoutePacket() = default;

    // Operation of type

    DsrPacketType getType() {
        return type;
    }

    void setType(DsrPacketType _type) {
        this->type = _type;
    }

    // Operation of srcIP

    in_addr_t getSrcIP() {
        return srcIP;
    }

    void setSrcIP(in_addr_t _srcIP) {
        this->srcIP = _srcIP;
    }

    void setSrcIP(const char* _srcIP) {
        in_addr tmp;
        inet_pton(AF_INET, _srcIP, &tmp);
        this->srcIP = tmp.s_addr;
    }

    // Operation of dstIP

    in_addr_t getDstIP() {
        return dstIP;
    }

    void setDstIP(in_addr_t _dstIP) {
        this->dstIP = _dstIP;
    }

    void setDstIP(const char* _dstIP) {
        in_addr tmp;
        inet_pton(AF_INET, _dstIP, &tmp);
        this->dstIP = tmp.s_addr;
    }

    // Operation of hop

    uint32_t getHop() {
        return hop;
    }

    void setHop(uint32_t _hop) {
        this->hop = _hop;
    }

    void increaseHop() {
        hop++;
    }

    void decreaseHop() {
        hop--;
    }

    // Operation of reqID

    uint32_t getReqID() {
        return reqID;
    }

    void setReqID(uint32_t _reqID) {
        this->reqID = _reqID;
    }

    // Operation of routeList

    std::vector<in_addr_t>& getRouteList() {
        return routeList;
    }

    void attachRoute(in_addr_t newIP) {
        routeList.push_back(newIP);
        routeListLength++;
    }

    void attachRoute(const char* newIP) {
        in_addr tmp;
        inet_pton(AF_INET, newIP, &tmp);
        attachRoute(tmp.s_addr);
    }

    void reverseRoute() {
        size_t n = routeList.size();
        for (size_t i = 0; i < n / 2; ++i) {
            in_addr_t tmp = routeList[i];
            routeList[i] = routeList[n - i - 1];
            routeList[n - i - 1] = tmp;
        }
    }
    
    /// @return 报文实例转换为字符串后的大小
    size_t expectedBufLen() {
        return DSR_REQ_HEADER_LEN + routeListLength * 4;
    }

    /// @brief 打印路由请求报文信息
    void printReqInfo();

    /// @brief 将缓冲区的内容解析到当前 DsrRoutePacket 实例
    /// @param pktBuf 路由请求报文缓冲区
    void parseFromBuf(const char* pktBuf);

    /// @brief 将当前 DsrRoutePacket 实例的内容转换为字符串，以便通过网络发送
    /// @param pktBuf 路由请求报文缓冲区
    /// @return 生成的路由请求报文总长度
    int serializeToBuf(char* pktBuf);
};

/**
 *  @brief 路由表的表项值结构体
 */
typedef struct RouteTableVal {
    in_addr_t nextHopIP;
    int metric;
    RouteTableVal()
        : nextHopIP(0)
        , metric(INT32_MAX)
    {
    }
    RouteTableVal(in_addr_t _nextHopIP, int _metric)
        : nextHopIP(_nextHopIP)
        , metric(_metric)
    {
    }
} routeTableVal;

/**
 * @brief 全局路由表单例（仅在DsrRouteGetter初始化时，初始化一次）
 */
class DsrRouteTable {
    friend class DsrRouteGetter;
    friend class DsrRouteListener;
    friend class routeTableProbe;

private:
    // 路由表，由srcIP映射到表项（下一跳IP、距离）
    std::map<in_addr_t, routeTableVal> routeTable;

private:
    DsrRouteTable();
    DsrRouteTable(const DsrRouteTable&) = delete;
    DsrRouteTable& operator=(const DsrRouteTable&) = delete;

    /// @brief 添加一条路由表项（当表项不存在时插入，当新表项的距离更小时更新，否则无操作）
    /// @param _dstIP 预计添加表项的目的IP
    /// @param _nextHopIP 预计添加表项的下一跳IP
    /// @param _metric 预计添加表项的距离（本节点到目的节点）
    /// @return =true 已添加或更新表项 =false 表项已存在且未更新
    bool updateRouteItem(in_addr_t _dstIP, in_addr_t _nextHopIP, int _metric);

    /// @brief 查找一条路由表项
    /// @param dstIP 欲查找路由的目标IP
    /// @param item 若存在路由表项，则表项将保存在item中
    /// @return =true 查找到表项，并保存在item中 =false 未查找到表项
    bool findRouteItem(in_addr_t dstIP, routeTableVal& item);

    /// @brief 删除一条表项
    /// @param dstIP 欲删除表项的目标节点IP
    /// @return true 成功删除  false 表项不存在
    bool deleteRouteItem(in_addr_t dstIP);

    void printTable();

public:
    ~DsrRouteTable();

    static DsrRouteTable& getInstance()
    {
        static DsrRouteTable instance;
        return instance;
    }
};

/**
 * @brief 记录本节点处理过的路由请求ID
 */
class DsrReqIdRecorder {
    friend class DsrRouteGetter;
    friend class DsrRouteListener;

private:
    std::unordered_map<in_addr_t, std::unordered_set<unsigned int>> idHistory;

private:
    DsrReqIdRecorder();
    DsrReqIdRecorder(const DsrReqIdRecorder&) = delete;
    DsrReqIdRecorder& operator=(const DsrReqIdRecorder&) = delete;

    /// @brief 记录一个已处理的路由请求ID
    /// @param reqID 路由请求ID
    void addReqID(in_addr_t srcIP, uint32_t reqID);

    /// @brief 判断路由请求ID是否已记录过
    /// @param reqID 路由请求ID
    /// @return =true 已记录 =false 未记录
    bool reqIDExist(in_addr_t srcIP, uint32_t reqID);

public:
    ~DsrReqIdRecorder();

    static DsrReqIdRecorder& getInstance()
    {
        static DsrReqIdRecorder instance;
        return instance;
    }
};

// 请求下一跳IP的模式
#define CHECK_TABLE_FIRST 1
#define SEND_REQ_ANYWAY 2

/**
 * @brief 等待其他应用线程发起的路由请求，并返回查找的路由
 */
class DsrRouteGetter {
private:
    void sendRequest(in_addr_t dstIP);

public:
    DsrRouteGetter();
    DsrRouteGetter(const DsrRouteGetter&) = delete;
    DsrRouteGetter& operator=(const DsrRouteGetter&) = delete;
    ~DsrRouteGetter();

    /// @brief 定时器，用于等待路由请求超时
    /// @param dstIP 等待的路由请求目的IP
    /// @param timeout 超时时间（秒）
    static void routeWaitTimer(in_addr_t dstIP, int timeout);

    /// @brief 请求到目的节点的下一跳节点IP，是另一重载的简单包装
    in_addr_t getNextHop(const char* dstIP, int timeout, int mode = CHECK_TABLE_FIRST);

    /// @brief 请求到目的节点的下一跳节点IP
    /// @param dstIP 目的节点IP
    /// @param timeout 超时时间（秒）
    /// @param mode CHECK_TABLE_FIRST 首先检查路由表缓存  SEND_REQ_ANYWAY 直接发起路由请求广播
    /// @return 下一跳节点的IP地址
    in_addr_t getNextHop(in_addr_t dstIP, int timeout, int mode = CHECK_TABLE_FIRST);
};

/**
 * @brief 监听其他节点发来的路由请求并处理
 */
class DsrRouteListener : public Stoppable
{
private:
    int runCount;
    int recv_sock, brd_sock;
    char* packetBuf;
    struct sockaddr_in brd_addr;

private:
    DsrRouteListener();
    DsrRouteListener(const DsrRouteListener&) = delete;
    DsrRouteListener& operator=(const DsrRouteListener&) = delete;

    void processRequestPkt(DsrRoutePacket& pkt);

    void processResponsePkt(DsrRoutePacket& pkt);

    void broadcastPkt(DsrRoutePacket& pkt);

    void unicastPkt(in_addr_t dstIP, DsrRoutePacket& pkt);

public:
    ~DsrRouteListener();

    static DsrRouteListener& getInstance()
    {
        static DsrRouteListener instance;
        return instance;
    }

    /// @brief 开始监听DSR报文的线程函数，仅能第一次创建有效
    void run();     // thread function
};

/**
 * @brief 打印路由表，仅作调试用
 */
class routeTableProbe {
public:
    void printRouteTable() {
        DsrRouteTable& table = DsrRouteTable::getInstance();
        table.printTable();
    }
};

#endif