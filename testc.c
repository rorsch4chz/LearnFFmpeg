//
// Created by rorschach on 2023/11/14.
//

#include <unistd.h>
#include "testc.h"
#include <string.h>

/**
 * 音频采集 -> 重采样 -> 编码
 *
 */

void encode(AVCodecContext *c_ctx, AVFrame *frame, AVPacket *pkt, FILE *out_file) {
    int ret;
    //将数据送编码器
    ret = avcodec_send_frame(c_ctx, frame);
    //如果ret>=0,说明数据设置成功
    while (ret >= 0) {
        //获取编码后的音频数据，如果成功，需要重复获取，直到失败为止。
        /*
        我们向编码器送一帧音频帧数据的时候，编码器不一定就会马上返回一个AVPacket音频帧。
        有可能是我们送了很多帧音频数据后，编码器才会返回一个编码好的AVPacket音频帧。也有
        可能同时返回多个编码好的AVPacket音频帧。

        参数1：编码器上下文
        参数2：编码器编码后的音频帧数据
        */
        ret = avcodec_receive_packet(c_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_log(NULL, AV_LOG_DEBUG, "Error, EAGAIN or EOF\n");
            return;
        } else if (ret < 0) {
            av_log(NULL, AV_LOG_DEBUG, "Error, encoding audio frame\n");
            exit(-1);
        }

        //write file
        fwrite(pkt->data, 1, pkt->size, out_file);
        fflush(out_file);
    }
}

//打开编码器
AVCodecContext *open_codec() {
//    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    AVCodec *codec = avcodec_find_decoder_by_name("libfdk_aac");

    //创建codec上下文
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);

    codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;     //输入音频的采样大小
    codec_ctx->channel_layout = AV_CH_LAYOUT_MONO; //输入音频的channel layout
    codec_ctx->channels = 1;                        //输入音频的channel 个数
    codec_ctx->sample_rate = 44100;                 //输入音频的采样率
    //只有当 bit_rate属性为0的时候  ffmpeg才会查找 profile属性值，并确认是哪个编码器，并会设置他们各自对应的默认的码率。
    codec_ctx->bit_rate = 0;        //AAC_LC: 128K, AAC_HE: 64K, AAC_HE_V2: 32K
    codec_ctx->profile = FF_PROFILE_AAC_HE_V2;

    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        av_log(NULL, AV_LOG_DEBUG, "Open codec fail！\n");
        return NULL;
    }
    return codec_ctx;
}

//重采样上下文
SwrContext *init_swr() {
    //重采样
    SwrContext *swr_ctx = NULL;
    swr_ctx = swr_alloc_set_opts(NULL,                             //ctx
                                 AV_CH_LAYOUT_MONO,     //输出channel布局
                                 AV_SAMPLE_FMT_S16,      //输出采样格式
                                 44100,                 //采样率

                                 AV_CH_LAYOUT_MONO,       //输入channel布局
                                 AV_SAMPLE_FMT_FLT,      //输入采样格式
                                 44100,                 //输入采样率
                                 0,
                                 NULL);
    if (!swr_ctx) {
        av_log(NULL, AV_LOG_DEBUG, "init_swr fail\n");
    }

    if (swr_init(swr_ctx) < 0) {
        av_log(NULL, AV_LOG_DEBUG, "init_swr fail\n");
    }
    return swr_ctx;
}

void record() {
    int ret;
    char errors[1024];
    AVFormatContext *fmt_ctx = NULL;

    int count = 0;
    AVPacket pkt;

    //[[video device]:[audio device]]
    char devicename[] = ":0";

    //重采样缓冲区
    uint8_t **src_data = NULL;
    int src_linesize = 0;

    uint8_t **dst_data = NULL;
    int dst_linesize = 0;

    av_log_set_level(AV_LOG_DEBUG);

    //register audio devices  注册设备
    avdevice_register_all();

    //get format  获取格式
    AVInputFormat *input_format = av_find_input_format("avfoundation");

    if (NULL == input_format) {
        printf("av_find_input_format fail\n");
        return;
    }

    // 打开设备 url可以是本地地址或网络地址
    ret = avformat_open_input(&fmt_ctx, devicename, input_format, NULL);

    if (ret < 0) {
        av_strerror(ret, errors, 1024);
        printf("Failed to open audio device, [%d] %s\n", ret, errors);
        return;
    }

    //create file
//    char *out = "/Users/bz/Desktop/audio.pcm";
    char *out = "/Users/bz/Desktop/audio.aac";
    FILE *out_file = fopen(out, "wb+");

    if (out_file == NULL) {
        return;
    }

    //编码上下文
    AVCodecContext *c_ctx = open_codec();

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        av_log(NULL, AV_LOG_DEBUG, "frame alloc fail \n");
    }
    frame->nb_samples = 512;                    //单通道一个音频帧的采样数
    frame->format = AV_SAMPLE_FMT_S16;          //每个采样的大小
    frame->channel_layout = AV_CH_LAYOUT_MONO;  //channel layout
    av_frame_get_buffer(frame, 0);
    if (!frame->buf[0]) {
        av_log(NULL, AV_LOG_DEBUG, "frame get buffer fail \n");
    }

    AVPacket *newpkt = av_packet_alloc();
    if (!newpkt) {
        av_log(NULL, AV_LOG_DEBUG, "packet alloc fail \n");
    }

    //重采样上下文
    SwrContext *swr_ctx = init_swr();

    //创建输入缓冲区
    av_samples_alloc_array_and_samples(&src_data,       //输出缓冲区地址
                                       &src_linesize,      //缓冲区大小
                                       1,               //通道个数
                                       512,     // 单通道采样个数  2048/4/1 = 512
                                       AV_SAMPLE_FMT_FLT,   //采样格式
                                       0);


    //创建输出缓冲区
    av_samples_alloc_array_and_samples(&dst_data,
                                       &dst_linesize,
                                       1,
                                       512,
                                       AV_SAMPLE_FMT_S16,
                                       0);

    while ((ret = av_read_frame(fmt_ctx, &pkt)) == 0 || ret == -35) {
        //设备暂未准备好，延时等待一下。
        if (ret == -35) {
            usleep(100);
            continue;
        }
        if (count > 200) {
            break;
        }

        //内存拷贝，按字节拷贝
        memcpy((void *) src_data[0], (void *) pkt.data, pkt.size);

        //重采样
        swr_convert(swr_ctx,                    //上下文
                    dst_data,                  //输出结果缓冲区
                    512,                    //输出每个通道的采样数
                    (const uint8_t **) src_data,  //输入缓冲区
                    512);                    //输入单个通道的采样数

        //将重采样的数据拷贝到frame中
        memcpy((void *) frame->data[0], dst_data[0], dst_linesize);

        //encode 编码
        encode(c_ctx, frame, newpkt, out_file);

        //write file
//        fwrite(pkt.data, pkt.size, 1, out_file);
//        fwrite(dst_data[0], 1, dst_linesize, out_file);
//        fflush(out_file);
        count++;

        av_log(NULL, AV_LOG_INFO, "packet size is %d(%p), count=%d \n", pkt.size, pkt.data, count);
        av_packet_unref(&pkt);
    }

    //强制将编码器缓冲区中的音频进行编码输出
    encode(c_ctx, NULL, newpkt, out_file);

    //close file
    fclose(out_file);

    //释放输入输出缓冲区
    if (src_data) {
        av_free(&src_data[0]);
    }
    av_freep(src_data);
    if (dst_data) {
        av_free(&dst_data[0]);
    }
    av_freep(dst_data);
    //释放重采样上下文
    swr_free(&swr_ctx);

    //释放AVFrame和AVPacket
    av_frame_free(&frame);
    av_packet_free(&newpkt);

    //release ctx
    avformat_close_input(&fmt_ctx);

    av_log(NULL, AV_LOG_DEBUG, "Finish！\n");
}

