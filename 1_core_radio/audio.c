#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
#include <sys/socket.h>

// Fonction "static" car elle n'est utile que dans ce fichier
static int write_to_shout(void *opaque, uint8_t *buf, int buf_size) {
    shout_t *shout = (shout_t *)opaque;
    if (shout_send(shout, buf, buf_size) != SHOUTERR_SUCCESS) return -1;
    shout_sync(shout);
    return buf_size;
}

int play_file(const char *filepath, shout_t *shout) {
    AVFormatContext *in_ctx = NULL;
    if (avformat_open_input(&in_ctx, filepath, NULL, NULL) < 0) return -1;
    avformat_find_stream_info(in_ctx, NULL);

    int st_idx = av_find_best_stream(in_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (st_idx < 0) { avformat_close_input(&in_ctx); return -1; }

    const AVCodec *dec = avcodec_find_decoder(in_ctx->streams[st_idx]->codecpar->codec_id);
    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dec_ctx, in_ctx->streams[st_idx]->codecpar);
    avcodec_open2(dec_ctx, dec, NULL);

    const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_VORBIS);
    AVCodecContext *enc_ctx = avcodec_alloc_context3(enc);
    enc_ctx->bit_rate = 128000;
    enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    enc_ctx->sample_rate = 44100;
    enc_ctx->channels = 2;
    enc_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
    enc_ctx->time_base = (AVRational){1, 44100};
    avcodec_open2(enc_ctx, enc, NULL);

    AVFormatContext *out_ctx;
    avformat_alloc_output_context2(&out_ctx, NULL, "ogg", NULL);
    uint8_t *avio_buf = av_malloc(4096);
    AVIOContext *avio_ctx = avio_alloc_context(avio_buf, 4096, 1, shout, NULL, write_to_shout, NULL);
    out_ctx->pb = avio_ctx;
    out_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

    AVStream *out_stream = avformat_new_stream(out_ctx, NULL);
    avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
    if (avformat_write_header(out_ctx, NULL) < 0) return -1;

    int64_t in_ch_layout = dec_ctx->channel_layout ? (int64_t)dec_ctx->channel_layout : av_get_default_channel_layout(dec_ctx->channels);
    SwrContext *swr = swr_alloc_set_opts(NULL, 
        AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, 44100,
        in_ch_layout, dec_ctx->sample_fmt, dec_ctx->sample_rate, 0, NULL);
    swr_init(swr);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    
    AVAudioFifo *fifo = av_audio_fifo_alloc(enc_ctx->sample_fmt, enc_ctx->channels, 1);
    int frame_size = enc_ctx->frame_size > 0 ? enc_ctx->frame_size : 1024;
    int pts_counter = 0;

    while (av_read_frame(in_ctx, pkt) >= 0) {
        if (pkt->stream_index == st_idx && avcodec_send_packet(dec_ctx, pkt) >= 0) {
            while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
                int out_samples = swr_get_out_samples(swr, frame->nb_samples);
                uint8_t **res_data = NULL;
                av_samples_alloc_array_and_samples(&res_data, NULL, enc_ctx->channels, out_samples, enc_ctx->sample_fmt, 0);
                int real_out = swr_convert(swr, res_data, out_samples, (const uint8_t **)frame->data, frame->nb_samples);

                if (av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + real_out) < 0) {
                    fprintf(stderr, "Erreur : Impossible d'agrandir la mémoire de la FIFO.\n");
                    av_freep(&res_data[0]);
                    av_freep(&res_data);
                    break;
                }
                av_audio_fifo_write(fifo, (void **)res_data, real_out);
                av_freep(&res_data[0]);
                av_freep(&res_data);

                while (av_audio_fifo_size(fifo) >= frame_size) {
                    AVFrame *out_frame = av_frame_alloc();
                    out_frame->nb_samples = frame_size;
                    out_frame->channel_layout = enc_ctx->channel_layout;
                    out_frame->format = enc_ctx->sample_fmt;
                    out_frame->sample_rate = enc_ctx->sample_rate;
                    av_frame_get_buffer(out_frame, 0);

                    av_audio_fifo_read(fifo, (void **)out_frame->data, frame_size);
                    out_frame->pts = pts_counter;
                    pts_counter += frame_size;

                    if (avcodec_send_frame(enc_ctx, out_frame) >= 0) {
                        AVPacket *enc_pkt = av_packet_alloc();
                        while (avcodec_receive_packet(enc_ctx, enc_pkt) >= 0) {
                            av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
                            enc_pkt->stream_index = out_stream->index;
                            av_interleaved_write_frame(out_ctx, enc_pkt);
                            av_packet_unref(enc_pkt);
                        }
                        av_packet_free(&enc_pkt);
                    }
                    av_frame_free(&out_frame);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    while (av_audio_fifo_size(fifo) > 0) {
        int chunk = av_audio_fifo_size(fifo) < frame_size ? av_audio_fifo_size(fifo) : frame_size;
        AVFrame *out_frame = av_frame_alloc();
        out_frame->nb_samples = chunk;
        out_frame->channel_layout = enc_ctx->channel_layout;
        out_frame->format = enc_ctx->sample_fmt;
        out_frame->sample_rate = enc_ctx->sample_rate;
        av_frame_get_buffer(out_frame, 0);

        av_audio_fifo_read(fifo, (void **)out_frame->data, chunk);
        out_frame->pts = pts_counter;
        pts_counter += chunk;

        if (avcodec_send_frame(enc_ctx, out_frame) >= 0) {
            AVPacket *enc_pkt = av_packet_alloc();
            while (avcodec_receive_packet(enc_ctx, enc_pkt) >= 0) {
                av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
                enc_pkt->stream_index = out_stream->index;
                av_interleaved_write_frame(out_ctx, enc_pkt);
                av_packet_unref(enc_pkt);
            }
            av_packet_free(&enc_pkt);
        }
        av_frame_free(&out_frame);
    }

    av_write_trailer(out_ctx);

    av_audio_fifo_free(fifo);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_ctx);
    avio_context_free(&avio_ctx);
    avformat_free_context(out_ctx);

    return 0;
}

int play_live(int sock, shout_t *shout) {
    const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_VORBIS);
    AVCodecContext *enc_ctx = avcodec_alloc_context3(enc);
    enc_ctx->bit_rate = 128000;
    enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    enc_ctx->sample_rate = 44100;
    enc_ctx->channels = 2;
    enc_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
    enc_ctx->time_base = (AVRational){1, 44100};
    avcodec_open2(enc_ctx, enc, NULL);

    AVFormatContext *out_ctx;
    avformat_alloc_output_context2(&out_ctx, NULL, "ogg", NULL);
    uint8_t *avio_buf = av_malloc(4096);
    AVIOContext *avio_ctx = avio_alloc_context(avio_buf, 4096, 1, shout, NULL, write_to_shout, NULL);
    out_ctx->pb = avio_ctx;
    out_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

    AVStream *out_stream = avformat_new_stream(out_ctx, NULL);
    avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
    if (avformat_write_header(out_ctx, NULL) < 0) return -1;

    SwrContext *swr = swr_alloc_set_opts(NULL, 
        AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, 44100,
        AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 44100, 0, NULL);
    swr_init(swr);

    AVAudioFifo *fifo = av_audio_fifo_alloc(enc_ctx->sample_fmt, enc_ctx->channels, 1);
    int frame_size = enc_ctx->frame_size > 0 ? enc_ctx->frame_size : 1024;
    int pts_counter = 0;

    int16_t pcm_buffer[4096]; 
    int bytes_read;

    while ((bytes_read = recv(sock, pcm_buffer, sizeof(pcm_buffer), 0)) > 0) {
        int num_samples = bytes_read / 4; 

        uint8_t const *in_data[1] = { (uint8_t *)pcm_buffer };
        uint8_t **res_data = NULL;
        av_samples_alloc_array_and_samples(&res_data, NULL, enc_ctx->channels, num_samples, enc_ctx->sample_fmt, 0);
        
        int real_out = swr_convert(swr, res_data, num_samples, in_data, num_samples);
        
        if (av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + real_out) < 0) {
            fprintf(stderr, "⚠️ Erreur critique : Mémoire insuffisante (Mode LIVE).\n");
            av_freep(&res_data[0]);
            av_freep(&res_data);
            break; 
        }
        
        av_audio_fifo_write(fifo, (void **)res_data, real_out);
        av_freep(&res_data[0]);
        av_freep(&res_data);

        while (av_audio_fifo_size(fifo) >= frame_size) {
            AVFrame *out_frame = av_frame_alloc();
            out_frame->nb_samples = frame_size;
            out_frame->channel_layout = enc_ctx->channel_layout;
            out_frame->format = enc_ctx->sample_fmt;
            out_frame->sample_rate = enc_ctx->sample_rate;
            av_frame_get_buffer(out_frame, 0);

            av_audio_fifo_read(fifo, (void **)out_frame->data, frame_size);
            out_frame->pts = pts_counter;
            pts_counter += frame_size;

            if (avcodec_send_frame(enc_ctx, out_frame) >= 0) {
                AVPacket *enc_pkt = av_packet_alloc();
                while (avcodec_receive_packet(enc_ctx, enc_pkt) >= 0) {
                    av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
                    enc_pkt->stream_index = out_stream->index;
                    av_interleaved_write_frame(out_ctx, enc_pkt);
                    av_packet_unref(enc_pkt);
                }
                av_packet_free(&enc_pkt);
            }
            av_frame_free(&out_frame);
        }
    }

    av_write_trailer(out_ctx);

    av_audio_fifo_free(fifo);
    swr_free(&swr);
    avcodec_free_context(&enc_ctx);
    avio_context_free(&avio_ctx);
    avformat_free_context(out_ctx);

    return 0;
}
