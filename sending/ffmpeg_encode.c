#include "include/libavcodec/avcodec.h"
#include "include/libavutil/avutil.h"
#include "include/libavutil/opt.h"
#include "include/libavutil/imgutils.h"
#include "include/libswscale/swscale.h"

#include "ffmpeg_encode.h"
#include "owvr_packet.h"

#define WIDTH 1920
#define HEIGHT 1080

// #define USE_YUV

typedef struct ffmpeg_encode_context
{
    AVCodecContext *enc_ctx;
    AVFrame *frame;
    int idx;
    struct SwsContext *rgb_to_yuv_ctx;
} ffmpeg_encode_context;

#ifdef USE_YUV
static int const srcstride[1] = {WIDTH * 3};
#else
static int const srcstride[1] = {WIDTH * 4};
#endif

static int ffmpeg_initialize(struct owvr_ctx *ctx)
{
    int ret;
    if (ctx->enc_priv != NULL)
    {
        free(ctx->enc_priv);
    }
    ffmpeg_encode_context *e = calloc(1, sizeof(ffmpeg_encode_context));
    ctx->enc_priv = e;
    avcodec_register_all();
    AVCodec *enc = avcodec_find_encoder_by_name("h264_nvenc");
    if (enc == NULL)
    {
        printf("couldn't find encoder\n");
        return -1;
    }
    e->enc_ctx = avcodec_alloc_context3(enc);
    e->enc_ctx->width = WIDTH;
    e->enc_ctx->height = HEIGHT;
    e->enc_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    e->enc_ctx->framerate = (AVRational){60, 1};
    e->enc_ctx->time_base = (AVRational){1, 60};
    e->enc_ctx->bit_rate = 15000000;
    e->enc_ctx->gop_size = 20;
    e->enc_ctx->max_b_frames = 0;
#ifdef USE_YUV
    e->enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
#else
    e->enc_ctx->pix_fmt = AV_PIX_FMT_0BGR32;
#endif
    ret = av_opt_set(e->enc_ctx->priv_data, "preset", "llhq", 0);
    ret = av_opt_set(e->enc_ctx->priv_data, "rc", "cbr_ld_hq", 0);
    ret = av_opt_set_int(e->enc_ctx->priv_data, "zerolatency", 1, 0);
    ret = av_opt_set_int(e->enc_ctx->priv_data, "delay", 0, 0);
    // "Instantaneous Decoder Refresh", so that when we generate I-frames mid-sequence, they will reset the reference picture buffer,
    // allowing the client to join and immediately begin rendering when it requests I-frames.
    ret = av_opt_set(e->enc_ctx->priv_data, "forced-idr", "true", 0);
    ret = av_opt_set_int(e->enc_ctx->priv_data, "temporal-aq", 1, 0);
    ret = av_opt_set_int(e->enc_ctx->priv_data, "spatial-aq", 1, 0);
    ret = av_opt_set_int(e->enc_ctx->priv_data, "aq-strength", 15, 0);

    ret = avcodec_open2(e->enc_ctx, enc, NULL);
    if (ret < 0)
    {
        printf("avcodec_open2 failed\n");
        return -1;
    }

    e->frame = av_frame_alloc();
    e->frame->format = e->enc_ctx->pix_fmt;
    e->frame->width = e->enc_ctx->width;
    e->frame->height = e->enc_ctx->height;

    ret = av_image_alloc(e->frame->data, e->frame->linesize, e->enc_ctx->width, e->enc_ctx->height, e->enc_ctx->pix_fmt, 32);
    if (ret < 0)
    {
        printf("av_image_alloc() failed\n");
        return -1;
    }
#ifdef USE_YUV
    e->rgb_to_yuv_ctx = sws_getContext(WIDTH, HEIGHT, AV_PIX_FMT_RGB24, WIDTH, HEIGHT, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
#else
    e->rgb_to_yuv_ctx = sws_getContext(WIDTH, HEIGHT, AV_PIX_FMT_RGB0, WIDTH, HEIGHT, AV_PIX_FMT_0BGR32, SWS_FAST_BILINEAR, NULL, NULL, NULL);
#endif
    return 0;
}

static int ffmpeg_process_frame(struct owvr_ctx *ctx, struct owvr_packet *pkt)
{
    int ret;
    ffmpeg_encode_context *e = ctx->enc_priv;
    AVCodecContext *enc_ctx = e->enc_ctx;
    AVFrame *frame = e->frame;
    AVPacket packet;
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    ret = avcodec_receive_packet(e->enc_ctx, &packet);
    if (ret >= 0)
    {
        memcpy(pkt->data, packet.data, packet.size);
        pkt->size = packet.size;
        return 1;
    }
    else if (ret != -11)
    {
        printf("avcodec_receive_packet error: %d\n", ret);
        return -1;
    }

    unsigned char *src = ctx->pix_buf;
    sws_scale(e->rgb_to_yuv_ctx, &src, srcstride, 0, HEIGHT, frame->data, frame->linesize);

    frame->pts = e->idx++;
    if (ctx->flag_send_iframe > 0)
    {
        frame->pict_type = AV_PICTURE_TYPE_I;
        ctx->flag_send_iframe = ~ctx->flag_send_iframe;
    }
    else
    {
        frame->pict_type = AV_PICTURE_TYPE_NONE;
        if (ctx->flag_send_iframe < 0)
        {
            ctx->flag_send_iframe++;
        }
    }

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret != 0 && ret != -11)
    {
        printf("avcodec_send_frame() failed: %d\n", ret);
        return -1;
    }

    return 0;
}
static void ffmpeg_deinitialize(struct owvr_ctx *ctx)
{
    void *fdsa = ctx;
    printf("fdsa %p\n", fdsa);
}

struct owvr_encoder ffmpeg_encode = {
    .init = ffmpeg_initialize,
    .process_frame = ffmpeg_process_frame,
    .deinit = ffmpeg_deinitialize,
};