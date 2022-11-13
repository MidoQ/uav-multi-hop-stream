#ifndef _VIDEO_STREAM_H
#define _VIDEO_STREAM_H

#include "dsr_route.h"
#include "sys_config.h"
#include "utils.h"
#include "basic_thread.h"
#include <iostream>
#include <iomanip>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <mutex>
#include <thread>
#include <string>

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

/**
 * @brief （汇聚节点）向控制器发送视频流
 */
class VideoUploader
{
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
    size_t i = 0, exeTime = 0;
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
    VideoRelayer(char pullAddr[]);
    ~VideoRelayer();

    /// @brief 线程函数
    void run();
};

#endif