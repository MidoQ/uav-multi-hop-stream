/**********************************************************
 * Author: Q.M.Feng
 * Modified: 2022-10-02
 * Description: DSR路由功能
 **********************************************************/

#ifndef _DSR_ROUTE_H
#define _DSR_ROUTE_H

#include "utils.h"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <sys/socket.h>
#include <vector>

#define PORT_DSR 9190
#define DSR_REQ_HEADER_LEN 21
#define DSR_PKT_MAX_LEN 400

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

    /// @brief 将缓冲区的内容解析到当前 DsrRoutePacket 实例
    /// @param pktBuf 路由请求报文缓冲区
    void parseFromBuf(const char* pktBuf);

    /// @brief 将当前 DsrRoutePacket 实例的内容转换为字符串，以便通过网络发送
    /// @param pktBuf 路由请求报文缓冲区
    /// @return 生成的路由请求报文总长度
    int serializeToBuf(char* pktBuf);
};

/// @brief 路由表的表项值结构体
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

/// @brief 全局路由表单例（仅在DsrRouteGetter初始化时，初始化一次）
class DsrRouteTable {
    friend class DsrRouteGetter;
    friend class DsrRouteListener;

private:
    // 路由表，由srcIP映射到表项（下一跳IP、距离）
    map<in_addr_t, routeTableVal> routeTable;

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
    friend class DsrRouteGetter;
    friend class DsrRouteListener;

private:
    set<unsigned int> idHistory;

private:
    DsrReqIdRecorder();
    DsrReqIdRecorder(const DsrReqIdRecorder&) = delete;
    DsrReqIdRecorder& operator=(const DsrReqIdRecorder&) = delete;

    /// @brief 记录一个已处理的路由请求ID
    /// @param reqID 路由请求ID
    void addReqID(uint32_t reqID);

    /// @brief 判断路由请求ID是否已记录过
    /// @param reqID 路由请求ID
    /// @return =true 已记录 =false 未记录
    bool reqIDExist(uint32_t reqID);

public:
    ~DsrReqIdRecorder();

    static DsrReqIdRecorder& getInstance()
    {
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

    in_addr_t getNextHop(const char* dstIP, int timeout);
    in_addr_t getNextHop(in_addr_t dstIP, int timeout);
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