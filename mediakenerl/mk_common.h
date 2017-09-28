#ifndef __MK_MEDIA_COMMON_H__
#define __MK_MEDIA_COMMON_H__
#include "mk_config.h"
#include "mk_def.h"
#include "mk_thread.h"
#include "mk_mutex.h"

#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#if MK_APP_OS == MK_OS_WIN32
#define snprintf _snprintf
#define strcasecmp stricmp
#define strncasecmp strnicmp
#define vsnprintf _vsnprintf
#endif
#include "libavformat/avformat.h"
#include "libavformat/avio.h"

#include "libavcodec/avcodec.h"

#include "libavfilter/avfilter.h"

#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libavutil/eval.h"
#include "libavutil/fifo.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"
#include "libavutil/threadmessage.h"

#include "libswresample/swresample.h"

#define PARAM_COUNT_MAX 256

static char* program_name = "libMediaKenerl";

typedef struct {
    char                   *specifier;    /**< stream/chapter/program/... specifier */
    union {
        uint8_t            *str;
        int                 i;
        int64_t             i64;
        float               f;
        double              dbl;
    } u;
} mk_specifier_opt_t;

typedef struct {
    const char             *name;
    int                     flags;
#define HAS_ARG      0x0001
#define OPT_BOOL     0x0002
#define OPT_EXPERT   0x0004
#define OPT_STRING   0x0008
#define OPT_VIDEO    0x0010
#define OPT_AUDIO    0x0020
#define OPT_INT      0x0080
#define OPT_FLOAT    0x0100
#define OPT_SUBTITLE 0x0200
#define OPT_INT64    0x0400
#define OPT_EXIT     0x0800
#define OPT_DATA     0x1000
#define OPT_PERFILE  0x2000     /* the option is per-file (currently ffmpeg-only).
                                   implied by OPT_OFFSET or OPT_SPEC */
#define OPT_OFFSET   0x4000     /* option is specified as an offset in a passed optctx */
#define OPT_SPEC     0x8000     /* option is to be stored in an array of mk_specifier_opt_t.
                                   Implies OPT_OFFSET. Next element after the offset is
                                   an int containing element count in the array. */
#define OPT_TIME     0x10000
#define OPT_DOUBLE   0x20000
#define OPT_INPUT    0x40000
#define OPT_OUTPUT   0x80000
     union {
        void               *dst_ptr;
        int (*func_arg)(void *,void *, const char *, const char *);
        size_t              off;
    } u;
    const char             *help;
    const char             *argname;
} mk_option_def_t;


/**
 * An option extracted from the commandline.
 * Cannot use AVDictionary because of options like -map which can be
 * used multiple times.
 */
typedef struct {
    const mk_option_def_t  *opt;
    const char             *key;
    const char             *val;
} mk_option_t;

typedef struct  {
    /**< group name */
    const char             *name;
    /**
     * Option to be used as group separator. Can be NULL for groups which
     * are terminated by a non-option argument (e.g. ffmpeg output files)
     */
    const char             *sep;
    /**
     * Option flags that must be set on each option that is
     * applied to this group
     */
    int                     flags;
} mk_option_group_def_t;

typedef struct {
    const mk_option_group_def_t *group_def;
    const char             *arg;
    mk_option_t            *opts;
    int                     nb_opts;
    AVDictionary           *codec_opts;
    AVDictionary           *format_opts;
    AVDictionary           *resample_opts;
    AVDictionary           *sws_dict;
    AVDictionary           *swr_opts;
} mk_option_group_t;

/**
 * A list of option groups that all have the same group type
 * (e.g. input files or output files)
 */
typedef struct {
    const mk_option_group_def_t *group_def;
    mk_option_group_t           *groups;
    int                          nb_groups;
} mk_option_group_list_t;

typedef struct {
    mk_option_group_t            global_opts;
    mk_option_group_list_t      *groups;
    int                          nb_groups;
    mk_option_group_t            cur_group;     /* parsing state */
} mk_option_parse_ctx_t;



#define VSYNC_AUTO       -1
#define VSYNC_PASSTHROUGH 0
#define VSYNC_CFR         1
#define VSYNC_VFR         2
#define VSYNC_VSCFR       0xfe
#define VSYNC_DROP        0xff

#define MAX_STREAMS       1024    /* arbitrary sanity check value */

enum HWAccelID {
    HWACCEL_NONE = 0,
    HWACCEL_AUTO,
    HWACCEL_VDPAU,
    HWACCEL_DXVA2,
    HWACCEL_VDA,
    HWACCEL_VIDEOTOOLBOX,
    HWACCEL_QSV,
    HWACCEL_VAAPI,
    HWACCEL_CUVID,
};

typedef struct {
    const char                   *name;
    int (*init)(AVCodecContext *s);
    enum HWAccelID                id;
    enum AVPixelFormat            pix_fmt;
} mk_hw_accel_t;

/* select an input stream for an output stream */
typedef struct  {
    int                           disabled;           /* 1 is this mapping is disabled by a negative map */
    int                           file_index;
    int                           stream_index;
    int                           sync_file_index;
    int                           sync_stream_index;
    char                         *linklabel;       /* name of an output link, for mapping lavfi outputs */
} mk_stream_map_t;

typedef struct {
    int                           file_idx;/* input */
    int                           stream_idx;
    int                           channel_idx;
    int                           ofile_idx;/* output */
    int                           ostream_idx;
} mk_audio_channel_map_t;

typedef struct {
    mk_option_group_t            *g;
    /* input/output options */
    int64_t                       start_time;
    int64_t                       start_time_eof;
    int                           seek_timestamp;
    const char                   *format;
    mk_specifier_opt_t           *codec_names;
    int                           nb_codec_names;
    mk_specifier_opt_t           *audio_channels;
    int                           nb_audio_channels;
    mk_specifier_opt_t           *audio_sample_rate;
    int                           nb_audio_sample_rate;
    mk_specifier_opt_t           *frame_rates;
    int                           nb_frame_rates;
    mk_specifier_opt_t           *frame_sizes;
    int                           nb_frame_sizes;
    mk_specifier_opt_t           *frame_pix_fmts;
    int                           nb_frame_pix_fmts;
    /* input options */
    int64_t                       input_ts_offset;
    int                           loop;
    int                           rate_emu;
    int                           accurate_seek;
    int                           thread_queue_size;
    mk_specifier_opt_t           *ts_scale;
    int                           nb_ts_scale;
    mk_specifier_opt_t           *dump_attachment;
    int                           nb_dump_attachment;
    mk_specifier_opt_t           *hwaccels;
    int                           nb_hwaccels;
    mk_specifier_opt_t           *hwaccel_devices;
    int                           nb_hwaccel_devices;
    mk_specifier_opt_t           *hwaccel_output_formats;
    int                           nb_hwaccel_output_formats;
    mk_specifier_opt_t           *autorotate;
    int                           nb_autorotate;
    /* output options */
    mk_stream_map_t              *stream_maps;
    int                           nb_stream_maps;
    mk_audio_channel_map_t       *audio_channel_maps; /* one info entry per -map_channel */
    int                           nb_audio_channel_maps; /* number of (valid) -map_channel settings */
    int                           metadata_global_manual;
    int                           metadata_streams_manual;
    int                           metadata_chapters_manual;
    const char                  **attachments;
    int                           nb_attachments;
    int                           chapters_input_file;
    int64_t                       recording_time;
    int64_t                       stop_time;
    uint64_t                      limit_filesize;
    float                         mux_preload;
    float                         mux_max_delay;
    int                           shortest;
    int                           video_disable;
    int                           audio_disable;
    int                           subtitle_disable;
    int                           data_disable;
    /* indexed by output file stream index */
    int                          *streamid_map;
    int                           nb_streamid_map;
    mk_specifier_opt_t           *metadata;
    int                           nb_metadata;
    mk_specifier_opt_t           *max_frames;
    int                           nb_max_frames;
    mk_specifier_opt_t           *bitstream_filters;
    int                           nb_bitstream_filters;
    mk_specifier_opt_t           *codec_tags;
    int                           nb_codec_tags;
    mk_specifier_opt_t           *sample_fmts;
    int                           nb_sample_fmts;
    mk_specifier_opt_t           *qscale;
    int                           nb_qscale;
    mk_specifier_opt_t           *forced_key_frames;
    int                           nb_forced_key_frames;
    mk_specifier_opt_t           *force_fps;
    int                           nb_force_fps;
    mk_specifier_opt_t           *frame_aspect_ratios;
    int                           nb_frame_aspect_ratios;
    mk_specifier_opt_t           *rc_overrides;
    int                           nb_rc_overrides;
    mk_specifier_opt_t           *intra_matrices;
    int                           nb_intra_matrices;
    mk_specifier_opt_t           *inter_matrices;
    int                           nb_inter_matrices;
    mk_specifier_opt_t           *chroma_intra_matrices;
    int                           nb_chroma_intra_matrices;
    mk_specifier_opt_t           *top_field_first;
    int                           nb_top_field_first;
    mk_specifier_opt_t           *metadata_map;
    int                           nb_metadata_map;
    mk_specifier_opt_t           *presets;
    int                           nb_presets;
    mk_specifier_opt_t           *copy_initial_nonkeyframes;
    int                           nb_copy_initial_nonkeyframes;
    mk_specifier_opt_t           *copy_prior_start;
    int                           nb_copy_prior_start;
    mk_specifier_opt_t           *filters;
    int                           nb_filters;
    mk_specifier_opt_t           *filter_scripts;
    int                           nb_filter_scripts;
    mk_specifier_opt_t           *reinit_filters;
    int                           nb_reinit_filters;
    mk_specifier_opt_t           *fix_sub_duration;
    int                           nb_fix_sub_duration;
    mk_specifier_opt_t           *canvas_sizes;
    int                           nb_canvas_sizes;
    mk_specifier_opt_t           *pass;
    int                           nb_pass;
    mk_specifier_opt_t           *passlogfiles;
    int                           nb_passlogfiles;
    mk_specifier_opt_t           *max_muxing_queue_size;
    int                           nb_max_muxing_queue_size;
    mk_specifier_opt_t           *guess_layout_max;
    int                           nb_guess_layout_max;
    mk_specifier_opt_t           *apad;
    int                           nb_apad;
    mk_specifier_opt_t           *discard;
    int                           nb_discard;
    mk_specifier_opt_t           *disposition;
    int                           nb_disposition;
    mk_specifier_opt_t           *program;
    int                           nb_program;
} mk_option_ctx_t;

typedef struct  {
    AVFilterContext              *filter;
    struct mk_input_stream_t     *ist;
    struct mk_filter_graph_t     *graph;
    uint8_t                      *name;
} mk_input_filter_t;

typedef struct {
    AVFilterContext              *filter;
    struct mk_output_stream_t    *ost;
    struct mk_filter_graph_t     *graph;
    uint8_t                      *name;
    /* temporary storage until stream maps are processed */
    AVFilterInOut                *out_tmp;
    enum AVMediaType              type;
} mk_output_filter_t;

typedef struct  mk_filter_graph_t{
    int                           index;
    const char                   *graph_desc;
    AVFilterGraph                *graph;
    int                           reconfiguration;
    mk_input_filter_t           **inputs;
    int                           nb_inputs;
    mk_output_filter_t          **outputs;
    int                           nb_outputs;
} mk_filter_graph_t;

typedef struct mk_input_stream_t{
    int                           file_index;
    AVStream                     *st;
    int                           discard;             /* true if stream data should be discarded */
    int                           user_set_discard;
    int                           decoding_needed;     /* non zero if the packets must be decoded in 'raw_fifo', see DECODING_FOR_* */
#define DECODING_FOR_OST    1
#define DECODING_FOR_FILTER 2

    AVCodecContext               *dec_ctx;
    AVCodec                      *dec;
    AVFrame                      *decoded_frame;
    AVFrame                      *filter_frame; /* a ref of decoded_frame, to be sent to filters */
    int64_t                       start;     /* time when read started */
    /* predicted dts of the next packet read for this stream or (when there are
     * several frames in a packet) of the next frame in current packet (in AV_TIME_BASE units) */
    int64_t                       next_dts;
    int64_t                       dts;       ///< dts of the last packet read for this stream (in AV_TIME_BASE units)
    int64_t                       next_pts;  ///< synthetic pts for the next decode frame (in AV_TIME_BASE units)
    int64_t                       pts;       ///< current pts of the decoded frame  (in AV_TIME_BASE units)
    int                           wrap_correction_done;
    int64_t                       filter_in_rescale_delta_last;
    int64_t                       min_pts; /* pts with the smallest value in a current stream */
    int64_t                       max_pts; /* pts with the higher value in a current stream */
    int64_t                       nb_samples; /* number of samples in the last decoded audio frame before looping */
    double                        ts_scale;
    int                           saw_first_ts;
    AVDictionary                 *decoder_opts;
    AVRational                    framerate;               /* framerate forced with -r */
    int                           top_field_first;
    int                           guess_layout_max;
    int                           autorotate;
    int                           resample_height;
    int                           resample_width;
    int                           resample_pix_fmt;
    int                           resample_sample_fmt;
    int                           resample_sample_rate;
    int                           resample_channels;
    uint64_t                      resample_channel_layout;
    int                           fix_sub_duration;
    struct { /* previous decoded subtitle and related variables */
        int got_output;
        int ret;
        AVSubtitle subtitle;
    } prev_sub;

    struct sub2video {
        int64_t last_pts;
        int64_t end_pts;
        AVFrame *frame;
        int w, h;
    } sub2video;

    int                           dr1;
    /* decoded data from this stream goes into all those filters
     * currently video and audio only */
    mk_input_filter_t           **filters;
    int                           nb_filters;
    int                           reinit_filters;
    /* hwaccel options */
    enum HWAccelID                hwaccel_id;
    char                         *hwaccel_device;
    enum AVPixelFormat            hwaccel_output_format;
    /* hwaccel context */
    enum HWAccelID                active_hwaccel_id;
    void                         *hwaccel_ctx;
    void (*hwaccel_uninit)(AVCodecContext *s);
    int  (*hwaccel_get_buffer)(AVCodecContext *s, AVFrame *frame, int flags);
    int  (*hwaccel_retrieve_data)(AVCodecContext *s, AVFrame *frame);
    enum AVPixelFormat            hwaccel_pix_fmt;
    enum AVPixelFormat            hwaccel_retrieved_pix_fmt;
    AVBufferRef                  *hw_frames_ctx;
    /* stats */
    // combined size of all the packets read
    uint64_t                      data_size;
    /* number of packets successfully read for this stream */
    uint64_t                      nb_packets;
    // number of frames/samples retrieved from the decoder
    uint64_t                      frames_decoded;
    uint64_t                      samples_decoded;
    int64_t                      *dts_buffer;
    int                           nb_dts_buffer;
} mk_input_stream_t;

typedef struct {
    AVFormatContext              *ctx;
    int                           eof_reached;      /* true if eof reached */
    int                           eagain;           /* true if last read attempt returned EAGAIN */
    int                           ist_index;        /* index of first stream in input_streams */
    int                           loop;             /* set number of times input stream should be looped */
    int64_t                       duration;         /* actual duration of the longest stream in a file
                                                       at the moment when looping happens */
    AVRational                    time_base; /* time base of the duration */
    int64_t                       input_ts_offset;
    int64_t                       ts_offset;
    int64_t                       last_ts;
    int64_t                       start_time;       /* user-specified start time in AV_TIME_BASE or AV_NOPTS_VALUE */
    int                           seek_timestamp;
    int64_t                       recording_time;
    int                           nb_streams;       /* number of stream that ffmpeg is aware of; may be different
                                                       from ctx.nb_streams if new streams appear during av_read_frame() */
    int                           nb_streams_warn;  /* number of streams that the user was warned of */
    int                           rate_emu;
    int                           accurate_seek;
    AVThreadMessageQueue         *in_thread_queue;
    mk_thread_t                  *thread;           /* thread reading from this file */
    int                           non_blocking;     /* reading packets from the thread should not block */
    int                           joined;           /* the thread has been joined */
    int                           thread_queue_size;/* maximum number of queued packets */
} mk_input_file_t;

enum forced_keyframes_const {
    FKF_N,
    FKF_N_FORCED,
    FKF_PREV_FORCED_N,
    FKF_PREV_FORCED_T,
    FKF_T,
    FKF_NB
};

#define ABORT_ON_FLAG_EMPTY_OUTPUT (1 <<  0)

extern const char *const forced_keyframes_const_names[];

typedef enum {
    ENCODER_FINISHED = 1,
    MUXER_FINISHED = 2,
} OSTFinished ;

typedef struct  mk_output_stream_t{
    int                           file_index;          /* file index */
    int                           index;               /* stream index in the output file */
    int                           source_index;        /* mk_input_stream_t index */
    AVStream                     *st;                  /* stream in the output file */
    int                           encoding_needed;     /* true if encoding needed for this stream */
    int                           frame_number;
    /* input pts and corresponding output pts for A/V sync */
    struct mk_input_stream_t     *sync_ist;            /* input stream to sync against */
    int64_t                       sync_opts;           /* output frame counter, could be changed to some true timestamp */ // FIXME look at frame_number
    /* pts of the first frame encoded for this stream, used for limiting
     * recording time */
    int64_t                       first_pts;
    /* dts of the last packet sent to the muxer */
    int64_t                       last_mux_dts;
    int                           nb_bitstream_filters;
    uint8_t                      *bsf_extradata_updated;
    AVBSFContext                **bsf_ctx;
    AVCodecContext               *enc_ctx;
    AVCodecParameters            *ref_par; /* associated input codec parameters with encoders options applied */
    AVCodec                      *enc;
    int64_t                       max_frames;
    AVFrame                      *filtered_frame;
    AVFrame                      *last_frame;
    int                           last_dropped;
    int                           last_nb0_frames[3];
    void                         *hwaccel_ctx;
    /* video only */
    AVRational                    frame_rate;
    int                           is_cfr;
    int                           force_fps;
    int                           top_field_first;
    int                           rotate_overridden;
    AVRational                    frame_aspect_ratio;
    /* forced key frames */
    int64_t                      *forced_kf_pts;
    int                           forced_kf_count;
    int                           forced_kf_index;
    char                         *forced_keyframes;
    AVExpr                       *forced_keyframes_pexpr;
    double                        forced_keyframes_expr_const_values[FKF_NB];
    /* audio only */
    int                          *audio_channels_map;             /* list of the channels id to pick from the source stream */
    int                           audio_channels_mapped;          /* number of channels in audio_channels_map */
    char                         *logfile_prefix;
    FILE                         *logfile;
    mk_output_filter_t           *filter;
    char                         *avfilter;
    char                         *filters;         ///< filtergraph associated to the -filter option
    char                         *filters_script;  ///< filtergraph script associated to the -filter_script option

    AVDictionary                 *encoder_opts;
    AVDictionary                 *sws_dict;
    AVDictionary                 *swr_opts;
    AVDictionary                 *resample_opts;
    char                         *apad;
    OSTFinished                   finished;        /* no more packets should be written for this stream */
    int                           unavailable;     /* true if the steram is unavailable (possibly temporarily) */
    int                           stream_copy;
    // init_output_stream() has been called for this stream
    // The encoder and the bitstream filters have been initialized and the stream
    // parameters are set in the AVStream.
    int                           initialized;
    const char                   *attachment_filename;
    int                           copy_initial_nonkeyframes;
    int                           copy_prior_start;
    char                         *disposition;
    int                           keep_pix_fmt;
    AVCodecParserContext         *parser;
    AVCodecContext               *parser_avctx;
    /* stats */
    // combined size of all the packets written
    uint64_t                      data_size;
    // number of packets send to the muxer
    uint64_t                      packets_written;
    // number of frames/samples sent to the encoder
    uint64_t                      frames_encoded;
    uint64_t                      samples_encoded;
    /* packet quality factor */
    int                           quality;
    int                           max_muxing_queue_size;
    /* the packets are buffered here until the muxer is ready to be initialized */
    AVFifoBuffer                 *muxing_queue;
    /* packet picture type */
    int                           pict_type;
    /* frame encode sum of squared error values */
    int64_t                       error[4];
}mk_output_stream_t ;

typedef struct  {
    AVFormatContext              *ctx;
    AVDictionary                 *opts;
    int                           ost_index;       /* index of the first stream in output_streams */
    int64_t                       recording_time;  ///< desired length of the resulting file in microseconds == AV_TIME_BASE units
    int64_t                       start_time;      ///< start time in microseconds == AV_TIME_BASE units
    uint64_t                      limit_filesize; /* filesize limit expressed in bytes */
    int                           shortest;
    int                           header_written;
} mk_output_file_t;

typedef void (*cb)(void* task,int ret);

typedef struct {
    mk_input_stream_t           **input_streams;
    int                           nb_input_streams;
    mk_input_file_t             **input_files;
    int                           nb_input_files;
    mk_output_stream_t          **output_streams;
    int                           nb_output_streams;
    mk_output_file_t            **output_files;
    int                           nb_output_files;
    mk_filter_graph_t           **filtergraphs;
    int                           nb_filtergraphs;
    int                           hwaccel_lax_profile_check;
    AVBufferRef                  *hw_device_ctx;
    char                         *vstats_filename;
    char                         *sdp_filename;
    float                         audio_drift_threshold;
    float                         dts_delta_threshold;
    float                         dts_error_threshold;
    int                           audio_volume;
    int                           audio_sync_method;
    int                           video_sync_method;
    float                         frame_drop_threshold;
    int                           do_deinterlace;
    int                           do_hex_dump;
    int                           do_pkt_dump;
    int                           copy_ts;
    int                           start_at_zero;
    int                           copy_tb;
    int                           debug_ts;
    int                           exit_on_error;
    int                           abort_on_flags;
    int                           print_stats;
    int                           frame_bits_per_raw_sample;
    AVIOInterruptCB               input_cb;
    AVIOInterruptCB               output_cb;
    AVDictionary                 *sws_dict;
    AVDictionary                 *swr_opts;
    AVDictionary                 *format_opts;
    AVDictionary                 *codec_opts;
    AVDictionary                 *resample_opts;
    volatile int                  running;
    volatile int                  transcode_init_done;
    volatile int                  exited;
    FILE                         *vstats_file;
    int                           nb_frames_dup;
    int                           nb_frames_drop;
    int64_t                       decode_error_stat[2];
    uint8_t                      *subtitle_out;
    int                           main_return_code;
    //task param
    int                           nparamcount;
    char                        **paramlist;
    mk_thread_t                  *thread;
    mk_mutex_t                   *mutex;
    int                           status;
    mk_task_stat_info_t           stat;
    time_t                        heartbeat;
} mk_task_ctx_t;

#endif /*__MK_MEDIA_COMMON_H__*/

