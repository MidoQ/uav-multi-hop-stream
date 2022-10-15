#include "topo.h"

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
    myIP = *p;
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
    *p = myIP;
    p++;

    char* pBegin = (char*)p;
    std::string posX_s = std::to_string(positionX);
    std::string posY_s = std::to_string(positionY);
    memset(pBegin, 0, 64);
    memcpy(pBegin, posX_s.c_str(), posX_s.size());
    memcpy(pBegin + 32, posY_s.c_str(), posY_s.size());

    return 68;
}

LiveBroadcast::LiveBroadcast()
{
    isBroadcasting = false;
    isListening = false;
    intervalSec = 5;
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

NeighborTable::NeighborTable()
{
    timeoutSec = 6;
    insertIndex = 0;

    neighbors[0].clear();
    neighbors[1].clear();

    // 超时操作线程
    auto timeoutClear = [&]() {
        while(1) {
            sleep_for(seconds(timeoutSec));

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
    *p = it->first;
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
