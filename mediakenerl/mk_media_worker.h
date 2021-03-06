#ifndef __MK_MEDIA_WORKER_H__
#define __MK_MEDIA_WORKER_H__

#include "mk_common.h"
#include <stdint.h>
#include <stdio.h>
#include <signal.h>

#include "mk_media_cmdutils.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libavutil/eval.h"
#include "libavutil/fifo.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"
#include "libavutil/threadmessage.h"
#include "libswresample/swresample.h"


extern const mk_option_def_t options[];
extern const mk_hw_accel_t hwaccels[];

void               mk_remove_avoptions(AVDictionary **a, AVDictionary *b);
void               mk_assert_avoptions(mk_task_ctx_t* task,AVDictionary *m);
int                mk_guess_input_channel_layout(mk_input_stream_t *ist);
enum AVPixelFormat mk_choose_pixel_fmt(AVStream *st, AVCodecContext *avctx, AVCodec *codec, enum AVPixelFormat target);
void               mk_choose_sample_fmt(AVStream *st, AVCodec *codec);
int                mk_configure_filtergraph(mk_task_ctx_t* task,mk_filter_graph_t *fg);
int                mk_configure_output_filter(mk_task_ctx_t* task,mk_filter_graph_t *fg, mk_output_filter_t *ofilter, AVFilterInOut *out);
int                mk_ist_in_filtergraph(mk_filter_graph_t *fg, mk_input_stream_t *ist);
int                mk_filtergraph_is_simple(mk_filter_graph_t*fg);
int                mk_init_simple_filtergraph(mk_task_ctx_t* task,mk_input_stream_t *ist, mk_output_stream_t *ost);
int                mk_init_complex_filtergraph(mk_task_ctx_t* task,mk_filter_graph_t *fg);
int                mk_ffmpeg_parse_options(mk_task_ctx_t* task,int argc, char **argv);
int                mk_vda_init(AVCodecContext *s);
int                mk_videotoolbox_init(AVCodecContext *s);
int                mk_qsv_init(AVCodecContext *s);
int                mk_cuvid_init(AVCodecContext *s);
mk_hw_device_t*    mk_hw_device_get_by_name(mk_task_ctx_t* task,const char *name);
int                mk_hw_device_init_from_string(mk_task_ctx_t* task,const char *arg, mk_hw_device_t **dev);
void               mk_hw_device_free_all(mk_task_ctx_t* task);
int                mk_hw_device_setup_for_decode(mk_task_ctx_t* task,mk_input_stream_t *ist);
int                mk_hw_device_setup_for_encode(mk_task_ctx_t* task,mk_output_stream_t *ost);
int                mk_hwaccel_decode_init(AVCodecContext *avctx);

void               mk_init_ffmpeg_option(mk_task_ctx_t* task);
static int         mk_init_output_stream(mk_task_ctx_t* task,mk_output_stream_t *ost, char *error, int error_len);
static void        mk_set_encoder_id(mk_task_ctx_t* task,mk_output_file_t *of, mk_output_stream_t *ost);
int                mk_ifilter_has_all_input_formats(mk_task_ctx_t* task,mk_filter_graph_t *fg);
void               mk_sub2video_update(mk_input_stream_t *ist, AVSubtitle *sub);
void               mk_check_filter_outputs(mk_task_ctx_t* task);
int                mk_ifilter_parameters_from_frame(mk_input_filter_t *ifilter, const AVFrame *frame);





/********function for set task option ************/
void               mk_set_vstat_file(mk_task_ctx_t* task,const char *name);
void               mk_set_pkt_dump(mk_task_ctx_t* task,int flag);
void               mk_set_hex_dump(mk_task_ctx_t* task,int flag);
void               mk_set_frame_drop_threshold(mk_task_ctx_t* task,int flag);
void               mk_set_audio_sync_method(mk_task_ctx_t* task,int flag);
void               mk_set_audio_drift_threshold(mk_task_ctx_t* task,int flag);
void               mk_set_copy_ts(mk_task_ctx_t* task,int flag);
void               mk_set_start_at_zero(mk_task_ctx_t* task,int flag);
void               mk_set_copy_tb(mk_task_ctx_t* task,int flag);
void               mk_set_dts_delta_threshold(mk_task_ctx_t* task,int flag);
void               mk_set_dts_error_threshold(mk_task_ctx_t* task,int flag);
void               mk_set_exit_on_error(mk_task_ctx_t* task,int flag);
void               mk_set_print_stats(mk_task_ctx_t* task,int flag);
void               mk_set_debug_ts(mk_task_ctx_t* task,int flag);
void               mk_set_frame_bits_per_raw_sample(mk_task_ctx_t* task,int flag);
void               mk_set_do_deinterlace(mk_task_ctx_t* task,int flag);
void               mk_set_audio_volume(mk_task_ctx_t* task,int flag);
void               mk_set_hwaccel_lax_profile_check(mk_task_ctx_t* task,int flag);
void               mk_set_filter_nbthreads(mk_task_ctx_t* task,int flag);
void               mk_set_filter_complex_nbthreads(mk_task_ctx_t* task,int flag);
void               mk_set_vstats_version(mk_task_ctx_t* task,int flag);




int                mk_main_task(mk_task_ctx_t* task);


#endif /* __MK_MEDIA_WORKER_H__ */

