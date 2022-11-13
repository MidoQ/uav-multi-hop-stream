#ifndef _TOPO_H
#define _TOPO_H

#include "dsr_route.h"
#include "sys_config.h"
#include "utils.h"
#include "basic_thread.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <ratio>
#include <set>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <vector>

#define PORT_LIVE 9290
#define PORT_NEIB_REPORT 9390
#define LIVE_PKT_MAX_LEN 100
#define NEIB_PKT_MAX_LEN 800
#define NEIB_PKT_HEADER_LEN 72
#define DEFAULT_LIVE_BRD_SEC 3
#define DEFAULT_LIVE_TIMEOUT_SEC 5
#define DEFAULT_NEIB_REPORT_SEC 5
#define DEFAULT_NEIB_TIMOUT_SEC 7

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
 * @brief 存活广播（邻居发现），报告自身的存活
 */
class LiveBroadcast : public Stoppable
{
private:
    int runCount;
    int brd_sock;
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

    /// @brief 线程函数，定期广播 LivePacket
    void run(); // thread function

    /// @brief 设置定期广播 LivePacket 的间隔
    /// @param _intervalSec 间隔秒数
    void setInterval(int _intervalSec) {
        this->intervalSec = _intervalSec;
    }
};

/**
 * @brief 存活广播（邻居发现）的监听，即监听其他节点的存活
 */
class LiveListen : public Stoppable
{
private:
    int runCount;
    int recv_sock;

private:
    LiveListen();
    LiveListen(const LiveListen&) = delete;
    LiveListen& operator=(const LiveListen&) = delete;

public:
    ~LiveListen();

    static LiveListen& getInstance()
    {
        static LiveListen instance;
        return instance;
    }

    /// @brief 线程函数，持续监听 LivePacket
    void run(); // thread function
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
#ifdef DEBUG_PRINT_TOPO
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
 * @brief 表示一条无向（双向）的连接
 */
typedef struct UndiLink {
    in_addr_t sIP;
    in_addr_t dIP;
    UndiLink(in_addr_t _sIP, in_addr_t _dIP) : sIP(_sIP), dIP(_dIP) {}
    bool operator<(const UndiLink& A) const {
        return sIP == A.sIP ? dIP < A.dIP : sIP < A.sIP;
    }
} UndiLink;

/**
 * @brief 表示一条带时间戳的无向（双向）的连接
 */
typedef std::chrono::time_point<std::chrono::steady_clock> std_clock;
typedef struct UndiLinkWithTime {
    in_addr_t sIP;
    in_addr_t dIP;
    std_clock timeStamp;
    UndiLinkWithTime(in_addr_t _sIP, in_addr_t _dIP, std_clock _timeStamp)
        : sIP(_sIP)
        , dIP(_dIP)
        , timeStamp(_timeStamp)
    {
    }
    UndiLinkWithTime() = delete;
    bool operator<(const UndiLinkWithTime& A) const {
        return timeStamp < A.timeStamp;
    }
} UndiLinkWithTime;

/**
 * @brief 全局拓扑图单例（仅汇聚节点）
 */
class TopoGraph {
    friend class NeighborTable;

private:
    size_t nodeCount;
    std::atomic<size_t> timeoutSec;
    std::mutex mtx4Gragh;
    std::mutex mtx4timeoutRec;
    std::mutex mtx4TimeoutCount;
    std::mutex mtx4PosList;
    std::map<in_addr_t, std::set<in_addr_t>> graph; // 邻接表形式的拓扑图，key为节点IP，value为其相连的邻居节点序列
    std::set<UndiLinkWithTime> timeoutRecord;
    std::map<in_addr_t, Position> posList; // 各节点的位置列表，此表只增改，不删除（此表不设锁）

private:
    TopoGraph();
    TopoGraph(const TopoGraph&) = delete;
    TopoGraph& operator=(const TopoGraph&) = delete;

    static void timeoutHandler();

    void addDirectLink(in_addr_t sIP, in_addr_t dIP);

    void removeDirectLink(in_addr_t sIP, in_addr_t dIP);

public:
    ~TopoGraph();

    static TopoGraph& getInstance() {
        static TopoGraph instance;
        return instance;
    }

    size_t getNodeCount() {
        return nodeCount;
    }

    void setTimeoutSec(size_t _timeoutSec) {
        timeoutSec = _timeoutSec;
    }

    /// @brief 添加一条sIP与dIP之间的双向连接
    void addLink(in_addr_t sIP, in_addr_t dIP);

    /// @brief 移除一条sIP与dIP之间的双向连接
    void removeLink(in_addr_t sIP, in_addr_t dIP);

    /// @brief 更新（或插入）sIP与dIP之间连接的时间戳
    void updateTimeoutRecord(in_addr_t sIP, in_addr_t dIP);

    /// @brief 更新（或插入）节点nodeIP的位置坐标
    void updatePos(in_addr_t nodeIP, double posX, double posY);

    /// @brief 将拓扑图转换为邻接矩阵形式
    /// @param nodeList 保存节点IP地址列表
    /// @param mat 保存邻接矩阵，其行列表示的节点与nodeList中顺序一致
    void toMatrix(std::vector<in_addr_t>& nodeList, std::vector<std::vector<char>>& mat);

    /// @brief 获取某节点的坐标
    /// @param nodeIP 节点IP地址
    /// @return 若节点存在，返回其坐标；若不存在，返回全0坐标
    Position getNodePos(in_addr_t nodeIP);
};

/**
 * @brief 邻居汇报报文发送
 */
class NeighborReporter : public Stoppable
{
private:
    int runCount;
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
    void run();
};

/**
 * @brief 邻居汇报报文接收
 */
class NeighborListener : public Stoppable
{
private:
    int runCount;
    int listen_sock, send_sock;
    struct sockaddr_in listen_addr, send_addr;
private:
    NeighborListener();
    NeighborListener(const NeighborListener&) = delete;
    NeighborListener& operator=(const NeighborListener&) = delete;

    void printNeighborPkt(char* pktBuf);

    void relayNeighborPkt(const char* pktBuf, size_t len);

    /// @brief 线程函数，接受到客户端连接后，为其新建一个线程进行数据接收和处理
    /// @param clnt_sock 客户端套接字
    void clntHandler(int clnt_sock);

public:
    ~NeighborListener();

    static NeighborListener& getInstance() {
        static NeighborListener instance;
        return instance;
    }

    /// @brief 线程函数，监听邻居表信息并转发（普通节点）或解析（汇聚节点）
    void run();
};

#ifdef DEBUG_PRINT_TOPO
class NeighborTableProbe {
public:
    void printNeighborTable();
};
#endif

#endif