#include "dsr_route.h"

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
}

DsrRouteTable::~DsrRouteTable()
{
}

int DsrRouteTable::addRouteItem(in_addr_t _srcIP, in_addr_t _nextHopIP, int _metric)
{
}

/* DsrReqIdRecorder */

DsrReqIdRecorder::DsrReqIdRecorder()
{
}

DsrReqIdRecorder::~DsrReqIdRecorder()
{
}

/* DsrRouteGetter */

DsrRouteGetter::DsrRouteGetter()
{
}

DsrRouteGetter::~DsrRouteGetter()
{
}

in_addr_t DsrRouteGetter::getNextHop(const char* srcIP, int timeout)
{
}

in_addr_t DsrRouteGetter::getNextHop(in_addr_t srcIP, int timeout)
{
}

/* DsrRouteListener */

DsrRouteListener::DsrRouteListener()
{
}

DsrRouteListener::~DsrRouteListener()
{
}

int DsrRouteListener::listenReqPacket(char* reqPacketBuf)
{
}

int DsrRouteListener::startListen()
{
}