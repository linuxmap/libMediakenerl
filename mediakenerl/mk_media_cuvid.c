
#include "libavutil/hwcontext.h"
#include "libavutil/pixdesc.h"

#include "mk_media_worker.h"


static void mk_cuvid_uninit(AVCodecContext *avctx)
{
    mk_input_stream_t*ist = avctx->opaque;

    av_buffer_unref(&ist->hw_frames_ctx);
}

int mk_cuvid_init(AVCodecContext *avctx)
{
    mk_input_stream_t  *ist = avctx->opaque;
    AVHWFramesContext *frames_ctx;
    int ret;

    av_log(avctx, AV_LOG_VERBOSE, "Initializing cuvid hwaccel\n");

    if (!hw_device_ctx) {
        ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA,
                                     ist->hwaccel_device, NULL, 0);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error creating a CUDA device\n");
            return ret;
        }
    }

    av_buffer_unref(&ist->hw_frames_ctx);
    ist->hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!ist->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "Error creating a CUDA frames context\n");
        return AVERROR(ENOMEM);
    }

    frames_ctx = (AVHWFramesContext*)ist->hw_frames_ctx->data;

    frames_ctx->format = AV_PIX_FMT_CUDA;
    frames_ctx->sw_format = avctx->sw_pix_fmt;
    frames_ctx->width = avctx->width;
    frames_ctx->height = avctx->height;

    av_log(avctx, AV_LOG_DEBUG, "Initializing CUDA frames context: sw_format = %s, width = %d, height = %d\n",
           av_get_pix_fmt_name(frames_ctx->sw_format), frames_ctx->width, frames_ctx->height);

    ret = av_hwframe_ctx_init(ist->hw_frames_ctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing a CUDA frame pool\n");
        return ret;
    }

    ist->hwaccel_uninit = mk_cuvid_uninit;

    return 0;

}

