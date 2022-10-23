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

class VideoCapturer {
private:
    VideoCapturer();
    VideoCapturer(const VideoCapturer&) = delete;
    VideoCapturer& operator=(const VideoCapturer&) = delete;

public:
    ~VideoCapturer();

    static VideoCapturer& getInstance() {
        static VideoCapturer instance;
        return instance;
    }
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

#endif