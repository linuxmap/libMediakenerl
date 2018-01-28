
#include <mfx/mfxvideo.h>
#include <stdlib.h>

#include "libavutil/dict.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavcodec/qsv.h"

#include "ffmpeg.h"

char *qsv_device = NULL;

static int mk_qsv_get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    mk_input_stream_t*ist = s->opaque;

    return av_hwframe_get_buffer(ist->hw_frames_ctx, frame, 0);
}

static void mk_qsv_uninit(AVCodecContext *s)
{
    mk_input_stream_t *ist = s->opaque;
    av_buffer_unref(&ist->hw_frames_ctx);
}

static int mk_qsv_device_init(InputStream *ist)
{
    int err;
    AVDictionary *dict = NULL;

    if (qsv_device) {
        err = av_dict_set(&dict, "child_device", qsv_device, 0);
        if (err < 0)
            return err;
    }

    err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_QSV,
                                 ist->hwaccel_device, dict, 0);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error creating a QSV device\n");
        goto err_out;
    }

err_out:
    if (dict)
        av_dict_free(&dict);

    return err;
}

int mk_qsv_init(AVCodecContext *s)
{
    mk_input_stream_t *ist = s->opaque;
    AVHWFramesContext *frames_ctx;
    AVQSVFramesContext *frames_hwctx;
    int ret;

    if (!hw_device_ctx) {
        ret = mk_qsv_device_init(ist);
        if (ret < 0)
            return ret;
    }

    av_buffer_unref(&ist->hw_frames_ctx);
    ist->hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!ist->hw_frames_ctx)
        return AVERROR(ENOMEM);

    frames_ctx   = (AVHWFramesContext*)ist->hw_frames_ctx->data;
    frames_hwctx = frames_ctx->hwctx;

    frames_ctx->width             = FFALIGN(s->coded_width,  32);
    frames_ctx->height            = FFALIGN(s->coded_height, 32);
    frames_ctx->format            = AV_PIX_FMT_QSV;
    frames_ctx->sw_format         = s->sw_pix_fmt;
    frames_ctx->initial_pool_size = 64;
    frames_hwctx->frame_type      = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;

    ret = av_hwframe_ctx_init(ist->hw_frames_ctx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error initializing a QSV frame pool\n");
        return ret;
    }

    ist->hwaccel_get_buffer = mk_qsv_get_buffer;
    ist->hwaccel_uninit     = mk_qsv_uninit;

    return 0;
}

