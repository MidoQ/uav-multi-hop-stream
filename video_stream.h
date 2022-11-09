#ifndef _VIDEO_STREAM_H
#define _VIDEO_STREAM_H

#include "dsr_route.h"
#include "sys_config.h"
#include "utils.h"
#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <mutex>
#include <thread>

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

class VideoPublisher {
private:
    VideoPublisher();
    VideoPublisher(const VideoPublisher&) = delete;
    VideoPublisher& operator=(const VideoPublisher&) = delete;

public:
    ~VideoPublisher();

    static VideoPublisher& getInstance() {
        static VideoPublisher instance;
        return instance;
    }
};

class VideoRelayer {
private:
    VideoRelayer();
    VideoRelayer(const VideoRelayer&) = delete;
    VideoRelayer& operator=(const VideoRelayer&) = delete;

public:
    ~VideoRelayer();

    static VideoRelayer& getInstance() {
        static VideoRelayer instance;
        return instance;
    }
};

#endif