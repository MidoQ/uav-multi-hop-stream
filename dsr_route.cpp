#include "dsr_route.h"

/* Global variables */
std::atomic<uint32_t> myReqID = {0};
char myIP_s[] = "192.168.2.101"; // debug temp
char broadcast_IP_s[] = "192.168.2.255";    // debug temp

enum RouteRespondState : char {
    waiting = 1,    // requester 正在等待路由回复
    arrived = 2,    // 路由回复已到达，但 requester 尚未处理完
    timeout = 3     // 路由回复等待超时
};

/// @brief 用于路由请求线程、定时器线程、路由监听线程之间的同步
struct RouteNotifier {
    std::mutex mtx;
    std::condition_variable cond;
    std::unordered_map<in_addr_t, RouteRespondState> respondStates;
    RouteNotifier() { respondStates.clear(); }
} routeNotifier;

/* DsrRoutePacket */

DsrRoutePacket::DsrRoutePacket()
{
    type = DsrPacketType::request;
    srcIP = 0;
    dstIP = 0;
    hop = 0;
    reqID = 0;
    routeListLength = 0;
}

DsrRoutePacket::DsrRoutePacket(const DsrRoutePacket& dsrRoutePacket)
{
    this->type = dsrRoutePacket.type;
    this->srcIP = dsrRoutePacket.srcIP;
    this->dstIP = dsrRoutePacket.dstIP;
    this->hop = dsrRoutePacket.hop;
    this->reqID = dsrRoutePacket.reqID;
    this->routeListLength = dsrRoutePacket.routeListLength;
    this->routeList = dsrRoutePacket.routeList;
}

DsrRoutePacket::DsrRoutePacket(DsrPacketType pktType, const char* srcIP_s, const char* dstIP_s)
{
    in_addr tmp;

    if (inet_pton(AF_INET, srcIP_s, &tmp) == 1) {
        srcIP = tmp.s_addr;
    } else {
        srcIP = 0;
    }

    if (inet_pton(AF_INET, dstIP_s, &tmp) == 1) {
        dstIP = tmp.s_addr;
    } else {
        dstIP = 0;
    }

    type = pktType;
    hop = 0;
    reqID = 0;
    routeListLength = 0;
}

DsrRoutePacket::DsrRoutePacket(DsrPacketType pktType, in_addr_t srcIP, in_addr_t dstIP)
{
    this->srcIP = srcIP;
    this->dstIP = dstIP;
    type = pktType;
    hop = 0;
    reqID = 0;
    routeListLength = 0;
}

void DsrRoutePacket::printReqInfo()
{
    char srcIP_s[INET_ADDRSTRLEN];
    char dstIP_s[INET_ADDRSTRLEN];
    char nodeIP_s[INET_ADDRSTRLEN];
    in_addr tmp;

    memset(srcIP_s, 0, INET_ADDRSTRLEN);
    memset(dstIP_s, 0, INET_ADDRSTRLEN);

    tmp.s_addr = srcIP;
    inet_ntop(AF_INET, &tmp, srcIP_s, INET_ADDRSTRLEN);

    tmp.s_addr = dstIP;
    inet_ntop(AF_INET, &tmp, dstIP_s, INET_ADDRSTRLEN);

    cout << "===================== DSR Packet =====================\n";

    if (type == DsrPacketType::request) {
        cout << "type: request\n";
    } else if (type == DsrPacketType::response) {
        cout << "type: response\n";
    } else {
        cout << "Unknown DSR packet type!\n";
    }

    cout << "srcIP: " << srcIP_s << "    dstIP: " << dstIP_s << endl
         << "current hop: " << hop << "    reqID: " << reqID << "    len: " << routeListLength << endl;

    cout << "Route:\n";
    for (size_t i = 0; i < routeList.size(); i++) {
        tmp.s_addr = routeList[i];
        inet_ntop(AF_INET, &tmp, nodeIP_s, INET_ADDRSTRLEN);
        if (i != 0) {
            cout << " ---> ";
        }
        cout << nodeIP_s;
    }

    cout << "\n=======================================================\n" << endl;
}

void DsrRoutePacket::parseFromBuf(const char* pktBuf)
{
    type = (DsrPacketType) pktBuf[0];

    uint32_t* cur = (uint32_t*)(pktBuf + 1);

    srcIP = ntoh32(*cur);
    cur++;

    dstIP = ntoh32(*cur);
    cur++;

    hop = ntoh32(*cur);
    cur++;

    reqID = ntoh32(*cur);
    cur++;

    routeListLength = ntoh32(*cur);
    cur++;

    for (size_t i = 0; i < routeListLength; i++) {
        routeList.push_back(ntoh32(*cur));
        cur++;
    }
}

int DsrRoutePacket::serializeToBuf(char* pktBuf)
{
    pktBuf[0] = (char)type;

    uint32_t* cur = (uint32_t*)(pktBuf + 1);

    *cur = hton32(srcIP);
    cur++;

    *cur = hton32(dstIP);
    cur++;

    *cur = hton32(hop);
    cur++;

    *cur = hton32(reqID);
    cur++;

    *cur = hton32(routeListLength);
    cur++;

    for (size_t i = 0; i < routeListLength; i++) {
        *cur = hton32(routeList[i]);
        cur++;
    }

    return DSR_REQ_HEADER_LEN + routeListLength * 4;
}

/* DsrRouteTable */

DsrRouteTable::DsrRouteTable()
{
    routeTable.clear();
}

DsrRouteTable::~DsrRouteTable()
{
}

bool DsrRouteTable::updateRouteItem(in_addr_t _dstIP, in_addr_t _nextHopIP, int _metric)
{
    std::map<in_addr_t, routeTableVal>::iterator it = routeTable.find(_dstIP);

    if (it == routeTable.end()) {
        // 无此表项，插入新表项即可
        routeTable.insert(std::pair<in_addr_t, routeTableVal>(_dstIP, routeTableVal(_nextHopIP, _metric)));
        return true;
    } else if (_metric < it->second.metric) {
        // 有此表项，但距离更小时才插入
        routeTable[_dstIP] = routeTableVal(_nextHopIP, _metric);
        return true;
    }
    return false;
}

bool DsrRouteTable::findRouteItem(in_addr_t dstIP, routeTableVal& item)
{
    std::map<in_addr_t, routeTableVal>::iterator it = routeTable.find(dstIP);
    
    if (it == routeTable.end()) {
        return false;
    }

    item.nextHopIP = it->second.nextHopIP;
    item.metric = it->second.metric;
    return true;
}

/* DsrReqIdRecorder */

DsrReqIdRecorder::DsrReqIdRecorder()
{
    idHistory.clear();
}

DsrReqIdRecorder::~DsrReqIdRecorder()
{
}

void DsrReqIdRecorder::addReqID(uint32_t reqID) {
    idHistory.insert(reqID);
}

bool DsrReqIdRecorder::reqIDExist(uint32_t reqID) {
    return idHistory.find((unsigned int)reqID) != idHistory.end();
}

/* DsrRouteGetter */

DsrRouteGetter::DsrRouteGetter()
{
}

DsrRouteGetter::~DsrRouteGetter()
{
}

in_addr_t DsrRouteGetter::getNextHop(const char* dstIP, int timeout)
{
    in_addr tmp;
    inet_pton(AF_INET, dstIP, &tmp);
    return getNextHop(tmp.s_addr, timeout);
}

in_addr_t DsrRouteGetter::getNextHop(in_addr_t dstIP, int timeout)
{
    DsrRouteTable& table = DsrRouteTable::getInstance();
    routeTableVal tableItem;

    if (table.findRouteItem(dstIP, tableItem)) {
        // 找到已缓存的表项，直接返回下一跳地址
        return tableItem.nextHopIP;
    }
    else {
        // 未找到已缓存的表项，发起路由请求，并等待监听线程的通知

        // 广播路由请求
        sendRequest(dstIP);

        // 等待定时器线程 or 路由监听线程通知
        std::unique_lock<std::mutex> lock(routeNotifier.mtx);

        // 查找等待列表中是否已经存在相同dstIP的请求
        auto states = &routeNotifier.respondStates;
        auto it = states->find(dstIP);

        if (it != states->end()) {
            // 等待列表中已存在该dstIP的请求
            if (it->second == RouteRespondState::arrived) {
                // 查找路由表
                routeTableVal item;
                DsrRouteTable& routeTable = DsrRouteTable::getInstance();
                if (routeTable.findRouteItem(dstIP, item))
                    return item.nextHopIP;
                else
                    throw("Destination Unreachable");
            }
            else if (it->second == RouteRespondState::timeout) {
                throw("Destination Unreachable");
            }
        }
        else {
            // 等待列表中不存在该dstIP的请求
            states->insert(std::pair<in_addr_t, RouteRespondState>(dstIP, RouteRespondState::waiting));
        }

        // 新建定时器线程
        std::thread timer_thread(routeWaitTimer, dstIP, timeout);
        timer_thread.detach();

        // 等待被唤醒
        while (states->find(dstIP) == states->end() || (*states)[dstIP] == RouteRespondState::waiting) {
            routeNotifier.cond.wait(lock);
        }

        // 查看本dstIP的请求是否已被删除（自己可能不是第一个被唤醒的线程）
        it = states->find(dstIP);
        if (it != states->end()) {
            // 请求尚未被删除，则删除请求
            states->erase(dstIP);

            if (it->second == RouteRespondState::timeout) {
                lock.unlock();
                throw("Destination Unreachable");
            }
        }

        lock.unlock();

        // 查找路由表（包含请求已被删除、请求未删除且状态为arrived的情况
        routeTableVal item;
        DsrRouteTable& routeTable = DsrRouteTable::getInstance();
        if (routeTable.findRouteItem(dstIP, item))
            return item.nextHopIP;
        else
            throw("Destination Unreachable");
    }
}


void DsrRouteGetter::sendRequest(in_addr_t dstIP)
{
    in_addr_t myIP;     // debug temp
    in_addr tmp;
    int send_sock;
    struct sockaddr_in send_addr;
    char send_buf[DSR_PKT_GENERAL_LEN];

    int so_brd = 1;

    // 设置UDP套接字，广播模式
    send_sock = socket(PF_INET, SOCK_DGRAM, 0);

    memset(&send_addr, 0, sizeof(send_addr));
    send_addr.sin_family = AF_INET;
    inet_pton(AF_INET, broadcast_IP_s, &tmp);
    send_addr.sin_addr.s_addr = tmp.s_addr;
    send_addr.sin_port = hton16(PORT_DSR);

    setsockopt(send_sock, SOL_SOCKET, SO_BROADCAST, (void*)&so_brd, sizeof(so_brd));

    // 创建DSR报文
    inet_pton(AF_INET, myIP_s, &tmp);
    myIP = tmp.s_addr;
    DsrRoutePacket pkt(DsrPacketType::request, myIP, dstIP);

    pkt.setHop(1);      // 准备发送的报文，跳数已经变为到接收者的跳数
    pkt.setReqID(myReqID++);
    pkt.attachRoute(myIP);  // debug temp

    memset(send_buf, 0, DSR_PKT_GENERAL_LEN);
    int send_len = pkt.serializeToBuf(send_buf);

    // 发送2次DSR广播报文
    sendto(send_sock, send_buf, send_len, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
    sleep_for(nanoseconds(20000));   // wait for 20us
    sendto(send_sock, send_buf, send_len, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
}

void DsrRouteGetter::routeWaitTimer(in_addr_t dstIP, int timeout)
{
    delay(timeout);

    std::unique_lock<std::mutex> lock(routeNotifier.mtx);

    auto it = routeNotifier.respondStates.find(dstIP);
    if (it != routeNotifier.respondStates.end() && it->second == RouteRespondState::waiting) {
        it->second = RouteRespondState::timeout;
        routeNotifier.cond.notify_all();
        lock.unlock();
    }
}

/* DsrRouteListener */

DsrRouteListener::DsrRouteListener()
{
    struct sockaddr_in recv_addr;
    in_addr tmp;
    int so_brd = 1;

    packetBuf = new char[DSR_PKT_MAX_LEN + 1];
    memset(packetBuf, 0, DSR_PKT_MAX_LEN + 1);

    // 设置UDP监听地址
    recv_sock = socket(PF_INET, SOCK_DGRAM, 0);

    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = hton32(INADDR_ANY);
    recv_addr.sin_port = hton16(PORT_DSR);

    if (bind(recv_sock, (struct sockaddr*)&recv_addr, sizeof(recv_addr)) == -1) {
        cout << '[' << __func__ << "]: bind error!\n";
    }

    // 设置UDP广播地址
    brd_sock = socket(PF_INET, SOCK_DGRAM, 0);

    memset(&brd_addr, 0, sizeof(brd_addr));
    brd_addr.sin_family = AF_INET;
    inet_pton(AF_INET, broadcast_IP_s, &tmp);
    brd_addr.sin_addr.s_addr = tmp.s_addr;
    brd_addr.sin_port = hton16(PORT_DSR);

    setsockopt(brd_sock, SOL_SOCKET, SO_BROADCAST, (void*)&so_brd, sizeof(so_brd));
}

DsrRouteListener::~DsrRouteListener()
{
    close(recv_sock);
    delete[] packetBuf;
}

int DsrRouteListener::listenPacket()
{
    int recvLen;
    DsrRoutePacket packetInfo;

    while (1) {
        recvLen = recvfrom(recv_sock, packetBuf, DSR_PKT_MAX_LEN, 0, NULL, 0);
        packetInfo.parseFromBuf(packetBuf);

        // 处理报文
        if (packetInfo.getType() == DsrPacketType::request) {
            processRequestPkt(packetInfo);
        } else if (packetInfo.getType() == DsrPacketType::response) {
            processResponsePkt(packetInfo);
        } else {
            cout << '[' << __func__ << "] Unknown DSR packet type!\n";
        }
    }

    return 0;
}

void DsrRouteListener::startListen()
{
}

void DsrRouteListener::processRequestPkt(DsrRoutePacket& pkt)
{
    // debug temp
    in_addr tmp;
    inet_pton(AF_INET, myIP_s, &tmp);
    in_addr_t myIP = tmp.s_addr;
    // debug temp

    // 更新路由表
    DsrRouteTable& table = DsrRouteTable::getInstance();
    std::vector<in_addr_t>& list = pkt.getRouteList();
    in_addr_t myNextHopToSrc = list[list.size() - 1];
    table.updateRouteItem(pkt.getSrcIP(), myNextHopToSrc, pkt.getHop());

    // 处理路由请求报文
    if (pkt.getDstIP() != myIP) {
        // 本节点不是目的节点
        pkt.attachRoute(myIP);
        pkt.increaseHop();
        broadcastPkt(pkt);
    } else {
        // 本节点是目的节点
        DsrRoutePacket responsePkt(pkt);
        responsePkt.setType(DsrPacketType::response);
        responsePkt.attachRoute(myIP);
        responsePkt.reverseRoute();
        responsePkt.setHop(1);
        unicastPkt(responsePkt.getRouteList().at(1), pkt);
    }
}

void DsrRouteListener::processResponsePkt(DsrRoutePacket& pkt)
{
    // debug temp
    in_addr tmp;
    inet_pton(AF_INET, myIP_s, &tmp);
    in_addr_t myIP = tmp.s_addr;
    // debug temp

    // 更新路由表
    DsrRouteTable& table = DsrRouteTable::getInstance();
    std::vector<in_addr_t>& list = pkt.getRouteList();
    in_addr_t myNextHopToDst = list[pkt.getHop() - 1];
    table.updateRouteItem(pkt.getDstIP(), myNextHopToDst, pkt.getHop());

    if (pkt.getSrcIP() != myIP) {
        // 本节点不是路由请求者
        pkt.increaseHop();
        unicastPkt(list[pkt.getHop()], pkt);
    }
    else {
        // 本节点是路由请求者
        std::unique_lock<std::mutex> lock(routeNotifier.mtx);

        // 仅当等待列表中还存在对于dstIP的请求，且状态为waiting时，唤醒所有请求线程
        auto states = &routeNotifier.respondStates;
        auto it = states->find(pkt.getDstIP());
        if (it != states->end() && it->second == RouteRespondState::waiting) {
            it->second = RouteRespondState::arrived;
            routeNotifier.cond.notify_all();
            lock.unlock();
        }
    }
}

void DsrRouteListener::broadcastPkt(DsrRoutePacket& pkt)
{
    int len = pkt.expectedBufLen();
    char* send_buf = new char[len + 1];
    pkt.serializeToBuf(send_buf);

    sendto(brd_sock, send_buf, len, 0, (struct sockaddr*)&brd_addr, sizeof(brd_addr));
    sleep_for(nanoseconds(20000));
    sendto(brd_sock, send_buf, len, 0, (struct sockaddr*)&brd_addr, sizeof(brd_addr));

    delete[] send_buf;
}

void DsrRouteListener::unicastPkt(in_addr_t dstIP, DsrRoutePacket& pkt)
{
    int send_sock;
    struct sockaddr_in send_addr;

    // 设置UDP套接字
    send_sock = socket(PF_INET, SOCK_DGRAM, 0);

    memset(&send_addr, 0, sizeof(send_addr));
    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = dstIP;
    send_addr.sin_port = hton16(PORT_DSR);

    int len = pkt.expectedBufLen();
    char* send_buf = new char[len + 1];
    pkt.serializeToBuf(send_buf);

    sendto(send_sock, send_buf, len, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
    sleep_for(nanoseconds(20000));
    sendto(send_sock, send_buf, len, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));

    delete[] send_buf;
}