#ifndef _VIDEO_STREAM_H
#define _VIDEO_STREAM_H

#include "basic_thread.h"
#include "dsr_route.h"
#include "sys_config.h"
#include "utils.h"
#include <arpa/inet.h>
#include <atomic>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
};

#define PORT_VIDEO 8554
#define PORT_VIDEO_TRANS_PKT 8600
#define VT_PKT_MAX_LEN 64
#define VS_URL_MAX_LEN 128

enum class VideoTransCmd : char {
    unknown = 0,
    start = 1,      // 要求开始传输
    ready = 2,      // 节点已准备好
    stop = 4,       // 要求停止传输
    lost = 8        // 丢失与传输节点的连接
};

class VideoTransPacket
{
private:
    VideoTransCmd cmd;      // 命令类型
    in_addr_t src;          // 发送命令的节点
    in_addr_t dst;          // 接收命令的节点（下一跳）
    in_addr_t requester;    // 发起视频流传输请求的节点（一般是汇聚节点）
    in_addr_t capturer;     // 采集视频并响应视频流传输请求的节点

public:
    VideoTransPacket();
    VideoTransPacket(VideoTransCmd cmd,
        in_addr_t src, in_addr_t dst, in_addr_t requester, in_addr_t capturer);
    ~VideoTransPacket();

    VideoTransCmd getCmd() { return cmd; }
    void setCmd(VideoTransCmd cmd) { this->cmd = cmd; }

    in_addr_t getSrc() { return src; }
    void setSrc(in_addr_t src) { this->src = src; }

    in_addr_t getDst() { return dst; }
    void setDst(in_addr_t dst) { this->dst = dst; }

    in_addr_t getRequester() { return requester; }
    void setRequester(in_addr_t requester) { this->requester = requester; }

    in_addr_t getCapturer() { return capturer; }
    void setCapturer(in_addr_t capturer) { this->capturer = capturer; }

    /// @brief 
    /// @param pktBuf 
    void parseFromBuf(const char* pktBuf);

    /// @brief 
    /// @param pktBuf 
    /// @return 
    int serializeToBuf(char* pktBuf);

    /// @brief 
    void printPktInfo();
};

/**
 * @brief （汇聚节点）向控制器发送视频流
 */
class VideoUploader {
private:
    VideoUploader();
    VideoUploader(const VideoUploader&) = delete;
    VideoUploader& operator=(const VideoUploader&) = delete;

public:
    ~VideoUploader();

    static VideoUploader& getInstance() {
        static VideoUploader instance;
        return instance;
    }
};

#define H264FPS 25

/**
 * @brief 从摄像头采集视频流，并发布到本节点的rtsp-server
 */
class VideoPublisher : public Stoppable
{
private:
    int runCount = 0;
    int ret = 0;
    bool ioIsSet = false;
    int vsIndex = -1;
    int frameIndex = 0;
    char inFilename[256] = { 0 };   //输入URL
    char outFilename[256] = { 0 };  //输出URL
    char errmsg[1024] = { 0 };

    AVFormatContext* ifmtCtx = nullptr;
    AVFormatContext* ofmtCtx = nullptr;
    AVCodecContext* pRawCodecCtx = nullptr;
    AVCodecContext* pH264CodecCtx = nullptr;
    AVPacket* pPkt = nullptr;
    AVFrame *pFrameRaw = nullptr, *pFrameCopy = nullptr, *pFrameYUV = nullptr;
    SwsContext* pImgConvertCtx = nullptr;

private:
    VideoPublisher();
    VideoPublisher(const VideoPublisher&) = delete;
    VideoPublisher& operator=(const VideoPublisher&) = delete;

    int findVideoStreamIndex(AVFormatContext* ifmtCtx);

    AVFormatContext* openInputCtx(char inFilename[]);

    AVCodecContext* openInputCodecCtx(AVFormatContext* ifmtCtx);

    AVCodecContext* openH264CodexCtx(AVPixelFormat codeType, int width, int height, int fps);

    AVFormatContext* openOutputCtx(char outFilename[], AVFormatContext* ifmtCtx, AVCodecContext* pCodecCtx);

public:
    ~VideoPublisher();

    static VideoPublisher& getInstance() {
        static VideoPublisher instance;
        return instance;
    }

    int setIOName(const char deviceName[], const char publishUrl[]);

    /// @brief 线程函数
    void run();
};

//'1': Use H.264 Bitstream Filter
#define USE_H264BSF 0

/**
 * @brief 拉取邻居节点的视频流，并发布到本节点的rtsp-server
 */
class VideoRelayer : public Stoppable
{
private:
    int runCount = 0;
    int ret = 0;
    int videoIndex = -1;
    int frameIndex = 0;
    bool firstPtsIsSet = false;
    bool ioIsSet = false;
    int64_t firstPts = 0, firstDts = 0;
    AVOutputFormat* ofmt = NULL;
    AVFormatContext *ifmtCtx = NULL, *ofmtCtx = NULL;
    AVPacket pkt;
    AVCodec *pInCodec, *pOutCodec;
    AVCodecContext *pInCodecCtx, *pOutCodecCtx;

    char inFilename[256] = { 0 };
    char outFilename[256] = { 0 };
    char errmsg[1024] = { 0 };

private:
    VideoRelayer(const VideoRelayer&) = delete;
    VideoRelayer& operator=(const VideoRelayer&) = delete;

    int findVideoStreamIndex(AVFormatContext* ifmtCtx);

    AVFormatContext* openInputCtx(char inFilename[]);

    AVFormatContext* openOutputCtx(char outFilename[], AVFormatContext* ifmtCtx);

public:
    VideoRelayer();
    VideoRelayer(const char pullUrl[], const char publishUrl[]);
    ~VideoRelayer();

    // int setIOName(const char pullUrl[], const char publishUrl[]);

    /// @brief 线程函数
    void run();
};

/**
 * @brief 控制视频流的拉取、中继等操作
 */
class VideoTransCtrler : public Stoppable
{
private:
    int runCount;
    std::unordered_map<in_addr_t, VideoRelayer*> relayerList;   // 采集节点IP与Relayer实例的映射
    // std::unordered_map<in_addr_t, std::thread*> relayerThreadList;

private:
    VideoTransCtrler();
    VideoTransCtrler(const VideoTransCtrler&) = delete;
    VideoTransCtrler& operator=(const VideoTransCtrler&) = delete;

    void packetReact(VideoTransPacket& pkt);

    void addRelayer(in_addr_t capturerIP, in_addr_t pullIP);

    void deleteRelayer(in_addr_t capturerIP);

public:
    ~VideoTransCtrler();

    static VideoTransCtrler& getInstance() {
        static VideoTransCtrler instance;
        return instance;
    }

    /// @brief 线程函数
    void run();
};

/// @brief 生成发布视频流的 URL 地址 rtsp://<IP>:8554/vs<num>
/// @details e.g. rtsp://192.168.2.101/vs01
/// @param capturerIP 决定<num>
/// @param publishIP 决定<IP>
/// @param urlBuf 
void generateUrl(in_addr_t capturerIP, in_addr_t publishIP, char urlBuf[]);

#endif