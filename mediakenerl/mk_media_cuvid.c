
#include "libavutil/hwcontext.h"

#include "mk_media_worker.h"

typedef struct CUVIDContext {
    AVBufferRef *hw_frames_ctx;
} CUVIDContext;

static void mk_cuvid_uninit(AVCodecContext *avctx)
{
    mk_input_stream_t*ist = avctx->opaque;
    CUVIDContext *ctx = ist->hwaccel_ctx;

    if (ctx) {
        av_buffer_unref(&ctx->hw_frames_ctx);
        av_freep(&ctx);
    }

    av_buffer_unref(&ist->hw_frames_ctx);

    ist->hwaccel_ctx = 0;
    ist->hwaccel_uninit = 0;
}

int mk_cuvid_init(AVCodecContext *avctx)
{
    mk_input_stream_t  *ist = avctx->opaque;
    CUVIDContext *ctx = ist->hwaccel_ctx;

    av_log(NULL, AV_LOG_TRACE, "Initializing cuvid hwaccel\n");

    if (!ctx) {
        av_log(NULL, AV_LOG_ERROR, "CUVID transcoding is not initialized. "
               "-hwaccel cuvid should only be used for one-to-one CUVID transcoding "
               "with no (software) filters.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

int mk_cuvid_transcode_init(mk_task_ctx_t* task,mk_output_stream_t*ost)
{
    mk_input_stream_t *ist;
    const enum AVPixelFormat *pix_fmt;
    AVHWFramesContext *hwframe_ctx;
    AVBufferRef *device_ref = NULL;
    CUVIDContext *ctx = NULL;
    int ret = 0;

    av_log(NULL, AV_LOG_TRACE, "Initializing cuvid transcoding\n");

    if (ost->source_index < 0)
        return 0;

    ist = task->input_streams[ost->source_index];

    /* check if the encoder supports CUVID */
    if (!ost->enc->pix_fmts)
        goto cancel;
    for (pix_fmt = ost->enc->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++)
        if (*pix_fmt == AV_PIX_FMT_CUDA)
            break;
    if (*pix_fmt == AV_PIX_FMT_NONE)
        goto cancel;

    /* check if the decoder supports CUVID */
    if (ist->hwaccel_id != HWACCEL_CUVID || !ist->dec || !ist->dec->pix_fmts)
        goto cancel;
    for (pix_fmt = ist->dec->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++)
        if (*pix_fmt == AV_PIX_FMT_CUDA)
            break;
    if (*pix_fmt == AV_PIX_FMT_NONE)
        goto cancel;

    av_log(NULL, AV_LOG_VERBOSE, "Setting up CUVID transcoding\n");

    if (ist->hwaccel_ctx) {
        ctx = ist->hwaccel_ctx;
    } else {
        ctx = av_mallocz(sizeof(*ctx));
        if (!ctx) {
            ret = AVERROR(ENOMEM);
            goto error;
        }
    }

    if (!ctx->hw_frames_ctx) {
        ret = av_hwdevice_ctx_create(&device_ref, AV_HWDEVICE_TYPE_CUDA,
                                     ist->hwaccel_device, NULL, 0);
        if (ret < 0)
            goto error;

        ctx->hw_frames_ctx = av_hwframe_ctx_alloc(device_ref);
        if (!ctx->hw_frames_ctx) {
            av_log(NULL, AV_LOG_ERROR, "av_hwframe_ctx_alloc failed\n");
            ret = AVERROR(ENOMEM);
            goto error;
        }
        av_buffer_unref(&device_ref);

        ist->hw_frames_ctx = av_buffer_ref(ctx->hw_frames_ctx);
        if (!ist->hw_frames_ctx) {
            av_log(NULL, AV_LOG_ERROR, "av_buffer_ref failed\n");
            ret = AVERROR(ENOMEM);
            goto error;
        }

        ist->hwaccel_ctx = ctx;
        ist->resample_pix_fmt = AV_PIX_FMT_CUDA;
        ist->hwaccel_uninit = mk_cuvid_uninit;

        /* This is a bit hacky, av_hwframe_ctx_init is called by the cuvid decoder
         * once it has probed the necessary format information. But as filters/nvenc
         * need to know the format/sw_format, set them here so they are happy.
         * This is fine as long as CUVID doesn't add another supported pix_fmt.
         */
        hwframe_ctx = (AVHWFramesContext*)ctx->hw_frames_ctx->data;
        hwframe_ctx->format = AV_PIX_FMT_CUDA;
        hwframe_ctx->sw_format = AV_PIX_FMT_NV12;
    }

    return 0;

error:
    av_freep(&ctx);
    av_buffer_unref(&device_ref);
    return ret;

cancel:
    if (ist->hwaccel_id == HWACCEL_CUVID) {
        av_log(NULL, AV_LOG_ERROR, "CUVID hwaccel requested, but impossible to achieve.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}
