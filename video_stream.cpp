#include "video_stream.h"


/* VideoPublisher */

VideoPublisher::VideoPublisher()
{
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

void VideoPublisher::run()
{
    int argc = 4;
    char argv[][64] = { "./uav_main", "/dev/video0", "rtsp://192.168.15.202:8554/vs02", "20" };

    if (argc < 4) {
        cerr << "Usage: " << argv[0] << " <input> <rtsp_addr> <duration in sec>\n"
             << "e.g.1 " << argv[0] << " /dev/video0 rtsp://192.168.15.200:8554/mystream 60\n"
             << "e.g.2 " << argv[0] << " \"video=2K USB Camera\" rtsp://127.0.0.1:8554/mystream 120\n";
        exit(1);
    }

    // 基本设置初始化
    strcpy(inFilename, argv[1]);
    strcpy(outFilename, argv[2]);
    exeTime = std::stoi(argv[3]);
    cout << "INPUT: " << inFilename << "\nOUTPUT: " << outFilename << "\n";

    avdevice_register_all();
    avformat_network_init();

    cout << "av_version_info = " << av_version_info() << "\n";

    // 打开输入上下文
    ifmtCtx = openInputCtx(inFilename);
    if (!ifmtCtx) {
        cerr << "Fail to open input format context!\n";
        goto end;
    }

    // 查找视频流在上下文中的序号
    vsIndex = findVideoStreamIndex(ifmtCtx);
    if (vsIndex < 0) {
        cerr << "No video stream in INPUT!\n";
        goto end;
    }

    // 打开输入解码器
    pRawCodecCtx = openInputCodecCtx(ifmtCtx);
    if (!pRawCodecCtx) {
        cerr << "Fail to open input codec context!\n";
        goto end;
    }

    // 打开 H264 编码器
    pH264CodecCtx = openH264CodexCtx(AV_PIX_FMT_YUV420P, pRawCodecCtx->width, pRawCodecCtx->height, H264FPS);
    if (!pH264CodecCtx) {
        cerr << "Fail to open H264 codec context!\n";
        goto end;
    }

    // 打开输出上下文
    ofmtCtx = openOutputCtx(outFilename, ifmtCtx, pH264CodecCtx);
    if (!ofmtCtx) {
        cerr << "Fail to open output format context!\n";
        goto end;
    }

    // 分配缓存空间
    pFrameRaw = av_frame_alloc();
    pFrameCopy = av_frame_alloc();
    pFrameYUV = av_frame_alloc();
    pPkt = av_packet_alloc();

    pFrameCopy->width = pRawCodecCtx->width;
    pFrameCopy->height = pRawCodecCtx->height;
    pFrameCopy->format = pRawCodecCtx->pix_fmt;
    ret = av_frame_get_buffer(pFrameCopy, 16);
    if (ret != 0) {
        cerr << "pFrameCopy->data buffer init Failed!\n";
        goto end;
    }

    ret = av_image_alloc(pFrameYUV->data, pFrameYUV->linesize,
                   pH264CodecCtx->width, pH264CodecCtx->height, pH264CodecCtx->pix_fmt, 1);
    if (ret < 0) {
        cerr << "Fail to allocate memory for pFrameYUV->data\n";
        goto end;
    } else {
        cout << "Allocate memory for pFrameYUV->data: " << ret << " bytes.\n";
    }

    // RawVideo与H.264输入之间的转换
    pImgConvertCtx = sws_getContext(
                     pRawCodecCtx->width, pRawCodecCtx->height, pRawCodecCtx->pix_fmt, 
                     pH264CodecCtx->width, pH264CodecCtx->height, pH264CodecCtx->pix_fmt,
                     SWS_BILINEAR, NULL, NULL, NULL);

    // 循环采集、编码、推流
    // for (size_t cameraFrame = 0; cameraFrame < exeTime * 30; cameraFrame++) {
    for (size_t cameraFrame = 0; stopRequested() == false; cameraFrame++) {

        // 从摄像头读取一帧数据
        ret = av_read_frame(ifmtCtx, pPkt);
        if (ret < 0) {
            cerr << "Fail to read from camera!\n";
            break;
        }

        if (cameraFrame % 6 == 0) {
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
                    } else {
                        if (frameIndex % 100 == 0)
                            cout << "Send " << std::setw(5) << frameIndex << " packet successfully!\n";
                    }
                }
                else {
                    cout << "Waiting for H264 encoder...\n";
                }
            }
        }
        av_packet_unref(pPkt);
    }
    // av_write_trailer(ofmtCtx);

end:
    avformat_close_input(&ifmtCtx);

    if (pFrameYUV->data) {
        av_freep(&pFrameYUV->data[0]);
    }

    av_packet_free(&pPkt);
    av_frame_free(&pFrameRaw);
    av_frame_free(&pFrameCopy);

    /* close output */
    if (ofmtCtx && !(ofmtCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&ofmtCtx->pb);
    }
    // avformat_free_context(ofmtCtx);   // 调用该函数会内存错误，原因不详

    if (ret < 0 && ret != AVERROR_EOF) {
        cerr << "Error occurred\n";
    }

    cout << "VideoPublisher exit!" << endl;
}

/* VideoRelayer */

VideoRelayer::VideoRelayer()
{

}

VideoRelayer::VideoRelayer(char pullAddr[])
{

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
    AVFormatContext* ifmtCtx = nullptr;
    AVDictionary* inputOptions = nullptr;

    if (strstr(inFilename, "rtsp://")) {
        av_dict_set(&inputOptions, "rtsp_transport", "tcp", 0);
    }

    if ((ret = avformat_open_input(&ifmtCtx, inFilename, 0, &inputOptions)) < 0) {
        cerr << "Could not open input file.\n";
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
    int argc = 3;
    char argv[][64] = { "./uav_main", "rtsp://192.168.2.101:8554/vs01", "rtsp://192.168.15.200:8554/vs01"};

    if (argc < 3) {
        printf("Usage: %s <pull_addr> <republish_addr>\n"
               "e.g. %s rtsp://192.168.2.101:8554/vs101 rtsp://192.168.15.200:8554/vs101\n"
               , argv[0], argv[0]);
        exit(1);
    }

    strcpy(inFilename, argv[1]);
    strcpy(outFilename, argv[2]);
    cout << "INPUT: " << inFilename << "\nOUTPUT: " << outFilename << "\n";

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

    // 丢弃最初的3帧
    for (int i = 0; i < 3; i++) {
        ret = av_read_frame(ifmtCtx, &pkt);
        av_packet_unref(&pkt);
    }

    // while (1) {
    for (size_t i = 0; stopRequested() == false; i++) {
        AVStream *inStream, *outStream;
        // Get an AVPacket
        ret = av_read_frame(ifmtCtx, &pkt);
        if (ret < 0)
            break;

        if (!firstPtsIsSet) {
            firstPts = pkt.pts;
            firstDts = pkt.dts;
            firstPtsIsSet = true;
        }

        inStream = ifmtCtx->streams[pkt.stream_index];
        outStream = ofmtCtx->streams[pkt.stream_index];

        /* copy packet */
        // Convert PTS/DTS
        pkt.pts -= firstPts;
        pkt.dts -= firstDts;
        av_packet_rescale_ts(&pkt, inStream->time_base, outStream->time_base);
        pkt.pos = -1;

        // Print to Screen
        if (pkt.stream_index == videoIndex) {
            frameIndex++;

            #if USE_H264BSF
            av_bitstream_filter_filter(h264bsfc, inStream->codec, NULL, &pkt.data, &pkt.size, pkt.data, pkt.size, 0);
            #endif
        }

        ret = av_interleaved_write_frame(ofmtCtx, &pkt);
        if (ret < 0) {
            av_make_error_string(errmsg, sizeof(errmsg), ret);
            cerr << "Relay packet failed: " << errmsg << '\n';
        } else {
            if (frameIndex % 100 == 0)
                cout << "Relay " << std::setw(5) << frameIndex << " packet successfully!\n";
        }

        av_packet_unref(&pkt);
    }

#if USE_H264BSF
    av_bitstream_filter_close(h264bsfc);
#endif

    // Write file trailer
    // av_write_trailer(ofmtCtx);

end:
    avformat_close_input(&ifmtCtx);

    /* close output */
    if (ofmtCtx && !(ofmt->flags & AVFMT_NOFILE))
        avio_close(ofmtCtx->pb);
    // avformat_free_context(ofmtCtx);

    if (ret < 0 && ret != AVERROR_EOF) {
        cerr << "Error occurred.\n";
        return;
    }

    return;
}