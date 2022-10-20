#include "topo.h"

/// @brief 将节点信息及全局邻居表信息打包为邻居汇报报文（字符串）
/// @param pktBuf 字符串缓冲区
/// @return 打包后的字符串长度
static size_t serializeNeighborPkt(char* pktBuf);

/// @brief 将邻居汇报报文（字符串）解析到全局拓扑图中（仅汇聚节点）
/// @param pktBuf 
static void parseNeighborPkt(const char* pktBuf);

/* LivePacket */

LivePacket::LivePacket()
{
    myIP = 0;
    positionX = 0.0;
    positionY = 0.0;
}

LivePacket::LivePacket(const in_addr_t _myIP, const double _positionX, const double _positionY)
{
    this->myIP = _myIP;
    this->positionX = _positionX;
    this->positionY = _positionY;
}

LivePacket::~LivePacket()
{
}

void LivePacket::parseFromBuf(const char* pktBuf)
{
    uint32_t* p = (uint32_t*) pktBuf;
    myIP = ntoh32(*p);
    p++;

    char* pBegin = (char*)p;
    std::string posX_s(pBegin, 32);
    std::string posY_s(pBegin + 32, 32);
    positionX = std::stod(posX_s);
    positionY = std::stod(posY_s);
}

int LivePacket::serializeToBuf(char* pktBuf)
{
    uint32_t* p = (uint32_t*) pktBuf;
    *p = hton32(myIP);
    p++;

    char* pBegin = (char*)p;
    std::string posX_s = std::to_string(positionX);
    std::string posY_s = std::to_string(positionY);
    memset(pBegin, 0, 64);
    memcpy(pBegin, posX_s.c_str(), posX_s.size());
    memcpy(pBegin + 32, posY_s.c_str(), posY_s.size());

    return 68;
}

/* LiveBroadcast */

LiveBroadcast::LiveBroadcast()
{
    isBroadcasting = false;
    isListening = false;
    intervalSec = DEFAULT_LIVE_BRD_SEC;
}

LiveBroadcast::~LiveBroadcast()
{
}

void LiveBroadcast::pktBroadcasting()
{
    LiveBroadcast& instance = LiveBroadcast::getInstance();

    if (instance.isBroadcasting) {
        cout << "LivePacket already broadcasting!\n";
        return;
    }

    instance.isBroadcasting = true;

    int pktLen;
    int so_brd = 1;
    struct sockaddr_in brd_addr;
    char pktBuf[LIVE_PKT_MAX_LEN];

    NodeConfig& config = NodeConfig::getInstance();

    // 创建本节点的存活广播报文
    memset(pktBuf, 0, LIVE_PKT_MAX_LEN);
    LivePacket pkt(config.getMyIP(), config.getPositionX(), config.getPositionY());
    pktLen = pkt.serializeToBuf(pktBuf);

    // 设置UDP套接字为广播模式
    instance.brd_sock = socket(PF_INET, SOCK_DGRAM, 0);

    memset(&brd_addr, 0, sizeof(brd_addr));
    brd_addr.sin_family = AF_INET;
    brd_addr.sin_addr.s_addr = config.getBroadcastIP();
    brd_addr.sin_port = hton16(PORT_LIVE);

    setsockopt(instance.brd_sock, SOL_SOCKET, SO_BROADCAST, (void*)&so_brd, sizeof(so_brd));

    // TODO: 开始时首先随机延时片刻

    // 周期性广播
    while(1) {
        // 连续发送两次
        sendto(instance.brd_sock, pktBuf, pktLen, 0, (struct sockaddr*)&brd_addr, sizeof(brd_addr));
        sleep_for(nanoseconds(20000));
        sendto(instance.brd_sock, pktBuf, pktLen, 0, (struct sockaddr*)&brd_addr, sizeof(brd_addr));
        
        // 间隔片刻
        // TODO: 再加上一个随机的延迟
        sleep_for(seconds(instance.intervalSec));
    }
}

void LiveBroadcast::pktListening()
{
    LiveBroadcast& instance = LiveBroadcast::getInstance();

    if (instance.isListening) {
        cout << "LivePackek already listening!\n";
        return;
    }

    instance.isListening = true;

    int recvLen;
    struct sockaddr_in recv_addr;
    in_addr_t myIP;
    char pktBuf[LIVE_PKT_MAX_LEN];

    memset(pktBuf, 0, LIVE_PKT_MAX_LEN);

    // 设置UDP监听地址
    instance.recv_sock = socket(PF_INET, SOCK_DGRAM, 0);

    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = hton32(INADDR_ANY);
    recv_addr.sin_port = hton16(PORT_LIVE);

    if (bind(instance.recv_sock, (struct sockaddr*)&recv_addr, sizeof(recv_addr)) == -1) {
        cout << '[' << __func__ << "]: bind error!\n";
    }

    NodeConfig& config = NodeConfig::getInstance();
    myIP = config.getMyIP();

    NeighborTable& neibTable = NeighborTable::getInstance();

    // 监听并处理 LivePacket
    while(1) {
        // 收取报文，解析后加入邻居表
        recvLen = recvfrom(instance.recv_sock, pktBuf, LIVE_PKT_MAX_LEN, 0, NULL, 0);
        LivePacket pkt;
        pkt.parseFromBuf(pktBuf);
        if (pkt.getIP() == myIP)
            continue;
        neibTable.addNeighbor(pkt.getIP(), pkt.getPositionX(), pkt.getPositionY());
    }
}

/* NeighborTable */

NeighborTable::NeighborTable()
{
    timeoutSec = DEFAULT_LIVE_TIMEOUT_SEC;
    insertIndex = 0;

    neighbors[0].clear();
    neighbors[1].clear();

    // 超时操作线程
    auto timeoutClear = [&]() {
        while(1) {
            sleep_for(seconds(timeoutSec));

            NodeConfig& config = NodeConfig::getInstance();
            TopoGraph& topoGraph = TopoGraph::getInstance();

            size_t clearIndex = insertIndex == 0 ? 1 : 0;
            std::unique_lock<std::mutex> lock(mtx4ClearMap);
            neighbors[clearIndex].clear();
            lock.unlock();

            insertIndex = clearIndex;
        }
    };

    std::thread timeout_thread(timeoutClear);
    // std::thread::id timeoutThreadId = timeout_thread.get_id();
    timeout_thread.detach();
}

NeighborTable::~NeighborTable()
{
    // TODO: 是否需要显式取消timeoutClear线程？

}

bool NeighborTable::contains(in_addr_t nodeIP)
{
    bool res;

    std::unique_lock<std::mutex> lock1(mtx4InsertMap);
    std::unique_lock<std::mutex> lock2(mtx4ClearMap);

    res = neighbors[0].find(nodeIP) != neighbors[0].end() 
        || neighbors[1].find(nodeIP) != neighbors[1].end();

    lock2.unlock();
    lock1.unlock();

    return res;
}

size_t NeighborTable::getNeighborCount()
{
    size_t count;

    std::unique_lock<std::mutex> lock1(mtx4InsertMap);
    std::unique_lock<std::mutex> lock2(mtx4ClearMap);

    size_t clearIndex = insertIndex == 0 ? 1 : 0;
    count = neighbors[insertIndex].size();
    for (auto it = neighbors[clearIndex].begin(); it != neighbors[clearIndex].end(); it++) {
        if (neighbors[insertIndex].find(it->first) == neighbors[insertIndex].end()) {
            count++;
        }
    }

    lock2.unlock();
    lock1.unlock();

    return count;
}

void NeighborTable::addNeighbor(in_addr_t nodeIP, double positionX, double positionY)
{
    std::unique_lock<std::mutex> lock(mtx4InsertMap);
    neighbors[insertIndex].insert(std::pair<in_addr_t, Position>(nodeIP, Position(positionX, positionY)));
    lock.unlock();
}

size_t NeighborTable::neighborInfo2Buf(char* buf, std::unordered_map<in_addr_t, Position>::iterator it)
{
    uint32_t* p = (uint32_t*) buf;
    *p = hton32(it->first);
    p++;

    char* pBegin = (char*)p;
    std::string posX_s = std::to_string(it->second.x);
    std::string posY_s = std::to_string(it->second.y);
    memset(pBegin, 0, 64);
    memcpy(pBegin, posX_s.c_str(), posX_s.size());
    memcpy(pBegin + 32, posY_s.c_str(), posY_s.size());

    return 68;
}

size_t NeighborTable::neighbors2Buf(char* buf)
{
    std::unordered_map<in_addr_t, Position> merged;

    std::unique_lock<std::mutex> lock1(mtx4InsertMap);
    std::unique_lock<std::mutex> lock2(mtx4ClearMap);

    size_t clearIndex = insertIndex == 0 ? 1 : 0;
    for (auto it = neighbors[clearIndex].begin(); it != neighbors[clearIndex].end(); it++) {
        merged[it->first] = it->second;
    }
    for (auto it = neighbors[insertIndex].begin(); it != neighbors[insertIndex].end(); it++) {
        merged[it->first] = it->second;
    }

    lock2.unlock();
    lock1.unlock();

    size_t len;
    for (auto it = merged.begin(); it != merged.end(); it++) {
        len = neighborInfo2Buf(buf, it);
        buf += len;
    }

    return merged.size();
}

TopoGraph::TopoGraph()
{
    nodeCount = 0;
    timeoutSec = DEFAULT_NEIB_TIMOUT_SEC;
    graph.clear();

    std::thread timeout_thread(timeoutHandler);
    timeout_thread.detach();
}

TopoGraph::~TopoGraph()
{

}

void TopoGraph::timeoutHandler()
{
    TopoGraph& topoGraph = TopoGraph::getInstance();

    struct timeval timeoutVal;
    while (1) {
        size_t sec = topoGraph.timeoutSec;
        timeoutVal.tv_sec = sec;
        timeoutVal.tv_usec = 0;
        select(0, NULL, NULL, NULL, &timeoutVal); // 利用select进行延时

        std::unique_lock<std::mutex> lock(topoGraph.mtx4timeoutRec);
        std_clock timeToCheck = std::chrono::steady_clock::now();
        for (auto it = topoGraph.timeoutRecord.begin(); it != topoGraph.timeoutRecord.end();) {
            std::chrono::duration<double, std::milli> diff = timeToCheck - (*it).timeStamp;
            if (diff.count() > sec * 1000) {
                topoGraph.removeLink((*it).sIP, (*it).dIP);
                it = topoGraph.timeoutRecord.erase(it);
            } else {
                it++;
                // break;
            }
        }

        lock.unlock();
    }
}

void TopoGraph::addDirectLink(in_addr_t sIP, in_addr_t dIP)
{
    auto it = graph.find(sIP);
    
    if (it != graph.end()) {
        it->second.insert(dIP);
    } else {
        std::set<in_addr_t> tmp;
        tmp.clear();
        graph[sIP] = tmp;
        graph[sIP].insert(dIP);
        nodeCount++;
    }
}

void TopoGraph::removeDirectLink(in_addr_t sIP, in_addr_t dIP)
{
    auto it = graph.find(sIP);

    if (it != graph.end()) {
        auto itForVal = it->second.find(dIP);
        if (itForVal != it->second.end()) {
            it->second.erase(itForVal);
            if (it->second.empty()) {
                graph.erase(it);
                nodeCount--;
                // cout << "Link from " << sIP << " to " << dIP << " is removed!\n";
            }
        }
    }
}

void TopoGraph::removeLink(in_addr_t sIP, in_addr_t dIP)
{
    std::unique_lock<std::mutex> lock(mtx4Gragh);
    removeDirectLink(sIP, dIP);
    removeDirectLink(dIP, sIP);
    lock.unlock();
}

void TopoGraph::addLink(in_addr_t sIP, in_addr_t dIP)
{
    std::unique_lock<std::mutex> lock(mtx4Gragh);

    addDirectLink(sIP, dIP);
    addDirectLink(dIP, sIP);

    std::unique_lock<std::mutex> lock2(mtx4timeoutRec);
    updateTimeoutRecord(sIP, dIP);
    lock2.unlock();

    lock.unlock();
}

void TopoGraph::toMatrix(std::vector<in_addr_t>& nodeList, std::vector<std::vector<char>>& mat)
{
    nodeList.assign(nodeCount, 0);
    mat.assign(nodeCount, std::vector<char>(nodeCount, 0));

    std::unordered_map<in_addr_t, size_t> mapIp2Index;

    std::unique_lock<std::mutex> lock(mtx4Gragh);

    size_t k = 0;
    for (auto it = graph.begin(); it != graph.end(); it++) {
        nodeList[k] = it->first;
        mapIp2Index[it->first] = k;
        k++;
    }

    size_t i, j;
    for (auto it = graph.begin(); it != graph.end(); it++) {
        i = mapIp2Index[it->first];
        for (auto itForVal = it->second.begin(); itForVal != it->second.end(); itForVal++) {
            j = mapIp2Index[*itForVal];
            mat[i][j] = 1;
        }
    }

    lock.unlock();
}

void TopoGraph::updatePos(in_addr_t nodeIP, double posX, double posY)
{
    posList[nodeIP] = Position(posX, posY);
}

void TopoGraph::updateTimeoutRecord(in_addr_t sIP, in_addr_t dIP)
{
    // TODO：可优化。时间戳可简化为一个浮点数，在更新时原地修改。
    // 在程序启动时，在config中记录一个初始time_point，之后所有的时间戳与其作差记录为浮点数，单位一致即可。
    bool isExist = false;
    std_clock timeNow = std::chrono::steady_clock::now();
    for (auto it = timeoutRecord.begin(); it != timeoutRecord.end();) {
        if ((sIP == (*it).sIP && dIP == (*it).dIP) || (sIP == (*it).dIP && dIP == (*it).sIP)) {
            isExist = true;
            it = timeoutRecord.erase(it);
            UndiLinkWithTime link(sIP, dIP, timeNow);
            timeoutRecord.insert(link);
            break;
        } else {
            it++;
        }
    }

    if (!isExist) {
        UndiLinkWithTime link(sIP, dIP, timeNow);
        timeoutRecord.insert(link);
    }
}

size_t serializeNeighborPkt(char* pktBuf)
{
    uint32_t* pNeibCount = (uint32_t*)pktBuf;
    char* p = pktBuf + 4;
    size_t totalLen = 4, len, neibCount;

    NodeConfig& config = NodeConfig::getInstance();
    LivePacket myInfo = LivePacket(config.getMyIP(), config.getPositionX(), config.getPositionY());
    len = myInfo.serializeToBuf(p);
    p += len;
    totalLen += len;

    NeighborTable& table = NeighborTable::getInstance();
    neibCount = table.neighbors2Buf(p);
    totalLen += neibCount * 68;

    *pNeibCount = hton32(neibCount);

    return totalLen;
}

void parseNeighborPkt(const char* pktBuf)
{
    TopoGraph& topoGraph = TopoGraph::getInstance();

    uint32_t* pNeibCount = (uint32_t*)pktBuf;
    size_t neibCount = ntoh32(*pNeibCount);
    pNeibCount++;
    char* p = (char*) pNeibCount;

    LivePacket srcInfo;
    in_addr_t srcIP;
    srcInfo.parseFromBuf(p);
    p += 68;
    srcIP = srcInfo.getIP();

    for (size_t i = 0; i < neibCount; i++) {
        LivePacket neibInfo;
        neibInfo.parseFromBuf(p);
        p += 68;
        topoGraph.addLink(srcIP, neibInfo.getIP());
        topoGraph.updatePos(neibInfo.getIP(), neibInfo.getPositionX(), neibInfo.getPositionY());
    }
}

/* NeighborReporter */

NeighborReporter::NeighborReporter()
{
    intervalSec = DEFAULT_NEIB_REPORT_SEC;
}

NeighborReporter::~NeighborReporter()
{

}

void NeighborReporter::neighborReport()
{
    bool routeFail = false;
    char sendBuf[NEIB_PKT_MAX_LEN];
    in_addr_t nextHopIP, sinkNodeIP;
    NeighborReporter& reporter = NeighborReporter::getInstance();
    NodeConfig& config = NodeConfig::getInstance();
    DsrRouteGetter routeGetter;

    memset(sendBuf, 0, NEIB_PKT_MAX_LEN);

    sinkNodeIP = config.getSinkNodeIP();

    while (1) {
        sleep_for(seconds(reporter.intervalSec));

        // 获取下一跳IP
        if (config.getNodeType() == NodeType::sink) {
            nextHopIP = config.getMyIP();
        } else {
            try {
                if (routeFail) {
                    nextHopIP = routeGetter.getNextHop(sinkNodeIP, 3, SEND_REQ_ANYWAY);
                } else {
                    nextHopIP = routeGetter.getNextHop(sinkNodeIP, 3);
                }
                routeFail = false;
            } catch (const char* msg) {
                routeFail = true;
                cerr << __func__ << " : Fail to get next hop!\n";
                cerr << msg << endl;
                if (strcmp(msg, "DestinationUnreachable") == 0) {
                    cerr << "No route to sink node!\n";
                }
                continue;
            }
        }

        // 与下一跳节点连接
        reporter.send_sock = socket(PF_INET, SOCK_STREAM, 0);

        memset(&(reporter.send_addr), 0, sizeof(reporter.send_addr));
        reporter.send_addr.sin_family = AF_INET;
        reporter.send_addr.sin_port = htons(PORT_NEIB_REPORT);
        reporter.send_addr.sin_addr.s_addr = nextHopIP;
        if (connect(reporter.send_sock, (struct sockaddr*)&(reporter.send_addr), sizeof(reporter.send_addr)) == -1) {
            routeFail = true;   // 连接失败，下次强制发起路由请求广播
            cerr << __func__ << " : Fail to connect to next hop!\n";
            continue;
        }

        // 将邻居表序列化并发送
        int len = serializeNeighborPkt(sendBuf);
        if (len > NEIB_PKT_MAX_LEN) {
            cerr << "NeighborPacket (" << len << " bytes) too long!\n";
            continue;
        }
        send(reporter.send_sock, sendBuf, len, 0);

        sleep_for(milliseconds(20));
        close(reporter.send_sock);
    }
}

/* NeighborListener */

NeighborListener::NeighborListener()
{
    int option = 1;
    socklen_t optlen = sizeof(option);

    // 监听套接字基本设置
    listen_sock = socket(PF_INET, SOCK_STREAM, 0);

    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (void*)&option, optlen);

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_addr.sin_port = htons(PORT_NEIB_REPORT);

    if (bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) == -1) {
        cerr << __func__ << " : bind() error\n";
        exit(1);
    }
}

NeighborListener::~NeighborListener()
{

}

std::mutex mtx4printNeibPkt;
void NeighborListener::printNeighborPkt(char* pktBuf)
{
    size_t neibCount;
    uint32_t* pNeibCount = (uint32_t*)pktBuf;
    neibCount = ntoh32(*pNeibCount);

    char* p = pktBuf + 4;
    LivePacket srcInfo;
    srcInfo.parseFromBuf(p);
    p += 68;

    std::unique_lock<std::mutex> lock(mtx4printNeibPkt);

    in_addr tmp;
    in_addr_t ipAddr = srcInfo.getIP();
    char ipAddr_s[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ipAddr, ipAddr_s, INET_ADDRSTRLEN);
    cout << "----------- NeighborTable Recved -----------\n";
    cout << "Src: " << ipAddr_s << "\t[" << srcInfo.getPositionX() << ", " << srcInfo.getPositionY() << "]\n";
    cout << "Neighbor Count: " << neibCount << '\n';

    for (size_t i = 0; i < neibCount; i++) {
        LivePacket neibInfo;
        neibInfo.parseFromBuf(p);
        p += 68;
        ipAddr = neibInfo.getIP();
        inet_ntop(AF_INET, &ipAddr, ipAddr_s, INET_ADDRSTRLEN);
        cout << "Neib" << i << ": " << ipAddr_s
             << "\t[" << neibInfo.getPositionX() << ", " << neibInfo.getPositionY() << "]\n";
    }

    cout << "--------------------------------------------\n" << endl;

    lock.unlock();
}

void NeighborListener::relayNeighborPkt(const char* pktBuf, size_t len)
{
    bool routeFail = false;
    in_addr_t nextHopIP;
    NodeConfig& config = NodeConfig::getInstance();
    NeighborListener& listener = NeighborListener::getInstance();
    in_addr_t sinkNodeIP = config.getSinkNodeIP();
    DsrRouteGetter routeGetter;

    for (size_t i = 0; i < 5; ++i) {
        sleep_for(seconds(2));

        // 获取下一跳IP
        try {
            if (routeFail) {
                nextHopIP = routeGetter.getNextHop(sinkNodeIP, 3, SEND_REQ_ANYWAY);
            } else {
                nextHopIP = routeGetter.getNextHop(sinkNodeIP, 3);
            }
            routeFail = false;
        } catch (const char* msg) {
            routeFail = true;
            cerr << __func__ << " : Fail to get next hop!\n";
            if (strcmp(msg, "DestinationUnreachable") == 0) {
                cerr << "No route to sink node!\n";
            }
            continue;
        }

        // 与下一跳节点连接
        listener.send_sock = socket(PF_INET, SOCK_STREAM, 0);

        memset(&(listener.send_addr), 0, sizeof(listener.send_addr));
        listener.send_addr.sin_family = AF_INET;
        listener.send_addr.sin_port = htons(PORT_NEIB_REPORT);
        listener.send_addr.sin_addr.s_addr = nextHopIP;
        if (connect(listener.send_sock, (struct sockaddr*)&(listener.send_addr), sizeof(listener.send_addr)) == -1) {
            routeFail = true;   // 连接失败，下次强制发起路由请求广播
            cerr << __func__ << " : Fail to connect to next hop!\n";
            continue;
        }

        // 直接转发邻居表接收缓冲区的内容
        send(listener.send_sock, pktBuf, len, 0);

        sleep_for(milliseconds(20));
        close(listener.send_sock);
        break;
    }
}

void NeighborListener::neighborListen()
{
    int clnt_sock;
    socklen_t clnt_addr_size;
    struct sockaddr_in clnt_addr;

    NeighborListener& listener = NeighborListener::getInstance();

    if (listen(listener.listen_sock, 10) == -1) {
        cerr << __func__ << " : listen() error";
        exit(1);
    }

    while (1) {
        clnt_addr_size = sizeof(clnt_addr);
        // cout << __func__ << ": Listening for neighbor packet...\n";
        clnt_sock = accept(listener.listen_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        // cout << __func__ << ": New client connected!\n";

        std::thread clnt_thread(NeighborListener::clntHandler, clnt_sock);
        clnt_thread.detach();
    }
}

void NeighborListener::clntHandler(int clnt_sock)
{
    size_t recvLen, totalLen = 0;
    char* pRecv = nullptr;
    char recvBuf[NEIB_PKT_MAX_LEN];
    NodeConfig& config = NodeConfig::getInstance();

    memset(recvBuf, 0, NEIB_PKT_MAX_LEN);
    pRecv = recvBuf;

    while (1) {
        // 接收邻居包头
        recvLen = 0;
        while (recvLen < NEIB_PKT_HEADER_LEN) {
            int len = recv(clnt_sock, pRecv, NEIB_PKT_HEADER_LEN - recvLen, 0);
            pRecv += len;
            recvLen += len;
        }
        totalLen += recvLen;

        // 判断邻居表长度
        uint32_t* pCount = (uint32_t*)recvBuf;
        int neibCount = ntoh32(*pCount);
        int neibTableLen = neibCount * 68;
        
        // 接收邻居表
        recvLen = 0;
        while (recvLen < neibTableLen) {
            int len = recv(clnt_sock, pRecv, neibTableLen - recvLen, 0);
            pRecv += len;
            recvLen += len;
        }
        totalLen += recvLen;

        #ifdef DEBUG_PRINT_NEIB_PKT
        printNeighborPkt(recvBuf);
        #endif

        if (config.getNodeType() == NodeType::sink) {
            parseNeighborPkt(recvBuf);
        } else {
            relayNeighborPkt(recvBuf, totalLen);
        }
    }
}

#ifdef TOPO_TEST

void NeighborTableProbe::printNeighborTable() {
    NeighborTable& table = NeighborTable::getInstance();

    std::unordered_map<in_addr_t, Position> merged;

    std::unique_lock<std::mutex> lock1(table.mtx4InsertMap);
    std::unique_lock<std::mutex> lock2(table.mtx4ClearMap);

    size_t clearIndex = table.insertIndex == 0 ? 1 : 0;
    for (auto it = table.neighbors[clearIndex].begin(); it != table.neighbors[clearIndex].end(); it++) {
        merged[it->first] = it->second;
    }
    for (auto it = table.neighbors[table.insertIndex].begin(); it != table.neighbors[table.insertIndex].end(); it++) {
        merged[it->first] = it->second;
    }

    lock2.unlock();
    lock1.unlock();

    in_addr tmp;
    char ipStr[INET_ADDRSTRLEN];
    memset(ipStr, 0, INET_ADDRSTRLEN);

    cout << "Neighbor count: " << table.getNeighborCount() << '\n';
    cout << "--------------- [Neigbors] ---------------\n"
         << "IP\t\tposX\t\tposY\n";
    for (auto it = merged.begin(); it != merged.end(); it++) {
        inet_ntop(AF_INET, &(it->first), ipStr, INET_ADDRSTRLEN);
        cout << ipStr << '\t'
             << std::fixed << std::setprecision(3)
             << std::setw(8) << std::internal << it->second.x << '\t' 
             << std::setw(8) << std::internal << it->second.y << '\n';
    }
    cout << "------------------------------------------\n" << endl;
}

#endif
