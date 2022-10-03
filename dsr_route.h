/**********************************************************
 * Author: Q.M.Feng
 * Modified: 2022-10-02
 * Description: DSR路由功能
 **********************************************************/

#ifndef _DSR_ROUTE_H
#define _DSR_ROUTE_H

#include <arpa/inet.h>
#include <iostream>
#include <map>
#include <set>
#include <sys/socket.h>
#include <vector>
#include <cstring>
#include "utils.h"

#define PORT_DSR 9190
#define DSR_REQ_HEADER_LEN 21

using namespace std;

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
    vector<in_addr_t> routeList;

public:
    DsrRoutePacket();
    DsrRoutePacket(const DsrRoutePacket& dsrRouteRequest);
    DsrRoutePacket(DsrPacketType pktType, const char* srcIP_s, const char* dstIP_s);
    ~DsrRoutePacket() = default;

    /// @brief 打印路由请求报文信息
    void printReqInfo();

    /// @brief 将缓冲区的内容解析为当前 DsrRoutePacket 实例的头部信息
    /// @param reqHeaderBuf 路由头部信息的缓冲区
    /// @return 
    int parseHeaderFromBuf(const char* reqHeaderBuf);

    /// @brief 将当前 DsrRoutePacket 实例的头部信息转换为字符串，以便通过网络发送
    /// @param reqHeaderBuf 路由头部信息的缓冲区
    /// @return 
    int serializeHeaderToBuf(char* reqHeaderBuf);

    /// @brief 将缓冲区的内容解析为当前 DsrRoutePacket 实例的路由记录
    /// @param reqRouteBuf 路由记录的缓冲区
    /// @return 
    int parseRouteFromBuf(const char* reqRouteBuf);

    /// @brief 将当前 DsrRoutePacket 实例的路由记录转换为字符串，以便通过网络广播
    /// @param reqRouteBuf 
    /// @return 路由记录的缓冲区
    int serializeRouteToBuf(char* reqRouteBuf);
};

/******************************************************
 * Name: DsrRouteTable
 * Func: 全局路由表单例（仅在DsrRouteGetter初始化时，初始化一次）
 ******************************************************/
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

class DsrRouteTable {
private:
    map<in_addr_t, routeTableVal> routeTable;

private:
    DsrRouteTable();
    DsrRouteTable(const DsrRouteTable&) = delete;
    DsrRouteTable& operator=(const DsrRouteTable&) = delete;

    int addRouteItem(in_addr_t _srcIP, in_addr_t _nextHopIP, int _metric);

public:
    ~DsrRouteTable();

    static DsrRouteTable& getInstance()
    {
        static DsrRouteTable instance;
        return instance;
    }
};

/******************************************************
 * Name: DsrReqIdRecorder
 * Func: 记录本节点处理过的路由请求ID
 ******************************************************/
class DsrReqIdRecorder {
private:
    set<unsigned int> idHistory;

private:
    DsrReqIdRecorder();
    DsrReqIdRecorder(const DsrReqIdRecorder&) = delete;
    DsrReqIdRecorder& operator=(const DsrReqIdRecorder&) = delete;

public:
    ~DsrReqIdRecorder();

    static DsrReqIdRecorder& getInstance() {
        static DsrReqIdRecorder instance;
        return instance;
    }
};

/******************************************************
 * Name: DsrRouteGetter
 * Func: 等待其他应用线程发起的路由请求，并返回查找的路由
 * Others: 单例模式
 ******************************************************/
class DsrRouteGetter {
private:
    DsrRouteGetter();
    DsrRouteGetter(const DsrRouteGetter&) = delete;
    DsrRouteGetter& operator=(const DsrRouteGetter&) = delete;

public:
    ~DsrRouteGetter();

    static DsrRouteGetter& getInstance()
    {
        static DsrRouteGetter instance;
        return instance;
    }

    in_addr_t getNextHop(const char* srcIP, int timeout);
    in_addr_t getNextHop(in_addr_t srcIP, int timeout);
};

/******************************************************
 * Name: DsrRouteListener
 * Func: 监听其他节点发来的路由请求并处理
 * Others: 单例模式
 ******************************************************/
class DsrRouteListener {
private:
    char* reqPacketBuf;

private:
    DsrRouteListener();
    DsrRouteListener(const DsrRouteListener&) = delete;
    DsrRouteListener& operator=(const DsrRouteListener&) = delete;
    int listenReqPacket(char* reqPacketBuf);

public:
    ~DsrRouteListener();

    static DsrRouteListener& getInstance()
    {
        static DsrRouteListener instance;
        return instance;
    }

    int startListen(); // TODO: 在这里创建线程，并循环监听相应端口
};

#endif