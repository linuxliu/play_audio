#include <iostream>
#include <cstdio>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavfilter/avfilter.h>
#include <libavutil/audio_fifo.h>
#include <libavresample/avresample.h>
#include <libavutil/opt.h>
#include <SDL2/SDL.h>
}
using namespace std;

typedef struct Global_config_st {
    int paused;
    int64_t current_dts;
    
}Global_config;

typedef struct Stream_holder_st {
    int audio_stream_index;
    int video_stream_index;
    int subtitle_stream_index;
    
    AVStream *audio_stream;
    AVStream *video_stream;
    AVStream *subtitle_stream;
    
    AVFormatContext *format_ctx;
    AVCodecContext *codec_ctx;
    AVCodec *codec;
    AVDictionary *format_options;
    AVDictionary *codec_options;
    
    char file_name[1024];
    
}Stream_holder;

#define FILE_PATH "http://rs.ajmide.com/tr_67/67.m3u8"
//#define FILE_PATH "/Users/liuhaiqiang/Desktop/test.m4a"
#define FILE_NAME "./test.raw"
#define OUT_CHANNELS 2
#define OUT_SAMPLE_RATE 44100
#define OUT_SAMPLE_FMT AV_SAMPLE_FMT_S16
#define MAX_DESIRED_NB_SAMPLE 8192
int need_resample(AVFrame *in_frame)
{
    int ret = 0;
    if (in_frame->channel_layout != av_get_default_channel_layout(OUT_CHANNELS)
        || in_frame->sample_rate != OUT_SAMPLE_RATE
        || in_frame->format != OUT_SAMPLE_FMT
        ) {
        ret = 1;
    }
    
    return ret;
    
}


int init_out_frame(AVFrame **frame, int frame_size)
{
    int ret = 0;
    *frame = av_frame_alloc();
    if (NULL == *frame) {
        ret = 1;
        return ret;
    }
    (*frame)->nb_samples = frame_size;
    (*frame)->format = OUT_SAMPLE_FMT;
    (*frame)->channel_layout = av_get_default_channel_layout(OUT_CHANNELS);
    (*frame)->sample_rate = OUT_SAMPLE_RATE;
    ret = av_frame_get_buffer(*frame, 0);
    if (ret != 0) {
        cerr << "av_frame_get_buffer ERROR" << endl;
        av_frame_free(frame);
    }
    return ret;
}

void  fill_audio(void *udata,Uint8 *stream,int len){
    
    cout << "fill_audio len " << len << endl;
    AVAudioFifo * fifo = (AVAudioFifo*)udata;
    int nb_sample = av_audio_fifo_size(fifo);
    cout << "av_audio_fifo_size " << nb_sample << endl;
    if (nb_sample <= 0) {
        return;
    }
    else if (nb_sample >= MAX_DESIRED_NB_SAMPLE) {
        nb_sample = MAX_DESIRED_NB_SAMPLE;
    }
    len = av_samples_get_buffer_size(NULL, 2, nb_sample, AV_SAMPLE_FMT_S16, 0);
    SDL_memset(stream, 0, len);
    av_audio_fifo_read(fifo, (void**)(&stream), nb_sample);
    
}


int main()
{
    avformat_network_init();
    av_register_all();
    avcodec_register_all();
    avfilter_register_all();
    SDL_Init(SDL_INIT_AUDIO);
//    FILE *fp = fopen(FILE_NAME, "wb+");
//    if (NULL == fp) {
//        cerr << "can not open " << FILE_NAME <<endl;
//        return -1;
//    }
    
    AVFormatContext *avfmtctx  = NULL;
    int ret = 0;
    
    int audio_stream_index = -1;
    int i;
    
    AVCodecContext *avcctx;
    AVCodec *avc;
    AVStream *audio_stream;
    
    AVAudioFifo * audio_fifo;
    
    AVPacket *packet;
    
    AVFrame *frame;
    AVAudioResampleContext *avresamplectx = NULL;
    
    audio_fifo = av_audio_fifo_alloc(OUT_SAMPLE_FMT, 2, 1);
    
    if (NULL == audio_fifo) {
        goto end;
    }
    
    SDL_AudioSpec desired;
    desired.channels = 2;
    desired.format = AUDIO_S16SYS;
    desired.freq = 44100;
    desired.samples = MAX_DESIRED_NB_SAMPLE;
    desired.callback = fill_audio;
    desired.userdata = (void*)audio_fifo;
    
    SDL_OpenAudio(&desired, NULL);
    SDL_PauseAudio(0);
    
    
    ret = avformat_open_input(&avfmtctx, FILE_PATH, NULL, NULL);
    if (ret < 0) {
        cerr<< "avformat_open_input error" << endl;
        return ret;
    }
    
    ret = avformat_find_stream_info(avfmtctx, NULL);
    if (ret <  0) {
        cerr << "avformat_find_stream_info ERROR" << endl;
        goto end;
    }
    
    av_dump_format(avfmtctx, 0, FILE_PATH, 0);
    
    
    for (i=0; i< avfmtctx->nb_streams; i++) {
        if (avfmtctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
        
    }
    if (audio_stream_index < 0) {
        cerr << "can not find audio_stream_index ERROR"<< endl;
        ret = -1;
        goto end;
    }
    
    cout<< "audio_stream_index: " << audio_stream_index << endl;
    
    audio_stream = avfmtctx->streams[audio_stream_index];
    
    avc = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    
    if (avc == NULL) {
        cerr << "avcodec_find_decoder ERROR " << endl;
        goto end;
    }
    
    avcctx = avcodec_alloc_context3(avc);
    
    if (NULL == avcctx) {
        cerr << "avcodec_alloc_context3 ERROR " << endl;
        goto end;
    }
    
    avcodec_parameters_to_context(avcctx, audio_stream->codecpar);
    
    ret = avcodec_open2(avcctx, avc, NULL);
    if (ret < 0) {
        cerr << "avcodec_open2 ERROR " << endl;
        goto end;
    }
    
    packet = av_packet_alloc();
    if (NULL == packet) {
        cerr << "av_packet_alloc ERROR " << endl;
        goto end;
    }
    av_init_packet(packet);
    packet->data = NULL;
    packet->size = 0;
    frame = av_frame_alloc();
    
    
    
    
    while (1) {
        SDL_Delay(10);
        ret = av_read_frame(avfmtctx, packet);
        
        if (ret == AVERROR_EOF) {
            cout << "av_read_frame EOF "<<endl;
            if (packet->buf == NULL) {
                cout << "packet null " <<endl;
                ret = 0;
                break;
            }
            else {
                cout << "packet size: " << packet->size << endl;
                
            }
        }
        else if (ret < 0 ) {
            cerr << "av_read_frame ERROR " << endl;
            goto end;
        }
        
        cout << "pkt->dts:  " << packet->dts << " time:  " <<
        av_rescale_q_rnd(packet->dts, audio_stream->time_base ,AV_TIME_BASE_Q ,AV_ROUND_UP) << endl;
        
        if (packet->stream_index != audio_stream_index) {
            av_packet_unref(packet);
            continue;
        }
//        if (packet->dts > 3672064) {
//            cout << "1111  packet->dts" << endl;
//
//            //            av_packet_unref(packet);
//        }
        
        ret = avcodec_send_packet(avcctx, packet);
        if (ret == AVERROR_EOF) {
            cout << "avcodec_send_packet eof" << endl;
            break;
        }
        else if (ret != 0) {
            cerr << "avcodec_send_packet ERROR " << endl;
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);
        ret = avcodec_receive_frame(avcctx, frame);
        if (ret == AVERROR_EOF) {
            cout << "avcodec_receive_frame eof frame->nb_samples : " << frame->nb_samples << endl;
            av_frame_unref(frame);
            break;
        }
        else if (ret != 0) {
            cerr << "avcodec_receive_frame ERROR " << endl;
            av_packet_unref(packet);
            continue;
        }
        
        cout << " frame nb_samples :" << frame->nb_samples  << "frame->sample_rate :" << frame->sample_rate << endl;
        // resample
        if (need_resample(frame)) {
            if (NULL == avresamplectx) {
                avresamplectx = avresample_alloc_context();
                av_opt_set_int(avresamplectx, "in_channel_layout",  frame->channel_layout, 0);
                av_opt_set_int(avresamplectx, "out_channel_layout", AV_CH_LAYOUT_STEREO,  0);
                av_opt_set_int(avresamplectx, "in_sample_rate",     frame->sample_rate,0);
                av_opt_set_int(avresamplectx, "out_sample_rate",    44100,                0);
                av_opt_set_int(avresamplectx, "in_sample_fmt",      frame->format,   0);
                av_opt_set_int(avresamplectx, "out_sample_fmt",     AV_SAMPLE_FMT_S16,    0);
            }
            if (!avresample_is_open(avresamplectx)) {
                avresample_open(avresamplectx);
            }
            int out_samples = avresample_get_out_samples(avresamplectx, frame->nb_samples);
            int delay_out_samples = avresample_get_delay(avresamplectx);
            out_samples += delay_out_samples;
            
            AVFrame *out_frame = NULL;
            if (0 != init_out_frame(&out_frame, out_samples)) {
                cerr<< "init_out_frame ERROR" << endl;
                goto end;
            }
            ret = avresample_convert_frame(avresamplectx, out_frame, frame);
            if (0 != ret) {
                av_frame_free(&out_frame);
                cerr << "avresample_convert_frame Error" << endl;
                goto end;
            }
            av_audio_fifo_write(audio_fifo, (void **)(out_frame->data), out_samples);
            av_frame_free(&out_frame);
        }
        else {
            ret = av_audio_fifo_write(audio_fifo, (void**)frame->data, frame->nb_samples);
            cout << "av_audio_fifo_write size:" << ret << " fifo size :" << av_audio_fifo_size(audio_fifo) << endl;
        }
        av_frame_unref(frame);
    }
    
end:
    if (audio_fifo != NULL) {
        av_audio_fifo_free(audio_fifo);
    }
    if (frame != NULL) {
        av_frame_free(&frame);
    }
    //    if (NULL !=fp) {
    //        fclose(fp);
    //    }
    if (NULL != avresamplectx) {
        if (avresample_is_open(avresamplectx)) {
            avresample_close(avresamplectx);
        }
        avresample_free(&avresamplectx);
    }
    
    if (NULL != avcctx) {
        avcodec_close(avcctx);
    }
    
    if (NULL != packet) {
        av_packet_free(&packet);
    }
    if (NULL != avfmtctx) {
        avformat_close_input(&avfmtctx);
    }
    if (NULL != avcctx) {
        avcodec_free_context(&avcctx);
    }
    avformat_network_deinit();
    return ret;
}

