#include "sdn_cmd.h"

SdnReporter::SdnReporter()
{
    reportInterval = DEFAULT_TOPO_REPORT_SEC;
    nodeList.clear();
    mat.clear();
    posList.clear();
}

SdnReporter::~SdnReporter()
{

}

void SdnReporter::setPosListFromTopo()
{
    TopoGraph& topo = TopoGraph::getInstance();

    for (auto it = nodeList.begin(); it != nodeList.end(); it++) {
        posList[*it] = topo.getNodePos(*it);
    }
}

size_t SdnReporter::serializeTopo(char* buf)
{
    char* p = buf;
    size_t nodeCount = nodeList.size();
    
    if (nodeCount == 0) {
        cerr << "No topo information!\n";
        return 0;
    }

    *p = (char) nodeCount;
    p++;

    // 在线节点列表
    for (size_t i = 0; i < nodeCount; i++) {
        *p = (char) ((nodeList[i] & 0xFF000000) >> 24);
        p++;
    }

    // 邻接矩阵
    for (size_t i = 0; i < nodeCount; i++) {
        for (size_t j = 0; j < nodeCount; j++) {
            *p = mat[i][j];
            p++;
        }
    }

    // 除汇聚节点外，其他节点与汇聚节点的距离
    size_t len = 16 * (nodeCount - 1);
    NodeConfig& config = NodeConfig::getInstance();
    in_addr_t sinkIP = config.getSinkNodeIP();
    Position sinkPos(config.getPositionX(), config.getPositionY());
    for (size_t i = 0; i < nodeCount; i++) {
        if (nodeList[i] == sinkIP)
            continue;
        Position pos = posList[nodeList[i]];
        std::string posX_s = std::to_string(pos.x - sinkPos.x);
        std::string posY_s = std::to_string(pos.y - sinkPos.y);
        memset(p, 0, 32);
        memcpy(p, posX_s.c_str(), posX_s.size());
        memcpy(p + 16, posY_s.c_str(), posY_s.size());
        p += 32;
    }

    return 1 + nodeCount + nodeCount * nodeCount + 32 * (nodeCount - 1);
}

void SdnReporter::SdnReportHandler()
{
    int send_sock;
    int sendLen;
    struct sockaddr_in send_addr;
    char sendBuf[TOPO_PKT_MAX_LEN];
    SdnReporter& reporter = SdnReporter::getInstance();
    NodeConfig& config = NodeConfig::getInstance();
    TopoGraph& topo = TopoGraph::getInstance();

    memset(sendBuf, 0, TOPO_PKT_MAX_LEN);

    // UDP发送套接字基本设置
    send_sock = socket(PF_INET, SOCK_DGRAM, 0);

    memset(&send_addr, 0, sizeof(send_addr));
    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = config.getControllerIP();
    send_addr.sin_port = hton16(PORT_SDN);

    // 循环定期发送拓扑信息给控制器
    while(1) {
        sleep_for(seconds(reporter.getReportInterval()));
        topo.toMatrix(reporter.nodeList, reporter.mat);
        reporter.setPosListFromTopo();
        sendLen = reporter.serializeTopo(sendBuf);
        sendto(send_sock, sendBuf, sendLen, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
        cout << "Topo uploaded!\n";
    }
}

void SdnReporter::startReport()
{
    NodeConfig& config = NodeConfig::getInstance();
    if (config.getNodeType() != NodeType::sink) {
        cerr << __func__ << ": Failed! This node is not a sink node!\n";
        return;
    }
    std::thread topoReport_thread(SdnReportHandler);
    topoReport_thread.detach();
}

SdnListener::SdnListener()
{
    in_addr tmp;
    char networkIP_s[INET_ADDRSTRLEN] = "192.168.2.0";
    inet_pton(AF_INET, networkIP_s, &tmp);
    networkIP = tmp.s_addr;
}

SdnListener::~SdnListener()
{

}

SdnCmdType SdnListener::checkCmdType(char* buf)
{
    if (strstr(buf, "node") == 0) {
        return SdnCmdType::startVideo;
    } else if (strstr(buf, "End") == 0) {
        return SdnCmdType::endVideo;
    } else {
        return SdnCmdType::unknown;
    }
}

in_addr_t SdnListener::numstr2IP(char* buf)
{
    in_addr_t res = networkIP;

    while (1) {
        size_t tail = strlen(buf) - 1;
        if (buf[tail] == '\n') {
            buf[tail] = 0;
        } else {
            break;
        }
    }

    std::string str(buf);
    uint32_t ip = std::stoi(str) + 99;
    res = res & (ip << 24);

    return res;
}

void SdnListener::SdnListenHandler()
{
    int recv_sock;
    int recvLen;
    socklen_t recv_sock_len;
    in_addr_t targetNodeIP;
    struct sockaddr_in recv_addr;
    SdnCmdType cmdType;
    char recvBuf[SDN_CMD_MAX_LEN];
    SdnListener& listener = SdnListener::getInstance();

    memset(recvBuf, 0, SDN_CMD_MAX_LEN);

    // UDP接收套接字基本设置
    recv_sock = socket(PF_INET, SOCK_DGRAM, 0);

    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    recv_addr.sin_port = htons(PORT_SDN);

    if (bind(recv_sock, (struct sockaddr*)&recv_addr, sizeof(recv_addr)) == -1) {
        cerr << __func__ << " : bind() error\n";
        return;
    }

    // 监听并处理 SDN 命令
    char ipAddr_s[INET_ADDRSTRLEN];
    while(1) {
        memset(recvBuf, 0, SDN_CMD_MAX_LEN);
        memset(ipAddr_s, 0, INET_ADDRSTRLEN);

        recv_sock_len = sizeof(recv_addr);
        recvLen = recvfrom(recv_sock, recvBuf, SDN_CMD_MAX_LEN, 0, (struct sockaddr*)&recv_addr, &recv_sock_len);
        recvBuf[recvLen] = 0;
        cmdType = listener.checkCmdType(recvBuf);

        switch (cmdType) {
        case SdnCmdType::startVideo :
            targetNodeIP = listener.numstr2IP(recvBuf);
            inet_ntop(AF_INET, &targetNodeIP, ipAddr_s, INET_ADDRSTRLEN);
            cout << "SDN commmand: start video at " << ipAddr_s << endl;
            break;
        case SdnCmdType::endVideo :
            targetNodeIP = listener.numstr2IP(recvBuf);
            inet_ntop(AF_INET, &targetNodeIP, ipAddr_s, INET_ADDRSTRLEN);
            cout << "SDN commmand: end video at " << ipAddr_s << endl;
            break;
        default:
            cout << "Unknown SDN command type!\n";
            break;
        }
    }
}

void SdnListener::startListen()
{
    NodeConfig& config = NodeConfig::getInstance();
    if (config.getNodeType() != NodeType::sink) {
        cerr << __func__ << ": Failed! This node is not a sink node!\n";
        return;
    }
    std::thread topoListen_thread(SdnListenHandler);
    topoListen_thread.detach();
}
