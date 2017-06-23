#ifndef __MK_MEDIA_CMDUTILS_H__
#define __MK_MEDIA_CMDUTILS_H__



#include <stdint.h>

#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "mk_common.h"

#ifdef _WIN32
#undef main /* We don't want SDL to override our main() */
#endif

void               mk_free_input_threads(mk_task_ctx_t* task);
int                mk_init_input_threads(mk_task_ctx_t* task);
int                mk_get_input_packet_mt(mk_input_file_t *f, AVPacket *pkt);
void               mk_cleanup_task(mk_task_ctx_t* task);
void               mk_init_opts(mk_task_ctx_t* task);
void               mk_uninit_opts(mk_task_ctx_t* task);

int                mk_opt_cpuflags(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg);
int                mk_opt_default(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg);
int                mk_opt_max_alloc(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg);

#if CONFIG_OPENCL
int                mk_opt_opencl(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg);
int                mk_opt_opencl_bench(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg);
#endif

double             mk_parse_number_or_die(mk_task_ctx_t* task,const char *context, const char *numstr, int type,
                           double min, double max);
int64_t            mk_parse_time_or_die(mk_task_ctx_t* task,const char *context, const char *timestr,
                           int is_duration);
int                mk_parse_option(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg,
                           const mk_option_def_t *options);
int                mk_parse_optgroup(mk_task_ctx_t* task,void *optctx, mk_option_group_t *g);
int                mk_split_commandline(mk_task_ctx_t* task,mk_option_parse_ctx_t *octx, int argc, char *argv[],
                           const mk_option_def_t *options,
                           const mk_option_group_def_t *groups, int nb_groups);
void               mk_uninit_parse_context(mk_task_ctx_t* task,mk_option_parse_ctx_t *octx);
int                mk_check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec);
AVDictionary      *mk_filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
                                AVFormatContext *s, AVStream *st, AVCodec *codec);
AVDictionary     **mk_setup_find_stream_info_opts(AVFormatContext *s,
                                           AVDictionary *codec_opts);
void               mk_log_error(const char *filename, int err);
void              *mk_grow_array(void *array, int elem_size, int *size, int new_size);

#define GROW_ARRAY(array, nb_elems)\
    array = mk_grow_array(array, sizeof(*array), &nb_elems, nb_elems + 1)

#define GET_PIX_FMT_NAME(pix_fmt)\
    const char *name = av_get_pix_fmt_name(pix_fmt);

#define GET_SAMPLE_FMT_NAME(sample_fmt)\
    const char *name = av_get_sample_fmt_name(sample_fmt)

#define GET_SAMPLE_RATE_NAME(rate)\
    char name[16];\
    snprintf(name, sizeof(name), "%d", rate);

#define GET_CH_LAYOUT_NAME(ch_layout)\
    char name[16];\
    snprintf(name, sizeof(name), "0x%"PRIx64, ch_layout);

#define GET_CH_LAYOUT_DESC(ch_layout)\
    char name[128];\
    av_get_channel_layout_string(name, sizeof(name), 0, ch_layout);

double             mk_get_rotation(AVStream *st);

#endif /* __MK_MEDIA_CMDUTILS_H__ */
