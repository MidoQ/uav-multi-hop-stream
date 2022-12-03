#include "video_stream.h"

/* PublishingList */

class PublishingList
{
private:
    std::atomic<bool> isEmpty;
    std::mutex mtx4pubList;
    std::unordered_set<std::string> list;   // 本地已推流URL列表

public:
    PublishingList()
    {
        isEmpty = true;
    }

    ~PublishingList() {}

    bool empty() { return isEmpty; }

    void add(std::string publishUrl);
    void add(char* publishUrl);
    void add(in_addr_t capturerIP, in_addr_t publisherIP);

    void erase(std::string publishUrl);
    void erase(char* publishUrl);
    void erase(in_addr_t capturerIP, in_addr_t publisherIP);

    bool find(std::string publishUrl);
    bool find(char* publishUrl);
    bool find(in_addr_t capturerIP, in_addr_t publisherIP);

    std::string fetch() { return *(list.begin()); }

} publishingList, lostList;

void PublishingList::add(std::string publishUrl)
{
    cout << "Adding publishUrl: " << publishUrl << '\n';
    std::unique_lock<std::mutex> lock(mtx4pubList);
    list.insert(publishUrl);
    isEmpty = false;
    lock.unlock();
}

void PublishingList::add(char* publishUrl)
{
    add(std::string(publishUrl));
}

void PublishingList::add(in_addr_t capturerIP, in_addr_t publisherIP)
{
    std::string publishUrl = generateUrl(capturerIP, publisherIP);
    add(publishUrl);
}

void PublishingList::erase(std::string publishUrl)
{
    #ifdef DEBUG_PRINT_VS_CONTROL
    cout << "Erasing publishUrl: " << publishUrl << '\n';
    #endif
    std::unique_lock<std::mutex> lock(mtx4pubList);
    auto it = list.find(publishUrl);
    if (it != list.end()) {
        list.erase(it);
    } else {
        cout << "URL [" << publishUrl << "] NOT found in publishingList.\n";
    }
    if (list.empty()) {
        isEmpty = true;
    }
    lock.unlock();
}

void PublishingList::erase(char* publishUrl)
{
    erase(std::string(publishUrl));
}

void PublishingList::erase(in_addr_t capturerIP, in_addr_t publisherIP)
{
    std::string publishUrl = generateUrl(capturerIP, publisherIP);
    erase(publishUrl);
}

bool PublishingList::find(std::string publishUrl)
{
    #ifdef DEBUG_PRINT_VS_CONTROL
    cout << "Finding publishUrl: " << publishUrl << '\n';
    #endif
    std::unique_lock<std::mutex> lock(mtx4pubList);
    return list.find(publishUrl) != list.end();
}

bool PublishingList::find(char* publishUrl)
{
    return find(std::string(publishUrl));
}

bool PublishingList::find(in_addr_t capturerIP, in_addr_t publisherIP)
{
    std::string publishUrl = generateUrl(capturerIP, publisherIP);
    return find(publishUrl);
}

/* PacketSendQueue */

class PacketSendQueue : public Stoppable
{
private:
    int runCount;
    int send_sock;
    int sendLen;
    struct sockaddr_in send_addr;
    char sendBuf[VT_PKT_MAX_LEN];

    std::mutex mtx;
    std::atomic<bool> isEmpty;
    std::condition_variable cond;
    std::queue<VideoTransPacket> q;

private:
    void sendVTPakcet(VideoTransPacket& pkt);

    VideoTransPacket pop();

public:
    PacketSendQueue()
    {
        runCount = 0;
        isEmpty = true;
    }

    ~PacketSendQueue() {}

    bool empty() { return isEmpty; }

    void stop()
    {
        exitSignal.set_value();
        cond.notify_all();
    }

    void waitForPacket()
    {
        std::unique_lock<std::mutex> lock(mtx);
        while (stopRequested() == false && isEmpty) {
            cond.wait(lock);
        }
    }

    void push(VideoTransPacket& pkt);

    void run();

} packetSendQueue;

void PacketSendQueue::sendVTPakcet(VideoTransPacket& pkt)
{
    memset(sendBuf, 0, VT_PKT_MAX_LEN);
    sendLen = pkt.serializeToBuf(sendBuf);

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = pkt.getDst();
    send_addr.sin_port = hton16(PORT_VIDEO_TRANS_PKT);

    sendto(send_sock, sendBuf, sendLen, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
}

VideoTransPacket PacketSendQueue::pop()
{
    if (isEmpty) {
        throw("EmptyQueue");
    }
    std::unique_lock<std::mutex> lock(mtx);
    VideoTransPacket pkt = q.front();
    q.pop();
    if (q.empty()) {
        isEmpty = true;
    }
    lock.unlock();
    return pkt;
}

void PacketSendQueue::push(VideoTransPacket& pkt)
{
    std::unique_lock<std::mutex> lock(mtx);
    q.push(pkt);
    isEmpty = false;
    cond.notify_all();
    lock.unlock();
}

void PacketSendQueue::run()
{
    if (runCount == 0) {
        runCount++;
    } else {
        cout << "PacketSendQueue thread exited: a thread is already running.\n";
        return;
    }

    memset(sendBuf, 0, VT_PKT_MAX_LEN);

    send_sock = socket(PF_INET, SOCK_DGRAM, 0);

    while (stopRequested() == false) {
        waitForPacket();

        while (!isEmpty) {
            VideoTransPacket pkt = pop();
            #ifdef DEBUG_PRINT_VS_CONTROL
            cout << "\n\n*************** Packet to send **********************";
            pkt.printPktInfo();
            #endif
            sendVTPakcet(pkt);
        }
    }

    runCount--;
    cout << "PacketSendQueue::run() exit!\n";
}

/* PacketRecvQueue */

class PacketRecvQueue : public Stoppable
{
private:
    int runCount;
    std::atomic<bool> isEmpty;
    std::mutex mtx;
    std::condition_variable cond;
    std::queue<VideoTransPacket> q;

private:
    void push(VideoTransPacket& pkt);

public:
    PacketRecvQueue()
    {
        isEmpty = true;
        runCount = 0;
    }

    ~PacketRecvQueue()
    {
    }

    bool empty() { return isEmpty; }

    /// @brief 
    /// @return 队列中现存的 packet 数量
    size_t waitForPacket()
    {
        std::unique_lock<std::mutex> lock(mtx);
        while (stopRequested() == false && isEmpty) {
            cond.wait(lock);
        }
        return q.size();
    }

    VideoTransPacket pop();

    void run();

} packetRecvQueue;

void PacketRecvQueue::push(VideoTransPacket& pkt)
{
    std::unique_lock<std::mutex> lock(mtx);
    q.push(pkt);
    isEmpty = false;
    cond.notify_all();
    lock.unlock();
}

VideoTransPacket PacketRecvQueue::pop()
{
    if (isEmpty) {
        throw("EmptyQueue");
    }
    std::unique_lock<std::mutex> lock(mtx);
    VideoTransPacket pkt = q.front();
    q.pop();
    if (q.empty()) {
        isEmpty = true;
    }
    lock.unlock();
    return pkt;
}

void PacketRecvQueue::run()
{
    if (runCount == 0) {
        runCount++;
    } else {
        cout << "PacketRecvQueue thread exited: a thread is already running.\n";
        return;
    }

    int recv_sock;
    int recvLen;
    socklen_t recv_sock_len;
    struct sockaddr_in recv_addr;
    char recvBuf[VT_PKT_MAX_LEN];

    memset(recvBuf, 0, VT_PKT_MAX_LEN);

    // UDP接收套接字基本设置
    recv_sock = socket(PF_INET, SOCK_DGRAM, 0);

    struct timeval recvTimeout; // recvfrom()的超时时间
    recvTimeout.tv_sec = 3;
    recvTimeout.tv_usec = 0;
    if (setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout)) == -1) {
        cerr << __func__ << "setsockopt() failed!\n";
    }

    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    recv_addr.sin_port = htons(PORT_VIDEO_TRANS_PKT);

    if (bind(recv_sock, (struct sockaddr*)&recv_addr, sizeof(recv_addr)) == -1) {
        cerr << __func__ << " : bind() error\n";
        return;
    }

    // 循环接收
    while (stopRequested() == false) {
        memset(recvBuf, 0, VT_PKT_MAX_LEN);

        recv_sock_len = sizeof(recv_addr);
        recvLen = recvfrom(recv_sock, recvBuf, VT_PKT_MAX_LEN, 0, (struct sockaddr*)&recv_addr, &recv_sock_len);

        if (recvLen <= 0) {
            if (errno == EAGAIN) {
                // cout << "No VideoTransPacket recved.\n";
            } else {
                cerr << "Error accured when recving VideoTransPacket!\n";
            }
            continue;
        }

        VideoTransPacket pkt;
        pkt.parseFromBuf(recvBuf);

        push(pkt);

        #ifdef DEBUG_PRINT_VS_CONTROL
        cout << "\n\n*************** Packet recved **********************";
        pkt.printPktInfo();
        #endif
    }

    cond.notify_all(); // 唤醒所有可能在等待的线程
    runCount--;
    cout << "PacketRecvQueue::run() exit!\n";
}

/* VideoTransPacket */

VideoTransPacket::VideoTransPacket()
{
    cmd = VideoTransCmd::unknown;
    src = 0;
    dst = 0;
    requester = 0;
    capturer = 0;
}

VideoTransPacket::VideoTransPacket(VideoTransCmd cmd,
    in_addr_t src, in_addr_t dst, in_addr_t requester, in_addr_t capturer)
{
    this->cmd = cmd;
    this->src = src;
    this->dst = dst;
    this->requester = requester;
    this->capturer = capturer;
}

VideoTransPacket::~VideoTransPacket()
{
}

void VideoTransPacket::parseFromBuf(const char* pktBuf)
{
    cmd = (VideoTransCmd)pktBuf[0];

    uint32_t* cur = (uint32_t*)(pktBuf + 1);

    src = ntoh32(*cur);
    cur++;

    dst = ntoh32(*cur);
    cur++;

    requester = ntoh32(*cur);
    cur++;

    capturer = ntoh32(*cur);
    cur++;
}

int VideoTransPacket::serializeToBuf(char* pktBuf)
{
    pktBuf[0] = (char)cmd;

    uint32_t* cur = (uint32_t*)(pktBuf + 1);

    *cur = hton32(src);
    cur++;

    *cur = hton32(dst);
    cur++;

    *cur = hton32(requester);
    cur++;

    *cur = hton32(capturer);
    cur++;

    return 4 * 4 + 1;
}

void VideoTransPacket::printPktInfo()
{
    char cmd_s[32] = { 0 };
    char src_s[INET_ADDRSTRLEN];
    char dst_s[INET_ADDRSTRLEN];
    char requester_s[INET_ADDRSTRLEN];
    char capturer_s[INET_ADDRSTRLEN];

    switch (this->cmd) {
        case VideoTransCmd::unknown:
            strcpy(cmd_s, "unknown");
            break;
        case VideoTransCmd::start:
            strcpy(cmd_s, "start");
            break;
        case VideoTransCmd::ready:
            strcpy(cmd_s, "ready");
            break;
        case VideoTransCmd::stop:
            strcpy(cmd_s, "stop");
            break;
        case VideoTransCmd::lost:
            strcpy(cmd_s, "lost");
            break;
        default:
            break;
    }

    inet_ntop(AF_INET, &(this->src), src_s, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(this->dst), dst_s, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(this->requester), requester_s, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(this->capturer), capturer_s, INET_ADDRSTRLEN);

    cout << "\n============== VT Packet =================\n"
         << "cmd: " << cmd_s << '\n'
         << "src: " << src_s << '\t' << "dst: " << dst_s << '\n'
         << "req: " << requester_s << '\t' << "cap: " << capturer_s << '\n'
         << "==========================================\n" << endl;
}

/* VideoPublisher */

VideoPublisher::VideoPublisher()
{
    ioIsSet = false;
    memset(inFilename, 0, 256);
    memset(outFilename, 0, 256);
    memset(errmsg, 0, 1024);
}

VideoPublisher::~VideoPublisher()
{
}

int VideoPublisher::findVideoStreamIndex(AVFormatContext* ifmtCtx)
{
    if (!ifmtCtx) {
        cout << "[" << __func__ << "] Input format context is empty!\n";
        return -1;
    }

    // 查找视频流序号
    for (size_t i = 0; i < ifmtCtx->nb_streams; ++i) {
        if (ifmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            return i;
        }
    }

    cout << "CANNOT find video stream in input format context!\n";
    return -1;
}

AVFormatContext* VideoPublisher::openInputCtx(char inFilename[])
{
    int ret = 0;
    unsigned int i = 0;
    AVFormatContext* ifmtCtx = nullptr;
    AVInputFormat* ifmt = nullptr;
    AVDictionary* inputOptions = nullptr;

    // 查找输入封装
    if (strstr(inFilename, "/dev")) {
        ifmt = (AVInputFormat*)av_find_input_format("v4l2");
    } else if (strstr(inFilename, "video=")) {
        ifmt = (AVInputFormat*)av_find_input_format("dshow");
    }

    if (!ifmt) {
        cout << "CANNOT find input device\n";
        goto OPEN_INPUT_ERR;
    }

    // 输入上下文初始化

    // 打开输入上下文
    av_dict_set(&inputOptions, "video_size", "640x480", 0);
    av_dict_set(&inputOptions, "pixel_format", "yuyv422", 0);
    av_dict_set(&inputOptions, "framerate", "30", 0);
    av_dict_set_int(&inputOptions, "rtbufsize", 3041280 * 10, 0);

    if ((ret = avformat_open_input(&ifmtCtx, inFilename, ifmt, &inputOptions)) < 0) {
        cout << "CANNOT open input file: " << inFilename << "\n";
        goto OPEN_INPUT_ERR;
    }

    // 解码一段数据，获取流相关信息
    if ((ret = avformat_find_stream_info(ifmtCtx, 0)) < 0) {
        cout << "Failed to retrieve input stream information\n";
        goto OPEN_INPUT_ERR;
    }

    av_dump_format(ifmtCtx, 0, inFilename, 0);

    return ifmtCtx;

OPEN_INPUT_ERR:
    avformat_close_input(&ifmtCtx);

    if (ret < 0 && ret != AVERROR_EOF) {
        cout << "[" << __func__ << "] Error occurred\n";
        return nullptr;
    }

    return nullptr;
}

AVCodecContext* VideoPublisher::openInputCodecCtx(AVFormatContext* ifmtCtx)
{
    if (!ifmtCtx) {
        cout << "[" << __func__ << "] Input format context is empty!\n";
        return nullptr;
    }

    AVCodec* pRawCodec = nullptr;
    AVCodecContext* pRawCodecCtx = nullptr;
    int videoIndex = -1;

    // 查找视频流
    videoIndex = findVideoStreamIndex(ifmtCtx);
    if (videoIndex < 0) {
        cout << "No video stream!\n";
        return nullptr;
    }

    // 查找输入解码器
    pRawCodec = (AVCodec*) avcodec_find_decoder(ifmtCtx->streams[videoIndex]->codecpar->codec_id);
    if (!pRawCodec) {
        cout << "CANNOT find codec\n";
        return nullptr;
    }

    // 分配解码器上下文
    pRawCodecCtx = avcodec_alloc_context3(pRawCodec);
    if (!pRawCodecCtx) {
        cout << "CANNOT alloc codec context\n";
        return nullptr;
    }

    avcodec_parameters_to_context(pRawCodecCtx, ifmtCtx->streams[videoIndex]->codecpar);

    //  打开输入解码器
    if (avcodec_open2(pRawCodecCtx, pRawCodec, NULL) < 0) {
        cout << "CANNOT open codec\n";
        return nullptr;
    }

    return pRawCodecCtx;
}

AVCodecContext* VideoPublisher::openH264CodexCtx(AVPixelFormat codeType, int width, int height, int fps)
{
    AVCodec* pH264Codec = nullptr;
    AVCodecContext* pH264CodecCtx = nullptr;
    AVDictionary* params = nullptr;

    // 查找H264编码器
    pH264Codec = (AVCodec*) avcodec_find_encoder(AV_CODEC_ID_H264);
    // pH264Codec = (AVCodec*) avcodec_find_encoder_by_name("libx264");
    if (!pH264Codec) {
        cout << "CANNOT find h264 codec.\n";
        return nullptr;
    }

    // 设置编码器参数
    pH264CodecCtx = avcodec_alloc_context3(pH264Codec);
    pH264CodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pH264CodecCtx->pix_fmt = codeType; // AV_PIX_FMT_YUV420P;
    pH264CodecCtx->width = width;
    pH264CodecCtx->height = height;
    pH264CodecCtx->time_base = AVRational {1, fps};  // 时基
    pH264CodecCtx->framerate = AVRational {fps, 1};  // 帧率
    pH264CodecCtx->bit_rate = 400000;   //比特率（调节这个大小可以改变编码后视频的质量）
    // pH264CodecCtx->gop_size = 300;
    pH264CodecCtx->gop_size = 20;
    pH264CodecCtx->qmin = 10;
    pH264CodecCtx->qmax = 51;
    // some formats want stream headers to be separate
    // if (pH264CodecCtx->flags & AVFMT_GLOBALHEADER)
    {
        pH264CodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // 打开H.264编码器
    av_dict_set(&params, "preset", "superfast", 0);
    av_dict_set(&params, "tune", "zerolatency", 0); //实现实时编码
    if (avcodec_open2(pH264CodecCtx, pH264Codec, &params) < 0) {
        cout << "CANNOT open video encoder.\n";
        return nullptr;
    }

    return pH264CodecCtx;
}

AVFormatContext* VideoPublisher::openOutputCtx(char outFilename[], AVFormatContext* ifmtCtx, AVCodecContext* pCodecCtx)
{
    int ret = 0;
    char ofmtName[128] = { 0 };
    AVFormatContext* ofmtCtx = nullptr;

    // 分配输出上下文
    if (strstr(outFilename, "rtmp://")) {
        strcpy(ofmtName, "flv");
    } else if (strstr(outFilename, "udp://")) {
        strcpy(ofmtName, "mpegts");
    } else if (strstr(outFilename, "rtsp://")) {
        strcpy(ofmtName, "RTSP"); // RTSP+RTP的流媒体传输，实际上是将7个mpeg-ts packet封装为一个RTP packet
    }

    avformat_alloc_output_context2(&ofmtCtx, NULL, ofmtName, outFilename);
    if (!ofmtCtx) {
        cerr << "CANNOT create output context\n";
        goto OPEN_OUTPUT_ERR;
    }

    if (strstr(outFilename, "rtsp://")) {
        av_opt_set(ofmtCtx->priv_data, "rtsp_transport", "tcp", 0);
    }

    // 创建输出流
    for (size_t i = 0; i < ifmtCtx->nb_streams; ++i) {
        AVStream* outStream = avformat_new_stream(ofmtCtx, NULL);
        if (!outStream) {
            cerr << "Failed to allocate output stream\n";
            goto OPEN_OUTPUT_ERR;
        }

        ret = avcodec_parameters_from_context(outStream->codecpar, pCodecCtx);
        if (ret < 0) {
            av_make_error_string(errmsg, sizeof(errmsg), ret);
            cerr << "Fail to copy output stream codec parameters from codec context\n";
            goto OPEN_OUTPUT_ERR;
        }

        // RTSP specified
        if (strstr(outFilename, "rtsp://")) {
            outStream->nb_frames = 0;
            outStream->time_base.num = 1;
            outStream->time_base.den = 90000;
            outStream->start_time = 0;
        }
    }

    av_dump_format(ofmtCtx, 0, outFilename, 1);

    // 创建并初始化一个AVIOContext, 用以访问输出上下文指定的资源
    // RTSP具有AVFMT_NOFILE标志，不需要自己创建IO上下文
    if (!(ofmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmtCtx->pb, outFilename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            cerr << "CANNOT open output URL: " << outFilename << "\n";
            goto OPEN_OUTPUT_ERR;
        }
    }

    // 打开输出上下文并初始化
    ret = avformat_write_header(ofmtCtx, NULL);
    if (ret < 0) {
        cerr << "Error accourred when opening output file\n";
        goto OPEN_OUTPUT_ERR;
    }

    return ofmtCtx;

OPEN_OUTPUT_ERR:
    if (ofmtCtx && !(ofmtCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&ofmtCtx->pb);
    }
    avformat_free_context(ofmtCtx);

    if (ret < 0 && ret != AVERROR_EOF) {
        cerr << "[" << __func__ << "] Error occurred\n";
        return nullptr;
    }

    return nullptr;
}

int VideoPublisher::setIOName(const char deviceName[], const char publishUrl[])
{
    if (strlen(deviceName) >= 256 || strlen(publishUrl) >= 256) {
        cerr << "VideoPublisher::run() : arguments too long, must shorter than 256 characters\n";
        return -1;
    }

    strcpy(inFilename, deviceName);
    strcpy(outFilename, publishUrl);

    ioIsSet = true;
    return 0;
}

void VideoPublisher::run()
{
    if (runCount == 0) {
        runCount++;
    } else {
        cout << "VideoPublisher thread exited: a thread is already running.\n";
        return;
    }

    if (!ioIsSet) {
        cerr << __func__ << "Input & output information is not set yet!\n";
        return;
    }

    cout << "Video capture INPUT: " << inFilename << "\nVideo capture OUTPUT: " << outFilename << "\n";

    // 基本设置初始化
    avdevice_register_all();
    avformat_network_init();

    cout << "FFMPEG version = " << av_version_info() << "\n";

    // 打开输入上下文
    ifmtCtx = openInputCtx(inFilename);
    if (!ifmtCtx) {
        cerr << "Fail to open input format context!\n";
        goto PUBLISHER_END;
    }

    // 查找视频流在上下文中的序号
    vsIndex = findVideoStreamIndex(ifmtCtx);
    if (vsIndex < 0) {
        cerr << "No video stream in INPUT!\n";
        goto PUBLISHER_END;
    }

    // 打开输入解码器
    pRawCodecCtx = openInputCodecCtx(ifmtCtx);
    if (!pRawCodecCtx) {
        cerr << "Fail to open input codec context!\n";
        goto PUBLISHER_END;
    }

    // 打开 H264 编码器
    pH264CodecCtx = openH264CodexCtx(AV_PIX_FMT_YUV420P, pRawCodecCtx->width, pRawCodecCtx->height, H264FPS);
    if (!pH264CodecCtx) {
        cerr << "Fail to open H264 codec context!\n";
        goto PUBLISHER_END;
    }

    // 打开输出上下文
    ofmtCtx = openOutputCtx(outFilename, ifmtCtx, pH264CodecCtx);
    if (!ofmtCtx) {
        cerr << "Fail to open output format context!\n";
        goto PUBLISHER_END;
    }

    // 分配缓存空间
    pFrameRaw = av_frame_alloc();
    pFrameCopy = av_frame_alloc();
    pFrameYUV = av_frame_alloc();
    pPkt = av_packet_alloc();

    pFrameCopy->width = pRawCodecCtx->width;
    pFrameCopy->height = pRawCodecCtx->height;
    pFrameCopy->format = pRawCodecCtx->pix_fmt;
    // ret = av_frame_get_buffer(pFrameCopy, 16);
    // if (ret != 0) {
    //     cerr << "pFrameCopy->data buffer init Failed!\n";
    //     goto PUBLISHER_END;
    // }
    ret = av_image_alloc(pFrameCopy->data, pFrameCopy->linesize,
                   pRawCodecCtx->width, pRawCodecCtx->height, pRawCodecCtx->pix_fmt, 16);
    if (ret < 0) {
        cerr << "pFrameCopy->data buffer init Failed!\n";
        goto PUBLISHER_END;
    } else {
        cout << "Allocate memory for pFrameCopy->data: " << ret << " bytes.\n";
    }

    ret = av_image_alloc(pFrameYUV->data, pFrameYUV->linesize,
                   pH264CodecCtx->width, pH264CodecCtx->height, pH264CodecCtx->pix_fmt, 1);
    if (ret < 0) {
        cerr << "Fail to allocate memory for pFrameYUV->data\n";
        goto PUBLISHER_END;
    } else {
        cout << "Allocate memory for pFrameYUV->data: " << ret << " bytes.\n";
    }

    // RawVideo与H.264输入之间的转换
    pImgConvertCtx = sws_getContext(
                     pRawCodecCtx->width, pRawCodecCtx->height, pRawCodecCtx->pix_fmt, 
                     pH264CodecCtx->width, pH264CodecCtx->height, pH264CodecCtx->pix_fmt,
                     SWS_BILINEAR, NULL, NULL, NULL);

    // 加入已推流节点列表
    publishingList.add(outFilename);

    // 循环采集、编码、推流
    for (size_t cameraFrame = 0; stopRequested() == false; cameraFrame++) {

        // 从摄像头读取一帧数据
        ret = av_read_frame(ifmtCtx, pPkt);
        if (ret < 0) {
            cerr << "Fail to read from camera!\n";
            break;
        }

        if (cameraFrame % 6 == 0) {
            av_packet_unref(pPkt);
            continue;
        }

        if (pPkt->stream_index == vsIndex) {
            // 送入 rawvideo 解码器
            ret = avcodec_send_packet(pRawCodecCtx, pPkt);
            if (ret < 0) {
                cerr << "Decode error.\n";
                break;
            }

            // 从 rawvideo 解码器读取一个 frame
            if (avcodec_receive_frame(pRawCodecCtx, pFrameRaw) >= 0) {
                // 将当前帧数据强制复制到内存
                ret = av_frame_copy(pFrameCopy, pFrameRaw);
                if (ret < 0) {
                    cerr << "av_frame_copy(pFrameCopy, pFrameRaw) Failed!\n";
                    continue;
                }

                ret = av_frame_copy_props(pFrameCopy, pFrameRaw);
                if (ret < 0) {
                    cerr << "av_frame_copy_props(pFrameCopy, pFrameRaw) Failed!\n";
                    continue;
                }

                // 转换到 H.264 编码器的输入格式
                sws_scale(pImgConvertCtx, 
                            (const uint8_t* const*)pFrameCopy->data, pFrameCopy->linesize, 0, pFrameCopy->height, 
                            pFrameYUV->data, pFrameYUV->linesize);

                pFrameYUV->format = pH264CodecCtx->pix_fmt;
                pFrameYUV->width = pH264CodecCtx->width;
                pFrameYUV->height = pH264CodecCtx->height;
                pFrameYUV->pts = frameIndex;
                pFrameYUV->pkt_pos = -1;
                frameIndex++;

                // 送入 H.264 编码器
                ret = avcodec_send_frame(pH264CodecCtx, pFrameYUV);
                if (ret < 0) {
                    av_make_error_string(errmsg, sizeof(errmsg), ret);
                    cerr << "Failed to encode.\n";
                    break;
                }

                // 从 H.264 编码器读取一个 packet
                if (avcodec_receive_packet(pH264CodecCtx, pPkt) >= 0) {
                    AVStream* outStream = ofmtCtx->streams[pPkt->stream_index];

                    // 将时间戳从编码器时基转换到推流器时基
                    av_packet_rescale_ts(pPkt, pH264CodecCtx->time_base, outStream->time_base);
                    pPkt->pos = -1;

                    // 将 packet 输出到推流器
                    ret = av_interleaved_write_frame(ofmtCtx, pPkt);
                    if (ret < 0) {
                        av_make_error_string(errmsg, sizeof(errmsg), ret);
                        cerr << "Send packet failed: [" << ret << "] " << errmsg << "\n";
                    }
                    #ifdef DEBUG_PRINT_VS_COUNT
                    else {
                        if (frameIndex % 100 == 0)
                            cout << "Send " << std::setw(5) << frameIndex << " packet successfully!\n";
                    }
                    #endif
                }
                else {
                    cout << "Waiting for H264 encoder...\n";
                }
            }
        }
        // av_frame_unref(pFrameYUV);
        // av_frame_unref(pFrameCopy);
        av_frame_unref(pFrameRaw);
        av_packet_unref(pPkt);
    }
    av_write_trailer(ofmtCtx);

PUBLISHER_END:
    publishingList.erase(outFilename);

    avformat_close_input(&ifmtCtx);

    if (pFrameCopy->data) {
        av_freep(&pFrameCopy->data[0]);
    }

    if (pFrameYUV->data) {
        av_freep(&pFrameYUV->data[0]);
    }

    av_packet_free(&pPkt);
    av_frame_free(&pFrameRaw);
    av_frame_free(&pFrameCopy);
    av_frame_free(&pFrameYUV);

    /* close output */
    if (ofmtCtx && !(ofmtCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&ofmtCtx->pb);
    }
    avformat_free_context(ofmtCtx);   // 调用该函数会内存错误，原因不详

    if (ret < 0 && ret != AVERROR_EOF) {
        cerr << "Error occurred\n";
    }

    runCount--;
    cout << "VideoPublisher::run() exit!" << endl;
}

/* VideoRelayer */

// std::unordered_map<VideoRelayer*, size_t> heartBeats;
// std::unordered_map<VideoRelayer*, bool> quitFfmpegBlocks;

VideoRelayer::VideoRelayer()
{
    ioIsSet = false;
    memset(inFilename, 0, 256);
    memset(outFilename, 0, 256);
    memset(errmsg, 0, 1024);
}

VideoRelayer::VideoRelayer(const char pullUrl[], const char publishUrl[])
{
    if (strlen(pullUrl) >= 256 || strlen(publishUrl) >= 256) {
        cerr << __func__ << "arguments too long, must shorter than 256 characters\n";
        return;
    } else {
        strcpy(inFilename, pullUrl);
        strcpy(outFilename, publishUrl);
        ioIsSet = true;
    }

    // heartBeats[this] = 0;
    // quitFfmpegBlocks[this] = false;
}

VideoRelayer::~VideoRelayer()
{

}

int VideoRelayer::findVideoStreamIndex(AVFormatContext* ifmtCtx)
{
    if (!ifmtCtx) {
        cout << "[" << __func__ << "] Input format context is empty!\n";
        return -1;
    }

    // 查找视频流序号
    for (size_t i = 0; i < ifmtCtx->nb_streams; ++i) {
        if (ifmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            return i;
        }
    }

    cout << "CANNOT find video stream in input format context!\n";
    return -1;
}

AVFormatContext* VideoRelayer::openInputCtx(char inFilename[])
{
    int ret = 0;
    int videoIndex = 0;
    AVFormatContext* ifmtCtx = avformat_alloc_context();
    AVDictionary* inputOptions = nullptr;

    ifmtCtx->interrupt_callback.callback = relayerCallbackFun;
    ifmtCtx->interrupt_callback.opaque = this;

    if (strstr(inFilename, "rtsp://")) {
        av_dict_set(&inputOptions, "rtsp_transport", "tcp", 0);
    }

    if ((ret = avformat_open_input(&ifmtCtx, inFilename, 0, &inputOptions)) < 0) {
        cerr << "Could not open input file.\n";
        av_make_error_string(errmsg, sizeof(errmsg), ret);  // debug
        cerr << errmsg << '\n';
        goto OPEN_INPUT_ERR;
    }

    if ((ret = avformat_find_stream_info(ifmtCtx, 0)) < 0) {
        cerr << "Failed to retrieve input stream information\n";
        goto OPEN_INPUT_ERR;
    }

    videoIndex = findVideoStreamIndex(ifmtCtx);
    if (videoIndex < 0) {
        cerr << "CANNOT find video stream from input context!\n";
        goto OPEN_INPUT_ERR;
    }

    if (strstr(inFilename, "rtsp://")) {
        ifmtCtx->streams[videoIndex]->nb_frames = 0;
        ifmtCtx->streams[videoIndex]->time_base.num = 1;
        ifmtCtx->streams[videoIndex]->time_base.den = 90000;
        ifmtCtx->duration = 3600;
        // outStream->start_time = 0;
    }

    return ifmtCtx;

OPEN_INPUT_ERR:
    avformat_close_input(&ifmtCtx);

    if (ret < 0 && ret != AVERROR_EOF) {
        cerr << "[" << __func__ << "] Error occurred\n";
        return nullptr;
    }

    return nullptr;
}

AVFormatContext* VideoRelayer::openOutputCtx(char outFilename[], AVFormatContext* ifmtCtx)
{
    int ret = 0;
    char ofmtName[128] = { 0 };
    AVFormatContext* ofmtCtx = nullptr;
    AVOutputFormat* ofmt = nullptr;

    // 分配输出上下文
    if (strstr(outFilename, "rtmp://")) {
        strcpy(ofmtName, "flv");
    } else if (strstr(outFilename, "udp://")) {
        strcpy(ofmtName, "mpegts");
    } else if (strstr(outFilename, "rtsp://")) {
        strcpy(ofmtName, "RTSP");
    }

    ret = avformat_alloc_output_context2(&ofmtCtx, NULL, ofmtName, outFilename);
    if (ret < 0) {
        cerr << "Could not create output context\n";
        goto OPEN_OUTPUT_ERR;
    }

    if (strstr(outFilename, "rtsp://")) {
        av_opt_set(ofmtCtx->priv_data, "rtsp_transport", "tcp", 0);
    }

    // 创建输出流
    ofmt = (AVOutputFormat*) ofmtCtx->oformat;
    for (size_t i = 0; i < ifmtCtx->nb_streams; i++) {
        // Create output AVStream according to input AVStream
        AVStream* inStream = ifmtCtx->streams[i];
        AVCodec* pCodec = (AVCodec*) avcodec_find_decoder(inStream->codecpar->codec_id);
        AVStream* outStream = avformat_new_stream(ofmtCtx, pCodec);
        if (!outStream) {
            cerr << "Fail to allocating output stream\n";
            goto OPEN_OUTPUT_ERR;
        }

        // Copy the settings of AVCodecContext
        AVCodecContext* pCodecCtx = avcodec_alloc_context3(pCodec);
        ret = avcodec_parameters_to_context(pCodecCtx, inStream->codecpar);
        if (ret < 0) {
            cerr << "Failed to copy context from input to output stream codec context\n";
            goto OPEN_OUTPUT_ERR;
        }
        pCodecCtx->codec_tag = 0;
        if (ofmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
            pCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        ret = avcodec_parameters_from_context(outStream->codecpar, pCodecCtx);
        if (ret < 0) {
            av_log(NULL , AV_LOG_ERROR , "eno:[%d] error to paramters codec paramter \n" , ret);
        }

        // RTSP specified
        if (strstr(outFilename, "rtsp://")) {
            outStream->nb_frames = 0;
            outStream->time_base.num = 1;
            outStream->time_base.den = 90000;
            // outStream->start_time = 0;
        }
    }

    av_dump_format(ofmtCtx, 0, outFilename, 1);

    // 创建并初始化一个AVIOContext, 用以访问输出上下文指定的资源
    // RTSP具有AVFMT_NOFILE标志，不需要自己创建IO上下文
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmtCtx->pb, outFilename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            cerr << "Could not open output URL: " << outFilename << '\n';
            goto OPEN_OUTPUT_ERR;
        }
    }

    // 打开输出上下文并初始化
    ret = avformat_write_header(ofmtCtx, NULL);
    if (ret < 0) {
        cerr << "Error occurred when opening output URL\n";
        goto OPEN_OUTPUT_ERR;
    }

    return ofmtCtx;

OPEN_OUTPUT_ERR:
    if (ofmtCtx && !(ofmtCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&ofmtCtx->pb);
    }
    avformat_free_context(ofmtCtx);

    if (ret < 0 && ret != AVERROR_EOF) {
        cerr <<  "[" << __func__ << "] Error occurred\n";
        return nullptr;
    }

    return nullptr;
}

void VideoRelayer::run()
{
    if (runCount == 0) {
        runCount++;
    } else {
        cout << "VideoRelayer thread exited: a thread is already running.\n";
        return;
    }

    cout << "Video relay INPUT: " << inFilename << "\nVideo relay OUTPUT: " << outFilename << "\n";

    bool abnormalQuit = false;

    // 初始化
    avformat_network_init();

    // 打开输入上下文
    ifmtCtx = openInputCtx(inFilename);
    if (!ifmtCtx) {
        cerr << "Fail to open input context!\n";
        exit(1);
    }

    // 查找视频流序号
    videoIndex = findVideoStreamIndex(ifmtCtx);
    if (videoIndex < 0) {
        cerr << "No video stream in INPUT!\n";
        exit(1);
    }

    av_dump_format(ifmtCtx, 0, inFilename, 0);

    // 打开输出上下文
    ofmtCtx = openOutputCtx(outFilename, ifmtCtx);
    if (!ofmtCtx) {
        cerr << "Fail to open output format context!\n";
        exit(1);
    }

#if USE_H264BSF
    AVBitStreamFilterContext* h264bsfc = av_bitstream_filter_init("h264_mp4toannexb");
#endif

    pPkt = av_packet_alloc();

    // 丢弃最初的3帧
    for (int i = 0; i < 3; i++) {
        ret = av_read_frame(ifmtCtx, pPkt);
        av_packet_unref(pPkt);
    }

    // 加入已推流节点列表
    publishingList.add(outFilename);

    // while (1) {
    for (size_t i = 0; stopRequested() == false; i++) {
        AVStream *inStream, *outStream;
        // Get an AVPacket
        ret = av_read_frame(ifmtCtx, pPkt);
        resetHeartBeat();    // 重置心跳值，告知 VideoTransCtrler 线程存活
        if (ret < 0) {
            cerr << "VideoRelayer: Broken link\n";
            abnormalQuit = true;
            break;
        }

        if (!firstPtsIsSet) {
            firstPts = pPkt->pts;
            firstDts = pPkt->dts;
            firstPtsIsSet = true;
        }

        inStream = ifmtCtx->streams[pPkt->stream_index];
        outStream = ofmtCtx->streams[pPkt->stream_index];

        /* copy packet */
        // Convert PTS/DTS
        pPkt->pts -= firstPts;
        pPkt->dts -= firstDts;
        av_packet_rescale_ts(pPkt, inStream->time_base, outStream->time_base);
        pPkt->pos = -1;

        // Print to Screen
        if (pPkt->stream_index == videoIndex) {
            frameIndex++;

            #if USE_H264BSF
            av_bitstream_filter_filter(h264bsfc, inStream->codec, NULL, pPkt->data, pPkt->size, pPkt->data, pPkt->size, 0);
            #endif
        }

        ret = av_interleaved_write_frame(ofmtCtx, pPkt);
        if (ret < 0) {
            av_make_error_string(errmsg, sizeof(errmsg), ret);
            cerr << "Relay packet failed: " << errmsg << '\n';
        }
        #ifdef DEBUG_PRINT_VS_COUNT
        else {
            if (frameIndex % 100 == 0)
                cout << "Relay " << std::setw(5) << frameIndex << " packet successfully!\n";
        }
        #endif

        av_packet_unref(pPkt);
    }

#if USE_H264BSF
    av_bitstream_filter_close(h264bsfc);
#endif

    // Write file trailer
    av_write_trailer(ofmtCtx);

RELAYER_END:
    publishingList.erase(outFilename);
    NodeConfig& config = NodeConfig::getInstance();
    if (abnormalQuit) {
        lostList.add(outFilename);
    }

    av_packet_free(&pPkt);
    av_free(pPkt);

    avformat_close_input(&ifmtCtx);

    /* close output */
    if (ofmtCtx && !(ofmtCtx->oformat->flags & AVFMT_NOFILE)) {
        cout << "Closing output...\n";
        avio_closep(&ofmtCtx->pb);
    }
    avformat_free_context(ofmtCtx);

    if (ret < 0 && ret != AVERROR_EOF) {
        cerr << "Error occurred.\n";
        return;
    }

    runCount--;
    cout << "VideoRelayer::run() exit!" << endl;
}

bool VideoRelayer::checkHeartTimeout(size_t ms)
{
    heartBeat += ms;
    if (heartBeat > RELAY_TIMEOUT_MS) {
        return true;
    }
    return false;
}

int VideoRelayer::relayerCallbackFun(void* param)
{
    VideoRelayer* pRelayer = (VideoRelayer*)param;
    if (!pRelayer)
        return 0;

    if (pRelayer->quitFfmpegBlock == true) {
        // 通知ffmpeg退出阻塞
        return 1;
    }
    return 0;
}

/* VideoTransCtrler */

VideoTransCtrler::VideoTransCtrler()
{
    runCount = 0;
}

VideoTransCtrler::~VideoTransCtrler()
{
}

void VideoTransCtrler::packetReact(VideoTransPacket& pkt)
{
    NodeConfig& config = NodeConfig::getInstance();
    DsrRouteGetter routeGetter;

    in_addr_t myIP = config.getMyIP();
    in_addr_t nextHopIP = 0;
    VideoTransPacket pktToSend(pkt);

    switch (pkt.getCmd()) {
    case VideoTransCmd::start: {
        try {
            if (pkt.getCapturer() == myIP) {
                nextHopIP = routeGetter.getNextHop(pkt.getRequester(), 10, SEND_REQ_ANYWAY);
                pktToSend.setCmd(VideoTransCmd::ready);
            } else {
                nextHopIP = routeGetter.getNextHop(pkt.getCapturer(), 10, SEND_REQ_ANYWAY);
            }
        } catch (const char* msg) {
            if (strcmp(msg, "DestinationUnreachable") == 0) {
                cerr << __func__ << " Fail to find route!\n";
                pkt.printPktInfo();
                break;
            }
        }

        pktToSend.setSrc(myIP);
        pktToSend.setDst(nextHopIP);

        // 等待本地推流初始化完成后，再发出ready包
        if (pktToSend.getCmd() == VideoTransCmd::ready) {
            // 汇聚节点不会收到 start 包，无需考虑汇聚节点准备好
            while (publishingList.find(myIP, myIP) == false) {
                cout << "Local video stream is not ready yet, waiting...\n";
                sleep_for(seconds(1));
            }
        }

        packetSendQueue.push(pktToSend);
        break;
    }

    case VideoTransCmd::ready: {
        addRelayer(pkt.getCapturer(), pkt.getSrc());
        std::string lostRecoverUrl = generateUrl(pkt.getCapturer(),
            config.getNodeType() == NodeType::sink ? config.getSinkIP2Ctrler() : myIP);
        if (lostList.find(lostRecoverUrl)) {
            lostList.erase(lostRecoverUrl);
            cout << "Lost stream: " << lostRecoverUrl << " recovered!\n";
        }

        if (pkt.getRequester() != myIP) {
            try {
                nextHopIP = routeGetter.getNextHop(pkt.getRequester(), 10, SEND_REQ_ANYWAY);
            } catch (const char* msg) {
                if (strcmp(msg, "DestinationUnreachable") == 0) {
                    cerr << __func__ << " Fail to find route!\n";
                    pkt.printPktInfo();
                    break;
                }
            }

            pktToSend.setSrc(myIP);
            pktToSend.setDst(nextHopIP);

            // 等待推流中继初始化完成后，再发出ready包
            while (publishingList.find(pkt.getCapturer(), myIP) == false) {
                cout << "Relayed video stream is not ready yet, waiting...\n";
                sleep_for(seconds(1));
            }

            packetSendQueue.push(pktToSend);
        }
        break;
    }

    case VideoTransCmd::stop: {
        if (pkt.getCapturer() == config.getMyIP()) {
            break;
        }

        deleteRelayer(pkt.getCapturer());

        try {
            nextHopIP = routeGetter.getNextHop(pkt.getCapturer(), 10, SEND_REQ_ANYWAY);
        } catch (const char* msg) {
            if (strcmp(msg, "DestinationUnreachable") == 0) {
                cerr << __func__ << " Fail to find route!\n";
                pkt.printPktInfo();
                break;
            }
        }

        if (nextHopIP != pkt.getCapturer()) {
            pktToSend.setSrc(myIP);
            pktToSend.setDst(nextHopIP);

            // 等待本地推流中继退出后，再继续发送stop包
            while (publishingList.find(pkt.getCapturer(), myIP) == true) {
                cout << "Local relayer is still running, waiting...\n";
                sleep_for(seconds(1));
            }

            packetSendQueue.push(pktToSend);
        }
        break;
    }

    case VideoTransCmd::lost: {
        // TODO
        break;
    }

    default:
        break;
    }
}

void VideoTransCtrler::addRelayer(in_addr_t capturerIP, in_addr_t pullIP)
{
    auto it = relayerList.find(capturerIP);
    if (it != relayerList.end()) {
        char ip_s[INET_ADDRSTRLEN];
        memset(ip_s, 0, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &capturerIP, ip_s, INET_ADDRSTRLEN);
        cout << "Relayer pulling stream from " << ip_s << " already exist!\n";
        return;
    }

    char pullUrl[VS_URL_MAX_LEN];
    char republishUrl[VS_URL_MAX_LEN];
    VideoRelayer* pRelayer = nullptr;
    NodeConfig& config = NodeConfig::getInstance();
    in_addr_t myIP = config.getMyIP();

    memset(pullUrl, 0, VS_URL_MAX_LEN);
    memset(republishUrl, 0, VS_URL_MAX_LEN);

    generateUrl(capturerIP, pullIP, pullUrl);
    if (config.getNodeType() == NodeType::sink) {
        generateUrl(capturerIP, config.getSinkIP2Ctrler(), republishUrl);
    } else {
        generateUrl(capturerIP, myIP, republishUrl);
    }

    pRelayer = new VideoRelayer(pullUrl, republishUrl);
    relayerList.insert({ capturerIP, pRelayer });
    relayerVec.push_back(pRelayer);
    std::thread relayerThread(&VideoRelayer::run, pRelayer);
    relayerThread.detach();
}

void VideoTransCtrler::deleteRelayer(in_addr_t capturerIP)
{
    auto it = relayerList.find(capturerIP);
    if (it == relayerList.end()) {
        char ip_s[INET_ADDRSTRLEN];
        memset(ip_s, 0, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &capturerIP, ip_s, INET_ADDRSTRLEN);
        cout << "No relayer is pulling stream from " << ip_s << " , nothing deleted!\n";
        return;
    }

    VideoRelayer* pRelayer = it->second;
    if (pRelayer->getRunCount() > 0) {
        pRelayer->stop();
        sleep_for(milliseconds(20));
    }
    delete pRelayer;
    pRelayer = nullptr;
    relayerList.erase(it);
}

void VideoTransCtrler::run()
{
    if (runCount == 0) {
        runCount++;
    } else {
        cout << "VideoTransCtrler thread exited: a thread is already running.\n";
        return;
    }

    std::atomic<bool> handlerStopFlag(false);
    std::atomic<bool> retryerStopFlag(false);
    NodeConfig& config = NodeConfig::getInstance();
    in_addr_t myIP = config.getMyIP();
    DsrRouteGetter routeGetter;

    // 因为 waitForPacket() 可能会被阻塞，所以必须单独一个子线程，以便程序退出时唤醒它
    auto packetHandler = [&]() {
        while (!handlerStopFlag) {
            size_t pktCount = packetRecvQueue.waitForPacket();
            if (pktCount == 0)
                continue;

            VideoTransPacket pkt = packetRecvQueue.pop();
            packetReact(pkt); // TODO 此处可以优化，因为等待某个节点准备好时，可能阻塞后面的packet的处理
        }
    };

    // 若有断开的视频流，则尝试重新连接
    auto relayerRetryer = [&]() {
        while (!retryerStopFlag) {
            if (!lostList.empty()) {
                in_addr_t capturerIP, publishIP, nextHopIP;
                std::string lostUrl = lostList.fetch();
                lostList.erase(lostUrl);
                splitUrl(lostUrl, capturerIP, publishIP);

                deleteRelayer(capturerIP);

                if (config.getNodeType() == NodeType::sink) {
                    while (publishingList.find(lostUrl)) {
                        cout << "Lost link relayer is not exited, waiting...\n";
                        sleep_for(seconds(1));
                    }

                    try {
                        nextHopIP = routeGetter.getNextHop(capturerIP, 5, SEND_REQ_ANYWAY);
                    } catch (const char* msg) {
                        if (strcmp(msg, "DestinationUnreachable") == 0) {
                            cerr << "Fail to find route to " << capturerIP << "\n";
                            continue;
                        }
                    }

                    VideoTransPacket pkt(VideoTransCmd::start, myIP, nextHopIP, myIP, capturerIP);
                    packetSendQueue.push(pkt);
                }
            }
            sleep_for(seconds(1));
        }
    };

    // Just for debug
    auto videoRequester = [&]() {
        in_addr_t nodeIP, nextHopIP;
        DsrRouteGetter routeGetter;
        char nodeIPList[][INET_ADDRSTRLEN] = { "192.168.2.100", "192.168.2.101", "192.168.2.103", "192.168.2.104"};

        sleep_for(seconds(3));

        for (char* ip_s : nodeIPList) {
            inet_pton(AF_INET, ip_s, &nodeIP);
            try {
                nextHopIP = routeGetter.getNextHop(nodeIP, 15, SEND_REQ_ANYWAY);
            } catch (const char* msg) {
                if (strcmp(msg, "DestinationUnreachable") == 0) {
                    cerr << "Fail to find route to " << ip_s << "\n";
                    continue;
                }
            }
            VideoTransPacket pkt(VideoTransCmd::start, myIP, nextHopIP, myIP, nodeIP);
            packetSendQueue.push(pkt);
        }

        sleep_for(seconds(5));

        /* cout << "***************** Stopping video stream... *******************\n";

        for (char* ip_s : nodeIPList) {
            inet_pton(AF_INET, ip_s, &nodeIP);

            // 停止该节点的 relayer
            auto it = relayerList.find(nodeIP);
            deleteRelayer(nodeIP);

            // 等待 relayer 停止后，再发送 stop 包
            while (publishingList.find(nodeIP, config.getSinkIP2Ctrler())) {
                cout << "Relayer still running, waiting...\n";
                sleep_for(seconds(1));
            }

            // 向该节点发送 stop 包
            try {
                nextHopIP = routeGetter.getNextHop(nodeIP, 15, CHECK_TABLE_FIRST);
            } catch (const char* msg) {
                if (strcmp(msg, "DestinationUnreachable") == 0) {
                    cerr << "Fail to find route to " << ip_s <<"\n";
                    continue;
                }
            }

            VideoTransPacket pkt(VideoTransCmd::stop, myIP, nextHopIP, myIP, nodeIP);
            packetSendQueue.push(pkt);
        } */
    };

    std::thread recvQueueThread(&PacketRecvQueue::run, &packetRecvQueue);
    std::thread sendQueueThread(&PacketSendQueue::run, &packetSendQueue);
    std::thread relayerRetryerThread(relayerRetryer);
    std::thread packetHandlerThread(packetHandler);

    if (config.getNodeType() == NodeType::sink) {
        std::thread videoRequesterThread(videoRequester);
        videoRequesterThread.detach();
    }

    // 等待退出
    std_clock timeNow, timeOld;
    timeNow = std::chrono::steady_clock::now();
    timeOld = timeNow;

    while (stopRequested() == false) {
        timeNow = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> diff = timeNow - timeOld;
        // cout << "Checking relayer state... diff = " << diff.count() <<  " ms\n";
        timeOld = timeNow;

        // 遍历所有 relayer 实例
        for (auto it = relayerVec.begin(); it != relayerVec.end();) {
            VideoRelayer* pRelayer = *it;
            if (!pRelayer) {
                cout << "Relayer " << pRelayer << " is empty, erased.\n";
                it = relayerVec.erase(it);
                continue;
            }

            if (pRelayer->checkHeartTimeout(diff.count()) == true) {
                pRelayer->setQuitBlock();   // 已超时，使阻塞函数如 av_read_frame() 退出
                it = relayerVec.erase(it);
                cout << "Relayer " << pRelayer << " timeout, erased.\n";
                continue;
            }
            it++;
        }

        sleep_for(seconds(3));
    }

    // 处理收取队列
    while (!packetRecvQueue.empty()) {
        cout << "Some recved packets needs to be handled, waiting...\n";
        sleep_for(seconds(1));
    }

    // 处理发送队列
    while (!packetSendQueue.empty()) {
        cout << "Some packets needs to be send, waiting...\n";
        sleep_for(seconds(1));
    }

    handlerStopFlag = true;
    retryerStopFlag = true;
    packetSendQueue.stop();
    packetRecvQueue.stop();

    // 清空 relayer列表 和全局的 publishingList
    while (!relayerList.empty()) {
        in_addr_t capturerIP = relayerList.begin()->first;
        deleteRelayer(capturerIP);
        publishingList.erase(capturerIP, config.getNodeType() == NodeType::sink ? config.getSinkIP2Ctrler() : myIP);
    }

    sendQueueThread.join();
    recvQueueThread.join();
    relayerRetryerThread.join();
    packetHandlerThread.join();

    runCount--;
    cout << "VideoTransCtrler::run() exit!\n";
}

void generateUrl(in_addr_t capturerIP, in_addr_t publishIP, char urlBuf[])
{
    char ipAddr_s[INET_ADDRSTRLEN];
    char twoDigit[3];
    char num;

    memset(ipAddr_s, 0, INET_ADDRSTRLEN);
    memset(twoDigit, 0, 3);

    inet_ntop(AF_INET, &publishIP, ipAddr_s, INET_ADDRSTRLEN);

    if (__BYTE_ORDER == __LITTLE_ENDIAN) {
        num = (char)((capturerIP & 0xFF000000) >> 24);
    } else if (__BYTE_ORDER == __BIG_ENDIAN) {
        num = (char)(capturerIP & 0x000000FF);
    }
    twoDigit[1] = '0' + num % 10;
    num = num / 10;
    twoDigit[0] = '0' + num % 10;

    sprintf(urlBuf, "rtsp://%s:%d/vs%s", ipAddr_s, PORT_VIDEO, twoDigit);
}

std::string generateUrl(in_addr_t capturerIP, in_addr_t publishIP)
{
    char urlBuf[VS_URL_MAX_LEN];
    memset(urlBuf, 0, VS_URL_MAX_LEN);

    generateUrl(capturerIP, publishIP, urlBuf);
    return std::string(urlBuf);
}

void splitUrl(const std::string& url, in_addr_t& capturerIP, in_addr_t& publishIP)
{
    char ip_s[INET_ADDRSTRLEN];
    memset(ip_s, 0, INET_ADDRSTRLEN);

    auto endStr = []() -> std::string {
        char buf[6] = { 0 };
        sprintf(buf, ":%d", PORT_VIDEO);
        return std::string(buf);
    };

    size_t begin = url.find("rtsp://") + strlen("rtsp://");
    size_t end = url.find(endStr());

    memcpy(ip_s, url.c_str() + begin, end - begin);

    inet_pton(AF_INET, ip_s, &publishIP);

    char c;
    strcpy(ip_s, "192.168.2.1");
    c = url[url.size() - 2];
    strncat(ip_s, &c, 1);
    c = url[url.size() - 1];
    strncat(ip_s, &c, 1);

    inet_pton(AF_INET, ip_s, &capturerIP);
}
