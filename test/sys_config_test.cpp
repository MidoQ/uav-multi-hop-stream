#include <iostream>
#include "../sys_config.h"

using namespace std;

int main(int argc, char** argv)
{
    cout << "sys_config_test running...\n";
    char tmp[INET_ADDRSTRLEN];

    NodeConfig& nodeConfig = NodeConfig::getInstance();

    cout << "External Print:\n";

    if (nodeConfig.getNodeType() == NodeType::sink) {
        cout << "nodeType: SINK\n";
    } else {
        cout << "nodeType: COMMON\n";
    }

    cout << "positionX: " << nodeConfig.getPositionX() << '\n';
    cout << "positionY: " << nodeConfig.getPositionY() << '\n';
    nodeConfig.getMyIP_s(tmp);
    cout << "myIP: " << tmp << "  [0x" << hex << nodeConfig.getMyIP() << "]\n";
    nodeConfig.getSinkNodeIP_s(tmp);
    cout << "sinkNodeIP: " << tmp << "  [0x" << hex << nodeConfig.getSinkNodeIP() << "]\n";

    cout << "Internal Print:\n";

    nodeConfig.printNodeConfig();

    return 0;
}