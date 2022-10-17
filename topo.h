#ifndef _TOPO_H
#define _TOPO_H

#include "sys_config.h"
#include "dsr_route.h"
#include "utils.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <iomanip>

#define PORT_LIVE 9290
#define PORT_NEIB_REPORT 9390
#define LIVE_PKT_MAX_LEN 100
#define NEIB_PKT_MAX_LEN 800
#define NEIB_PKT_HEADER_LEN 72

using std::cerr;
using std::cout;
using std::endl;
using namespace std::this_thread; // sleep_for, sleep_until
using namespace std::chrono; // nanoseconds, system_clock, seconds

/**
 * @brief 存活广播报文
 */
class LivePacket {
private:
    in_addr_t myIP;
    double positionX;
    double positionY;

public:
    LivePacket();
    LivePacket(const in_addr_t _myIP, const double _positionX, const double _positionY);
    ~LivePacket();

    in_addr_t getIP() {
        return myIP;
    }

    double getPositionX() {
        return positionX;
    }

    double getPositionY() {
        return positionY;
    }

    /// @brief 将缓冲区的内容解析到当前 LivePacket 实例
    /// @param pktBuf 存活广播报文缓冲区
    void parseFromBuf(const char* pktBuf);

    /// @brief 将当前 LivePacket 实例的内容转换为字符串，以便通过网络发送
    /// @param pktBuf 存活广播报文缓冲区
    /// @return 生成的存活广播报文总长度
    int serializeToBuf(char* pktBuf);
};

/**
 * @brief 存活广播（邻居发现），即报告自身的存活，和监听其他节点的存活
 */
class LiveBroadcast {
private:
    int brd_sock, recv_sock;
    bool isBroadcasting;
    bool isListening;
    int intervalSec;

private:
    LiveBroadcast();
    LiveBroadcast(const LiveBroadcast&) = delete;
    LiveBroadcast& operator=(const LiveBroadcast&) = delete;

public:
    ~LiveBroadcast();

    static LiveBroadcast& getInstance()
    {
        static LiveBroadcast instance;
        return instance;
    }

    /// @brief 线程函数，定期广播LiveBroadcast
    static void pktBroadcasting(); // thread function

    /// @brief 线程函数，持续监听LiveBroadcast
    static void pktListening(); // thread function

    /// @brief 设置定期广播LiveBroadcast的间隔
    /// @param _intervalSec 间隔秒数
    void setInterval(int _intervalSec) {
        this->intervalSec = _intervalSec;
    }
};

/**
 * @brief 节点坐标
 */
typedef struct Position {
    double x;
    double y;
    Position() : x(0.0), y(0.0) {}
    Position(double px, double py) : x(px), y(py) {}
} Position;

/**
 * @brief 全局邻居表单例
 * @details 初始化后，定期删除超时表项。删除以周期为单位，超时后删除上一周期的所有表项。
 *          即一个表项存在的间隔可能为 timeoutSec ~ 2*timeoutSec
 */
class NeighborTable {
#ifdef TOPO_TEST
    friend class NeighborTableProbe;
#endif
private:
    int timeoutSec; // 表项超时时间，默认为6秒
    std::unordered_map<in_addr_t, Position> neighbors[2];
    std::atomic<size_t> insertIndex;     // 由该变量标识的表作插入，另一个表等待超时删除
    std::mutex mtx4ClearMap, mtx4InsertMap;

private:
    NeighborTable();
    NeighborTable(const NeighborTable&) = delete;
    NeighborTable& operator=(const NeighborTable&) = delete;

    bool contains(in_addr_t nodeIP);

    /// @brief 将邻居表中的一项转为字符串
    /// @param buf 字符串缓冲区指针
    /// @param it 表项迭代器
    /// @return 生成的字符串长度
    size_t neighborInfo2Buf(char* buf, std::unordered_map<in_addr_t, Position>::iterator it);

public:
    ~NeighborTable();

    static NeighborTable& getInstance() {
        static NeighborTable instance;
        return instance;
    }

    /// @brief 获取邻居个数
    /// @return 邻居个数
    size_t getNeighborCount();

    /// @brief 设置表项超时删除的时间
    /// @param seconds 超时秒数
    void setTimeout(int seconds) {
        timeoutSec = seconds;
    }

    /// @brief 插入一个新的表项
    /// @param nodeIP 节点IP
    /// @param positionX 节点x坐标
    /// @param positionY 节点y坐标
    void addNeighbor(in_addr_t nodeIP, double positionX, double positionY);

    /// @brief 将邻居表转换为字符串，以便通过网络发送，不包含邻居个数和发送者行
    /// @param buf 字符串缓冲区指针，注意是从第一个邻居表项处开始
    /// @return 邻居个数
    size_t neighbors2Buf(char* buf);
};

/**
 * @brief 全局拓扑图单例（仅汇聚节点）
 */
class TopoGraph {
private:
    TopoGraph();
    TopoGraph(const TopoGraph&) = delete;
    TopoGraph& operator=(const TopoGraph&) = delete;

public:
    ~TopoGraph();

    static TopoGraph& getInstance() {
        static TopoGraph instance;
        return instance;
    }
};

/// @brief 将节点信息及全局邻居表信息打包为邻居汇报报文（字符串）
/// @param pktBuf 字符串缓冲区
/// @return 打包后的字符串长度
static size_t serializeNeighborPkt(char* pktBuf);

/// @brief 将邻居汇报报文（字符串）解析到全局拓扑图中（仅汇聚节点）
/// @param pktBuf 
static void parseNeighborPkt(const char* pktBuf);

/**
 * @brief 邻居汇报报文发送
 */
class NeighborReporter {
private:
    int intervalSec; // 邻居表汇报的间隔，默认为5秒
    int send_sock;
    struct sockaddr_in send_addr;

private:
    NeighborReporter();
    NeighborReporter(const NeighborReporter&) = delete;
    NeighborReporter& operator=(const NeighborReporter&) = delete;

public:
    ~NeighborReporter();

    static NeighborReporter& getInstance() {
        static NeighborReporter instance;
        return instance;
    }

    /// @brief 线程函数，定期向汇聚节点报告邻居表信息
    static void neighborReport();
};

/**
 * @brief 邻居汇报报文接收
 */
class NeighborListener {
private:
    int listen_sock, send_sock;
    struct sockaddr_in listen_addr, send_addr;
private:
    NeighborListener();
    NeighborListener(const NeighborListener&) = delete;
    NeighborListener& operator=(const NeighborListener&) = delete;

    static void printNeighborPkt(char* pktBuf);

    static void relayNeighborPkt(const char* pktBuf, size_t len);

    /// @brief 线程函数，接受到客户端连接后，为其新建一个线程进行数据接收和处理
    /// @param clnt_sock 客户端套接字
    static void clntHandler(int clnt_sock);

public:
    ~NeighborListener();

    static NeighborListener& getInstance() {
        static NeighborListener instance;
        return instance;
    }

    /// @brief 线程函数，监听邻居表信息并转发（普通节点）或解析（汇聚节点）
    static void neighborListen();
};

#ifdef TOPO_TEST
class NeighborTableProbe {
public:
    void printNeighborTable();
};
#endif

#endif