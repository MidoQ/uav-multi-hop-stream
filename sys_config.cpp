#include "sys_config.h"

using std::cout;
using std::endl;

NodeConfig::NodeConfig()
{
    nodeType = NodeType::common;
    positionX = 100.0;
    positionY = 100.0;
    myIP = 0;
    sinkNodeIP = 0;
    strcpy(myIP_s, "000.000.000.000");
    strcpy(sinkNodeIP_s, "000.000.000.000");
    initParamMap();

    cout << "Node config initializing...\n";

    // 从配置文件中读取配置
    if (loadConfigFromFile("/home/root/programs/uav_config.txt") != 0) {
        cout << "Config load failed!\n";
        exit(1);
    }

    cout << "Node config initialize over!\n";
}

NodeConfig::~NodeConfig()
{
}

int NodeConfig::loadConfigFromFile(const char config_filename[])
{
    std::string paramLine;
    std::string paramName;
    std::string paramVal;
    std::ifstream ifile;
    in_addr tmp;

    ifile.open(config_filename, std::ios::in);

    if (!ifile.is_open()) {
        std::cerr << "File " << config_filename << " does not exist!" << endl;
        return -1;
    }

    while (getline(ifile, paramLine)) {
        size_t signPos = paramLine.find('=');
        if (signPos != std::string::npos) {
            paramName = paramLine.substr(0, signPos);
            paramVal = paramLine.substr(signPos + 1, std::string::npos);
            assignParam(paramName, paramVal);
        } else {
            std::cerr << "Config param format wrong!" << endl;
        }
    }

    ifile.close();

    inet_pton(AF_INET, myIP_s, &tmp);
    myIP = tmp.s_addr;

    inet_pton(AF_INET, sinkNodeIP_s, &tmp);
    sinkNodeIP = tmp.s_addr;

    if (myIP == sinkNodeIP) {
        nodeType = NodeType::sink;
    } else {
        nodeType = NodeType::common;
    }

    return 0;
}

void NodeConfig::initParamMap()
{
    paramMap.clear();
    paramMap["positionX"] = 0;
    paramMap["positionY"] = 1;
    paramMap["myIP_s"] = 2;
    paramMap["sinkNodeIP_s"] = 3;
}

void NodeConfig::assignParam(std::string& paramName, std::string& paramVal)
{
    auto it = paramMap.find(paramName);
    if (it == paramMap.end()) {
        cout << "Unknown parameter: " << paramName << endl;
        return;
    }

    switch (it->second)
    {
    case 0:
        positionX = std::stod(paramVal);
        break;
    case 1:
        positionY = std::stod(paramVal);
        break;
    case 2:
        memcpy(myIP_s, paramVal.c_str(), paramVal.size());
        myIP_s[paramVal.size()] = 0;
        break;
    case 3:
        memcpy(sinkNodeIP_s, paramVal.c_str(), paramVal.size());
        sinkNodeIP_s[paramVal.size()] = 0;
        break;
    default:
        break;
    }
}

void NodeConfig::printNodeConfig()
{
    cout << "[Node Config]\n";

    if (nodeType == NodeType::sink) {
        cout << "nodeType: SINK\n";
    } else {
        cout << "nodeType: COMMON\n";
    }

    cout << "positionX: " << positionX << '\n';
    cout << "positionY: " << positionY << '\n';
    cout << "myIP: " << myIP_s << "  [0x" << std::hex << myIP << "]\n";
    cout << "sinkNodeIP: " << sinkNodeIP_s << "  [0x" << std::hex << sinkNodeIP << "]\n";
}
