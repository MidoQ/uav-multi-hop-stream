#ifndef _SYS_CONFIG
#define _SYS_CONFIG

// #include "utils.h"
#include <arpa/inet.h>
// #include <atomic>
// #include <chrono>
// #include <condition_variable>
#include <cstring>
#include <iostream>
// #include <map>
// #include <mutex>
// #include <sys/socket.h>
// #include <thread>
// #include <unistd.h>
#include <unordered_map>
// #include <unordered_set>
// #include <vector>
#include <fstream>
#include <string>

enum NodeType : char {
    sink = 1,
    common = 2
};

class NodeConfig {
private:
    NodeType nodeType;
    double positionX;
    double positionY;
    in_addr_t myIP;
    in_addr_t sinkNodeIP;
    char myIP_s[INET_ADDRSTRLEN];
    char sinkNodeIP_s[INET_ADDRSTRLEN];
    std::unordered_map<std::string, int> paramMap;

private:
    NodeConfig();
    NodeConfig(const NodeConfig&) = delete;
    NodeConfig& operator=(const NodeConfig&) = delete;

    int loadConfigFromFile(const char config_filename[]);

    void initParamMap();

    void assignParam(std::string& paramName, std::string& paramVal);


public:
    ~NodeConfig();

    static NodeConfig& getInstance() {
        static NodeConfig instance;
        return instance;
    }

    NodeType getNodeType() {
        return nodeType;
    }

    double getPositionX() {
        return positionX;
    }

    double getPositionY() {
        return positionY;
    }

    in_addr_t getMyIP() {
        return myIP;
    }

    void getMyIP_s(char* destBuf) {
        memcpy(destBuf, myIP_s, INET_ADDRSTRLEN);
    }

    in_addr_t getSinkNodeIP() {
        return sinkNodeIP;
    }
    
    void getSinkNodeIP_s(char* destBuf) {
        memcpy(destBuf, sinkNodeIP_s, INET_ADDRSTRLEN);
    }

    void printNodeConfig();
};

#endif