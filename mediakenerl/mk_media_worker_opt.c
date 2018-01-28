#include <stdint.h>
#include <time.h>

#include "mk_media_worker.h"
#include "mk_media_cmdutils.h"

#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"

#include "libavfilter/avfilter.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/fifo.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"


#define DEFAULT_PASS_LOGFILENAME_PREFIX "ffmpeg2pass"

#define MATCH_PER_STREAM_OPT(name, type, outvar, fmtctx, st)\
{\
    int i, ret;\
    for (i = 0; i < o->nb_ ## name; i++) {\
        char *spec = o->name[i].specifier;\
        if ((ret = mk_check_stream_specifier(fmtctx, st, spec)) > 0)\
            outvar = o->name[i].u.type;\
        else if (ret < 0)\
            task->running = 0;\
    }\
}

#define MATCH_PER_TYPE_OPT(name, type, outvar, fmtctx, mediatype)\
{\
    int i;\
    for (i = 0; i < o->nb_ ## name; i++) {\
        char *spec = o->name[i].specifier;\
        if (!strcmp(spec, mediatype))\
            outvar = o->name[i].u.type;\
    }\
}

const mk_hw_accel_t hwaccels[] = {
#if HAVE_VDPAU_X11
    { "vdpau", mk_vdpau_init, HWACCEL_VDPAU, AV_PIX_FMT_VDPAU },
#endif
#if HAVE_DXVA2_LIB
    { "dxva2", mk_dxva2_init, HWACCEL_DXVA2, AV_PIX_FMT_DXVA2_VLD },
#endif
#if CONFIG_VDA
    { "vda",   mk_videotoolbox_init,   HWACCEL_VDA,   AV_PIX_FMT_VDA },
#endif
#if CONFIG_VIDEOTOOLBOX
    { "videotoolbox",   mk_videotoolbox_init,   HWACCEL_VIDEOTOOLBOX,   AV_PIX_FMT_VIDEOTOOLBOX },
#endif
#if CONFIG_LIBMFX
    { "qsv",   qsv_init,   HWACCEL_QSV,   AV_PIX_FMT_QSV },
#endif
#if CONFIG_VAAPI
    { "vaapi", vaapi_decode_init, HWACCEL_VAAPI, AV_PIX_FMT_VAAPI },
#endif
#if CONFIG_CUVID
    { "cuvid", cuvid_init, HWACCEL_CUVID, AV_PIX_FMT_CUDA },
#endif
    { 0 },
};



static int intra_only         = 0;
static int file_overwrite     = 0;
static int no_file_overwrite  = 0;
static int do_psnr            = 0;
static int input_sync;
static int override_ffserver  = 0;
static int input_stream_potentially_available = 0;
static int ignore_unknown_streams = 0;
static int copy_unknown_streams = 0;

static int mk_decode_interrupt_cb(void *ctx)
{
    mk_task_ctx_t* task = (mk_task_ctx_t*)ctx;

    if(task->exited){
        return 1;
    }
    return 0;
}


static void mk_uninit_options(mk_option_ctx_t *o)
{
    const mk_option_def_t *po = options;
    int i;

    /* all OPT_SPEC and OPT_STRING can be freed in generic way */
    while (po->name) {
        void *dst = (uint8_t*)o + po->u.off;

        if (po->flags & OPT_SPEC) {
            mk_specifier_opt_t **so = dst;
            int i, *count = (int*)(so + 1);
            for (i = 0; i < *count; i++) {
                av_freep(&(*so)[i].specifier);
                if (po->flags & OPT_STRING)
                    av_freep(&(*so)[i].u.str);
            }
            av_freep(so);
            *count = 0;
        } else if (po->flags & OPT_OFFSET && po->flags & OPT_STRING)
            av_freep(dst);
        po++;
    }

    for (i = 0; i < o->nb_stream_maps; i++)
        av_freep(&o->stream_maps[i].linklabel);
    av_freep(&o->stream_maps);
    av_freep(&o->audio_channel_maps);
    av_freep(&o->streamid_map);
    av_freep(&o->attachments);
}

static void mk_init_options(mk_option_ctx_t *o)
{
    memset(o, 0, sizeof(*o));

    o->stop_time = INT64_MAX;
    o->mux_max_delay  = 0.7;
    o->start_time     = AV_NOPTS_VALUE;
    o->start_time_eof = AV_NOPTS_VALUE;
    o->recording_time = INT64_MAX;
    o->limit_filesize = UINT64_MAX;
    o->chapters_input_file = INT_MAX;
    o->accurate_seek  = 1;
}


/* return a copy of the input with the stream specifiers removed from the keys */
static AVDictionary *mk_strip_specifiers(AVDictionary *dict)
{
    AVDictionaryEntry *e = NULL;
    AVDictionary    *ret = NULL;

    while ((e = av_dict_get(dict, "", e, AV_DICT_IGNORE_SUFFIX))) {
        char *p = strchr(e->key, ':');

        if (p)
            *p = 0;
        av_dict_set(&ret, e->key, e->value, 0);
        if (p)
            *p = ':';
    }
    return ret;
}

static int mk_opt_abort_on(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    static const AVOption opts[] = {
        { "abort_on"        , NULL, 0, AV_OPT_TYPE_FLAGS, { .i64 = 0 }, INT64_MIN, INT64_MAX, .unit = "flags" },
        { "empty_output"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = ABORT_ON_FLAG_EMPTY_OUTPUT     },    .unit = "flags" },
        { NULL },
    };
    static const AVClass class = {
        .class_name = "",
        .item_name  = av_default_item_name,
        .option     = opts,
        .version    = LIBAVUTIL_VERSION_INT,
    };
    const AVClass *pclass = &class;

    return av_opt_eval_flags(&pclass, &opts[0], arg, &task->abort_on_flags);
}

static int mk_opt_sameq(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_ERROR, "Option '%s' was removed. "
           "If you are looking for an option to preserve the quality (which is not "
           "what -%s was for), use -qscale 0 or an equivalent quality factor option.\n",
           opt, opt);
    return AVERROR(EINVAL);
}

static int mk_opt_video_channel(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "This option is deprecated, use -channel.\n");
    return mk_opt_default(task,optctx, "channel", arg);
}

static int mk_opt_video_standard(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "This option is deprecated, use -standard.\n");
    return mk_opt_default(task,optctx, "standard", arg);
}

static int mk_opt_audio_codec(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    return mk_parse_option(task,o, "codec:a", arg, options);
}

static int mk_opt_video_codec(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    return mk_parse_option(task,o, "codec:v", arg, options);
}

static int mk_opt_subtitle_codec(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    return mk_parse_option(task,o, "codec:s", arg, options);
}

static int mk_opt_data_codec(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    return mk_parse_option(task,o, "codec:d", arg, options);
}

static int mk_opt_map(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    mk_stream_map_t *m = NULL;
    int i, negative = 0, file_idx;
    int sync_file_idx = -1, sync_stream_idx = 0;
    char *p, *sync;
    char *map;
    char *allow_unused;

    if (*arg == '-') {
        negative = 1;
        arg++;
    }
    map = av_strdup(arg);
    if (!map)
        return AVERROR(ENOMEM);

    /* parse sync stream first, just pick first matching stream */
    if (sync = strchr(map, ',')) {
        *sync = 0;
        sync_file_idx = strtol(sync + 1, &sync, 0);
        if (sync_file_idx >= task->nb_input_files || sync_file_idx < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid sync file index: %d.\n", sync_file_idx);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
        if (*sync)
            sync++;
        for (i = 0; i < task->input_files[sync_file_idx]->nb_streams; i++)
            if (mk_check_stream_specifier(task->input_files[sync_file_idx]->ctx,
                                       task->input_files[sync_file_idx]->ctx->streams[i], sync) == 1) {
                sync_stream_idx = i;
                break;
            }
        if (i == task->input_files[sync_file_idx]->nb_streams) {
            av_log(NULL, AV_LOG_FATAL, "Sync stream specification in map %s does not "
                                       "match any streams.\n", arg);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
    }


    if (map[0] == '[') {
        /* this mapping refers to lavfi output */
        const char *c = map + 1;
        GROW_ARRAY(o->stream_maps, o->nb_stream_maps);
        m = &o->stream_maps[o->nb_stream_maps - 1];
        m->linklabel = av_get_token(&c, "]");
        if (!m->linklabel) {
            av_log(NULL, AV_LOG_ERROR, "Invalid output link label: %s.\n", map);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
    } else {
        if (allow_unused = strchr(map, '?'))
            *allow_unused = 0;
        file_idx = strtol(map, &p, 0);
        if (file_idx >= task->nb_input_files || file_idx < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index: %d.\n", file_idx);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
        if (negative)
            /* disable some already defined maps */
            for (i = 0; i < o->nb_stream_maps; i++) {
                m = &o->stream_maps[i];
                if (file_idx == m->file_index &&
                    mk_check_stream_specifier(task->input_files[m->file_index]->ctx,
                                           task->input_files[m->file_index]->ctx->streams[m->stream_index],
                                           *p == ':' ? p + 1 : p) > 0)
                    m->disabled = 1;
            }
        else
            for (i = 0; i < task->input_files[file_idx]->nb_streams; i++) {
                if (mk_check_stream_specifier(task->input_files[file_idx]->ctx, task->input_files[file_idx]->ctx->streams[i],
                            *p == ':' ? p + 1 : p) <= 0)
                    continue;
                GROW_ARRAY(o->stream_maps, o->nb_stream_maps);
                m = &o->stream_maps[o->nb_stream_maps - 1];

                m->file_index   = file_idx;
                m->stream_index = i;

                if (sync_file_idx >= 0) {
                    m->sync_file_index   = sync_file_idx;
                    m->sync_stream_index = sync_stream_idx;
                } else {
                    m->sync_file_index   = file_idx;
                    m->sync_stream_index = i;
                }
            }
    }

    if (!m) {
        if (allow_unused) {
            av_log(NULL, AV_LOG_VERBOSE, "Stream map '%s' matches no streams; ignoring.\n", arg);
        } else {
            av_log(NULL, AV_LOG_FATAL, "Stream map '%s' matches no streams.\n"
                                       "To ignore this, add a trailing '?' to the map.\n", arg);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
    }

    av_freep(&map);
    return 0;
}

static int mk_opt_attach(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    GROW_ARRAY(o->attachments, o->nb_attachments);
    o->attachments[o->nb_attachments - 1] = arg;
    return 0;
}

static int mk_opt_map_channel(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    int n;
    AVStream *st;
    mk_audio_channel_map_t *m;

    GROW_ARRAY(o->audio_channel_maps, o->nb_audio_channel_maps);
    m = &o->audio_channel_maps[o->nb_audio_channel_maps - 1];

    /* muted channel syntax */
    n = sscanf(arg, "%d:%d.%d", &m->channel_idx, &m->ofile_idx, &m->ostream_idx);
    if ((n == 1 || n == 3) && m->channel_idx == -1) {
        m->file_idx = m->stream_idx = -1;
        if (n == 1)
            m->ofile_idx = m->ostream_idx = -1;
        return 0;
    }

    /* normal syntax */
    n = sscanf(arg, "%d.%d.%d:%d.%d",
               &m->file_idx,  &m->stream_idx, &m->channel_idx,
               &m->ofile_idx, &m->ostream_idx);

    if (n != 3 && n != 5) {
        av_log(NULL, AV_LOG_FATAL, "Syntax error, mapchan usage: "
               "[file.stream.channel|-1][:syncfile:syncstream]\n");
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }

    if (n != 5) // only file.stream.channel specified
        m->ofile_idx = m->ostream_idx = -1;

    /* check input */
    if (m->file_idx < 0 || m->file_idx >= task->nb_input_files) {
        av_log(NULL, AV_LOG_FATAL, "mapchan: invalid input file index: %d\n",
               m->file_idx);
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }
    if (m->stream_idx < 0 ||
        m->stream_idx >= task->input_files[m->file_idx]->nb_streams) {
        av_log(NULL, AV_LOG_FATAL, "mapchan: invalid input file stream index #%d.%d\n",
               m->file_idx, m->stream_idx);
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }
    st = task->input_files[m->file_idx]->ctx->streams[m->stream_idx];
    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(NULL, AV_LOG_FATAL, "mapchan: stream #%d.%d is not an audio stream.\n",
               m->file_idx, m->stream_idx);
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }
    if (m->channel_idx < 0 || m->channel_idx >= st->codecpar->channels) {
        av_log(NULL, AV_LOG_FATAL, "mapchan: invalid audio channel #%d.%d.%d\n",
               m->file_idx, m->stream_idx, m->channel_idx);
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }
    return 0;
}

static int mk_opt_sdp_file(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    av_free(task->sdp_filename);
    task->sdp_filename = av_strdup(arg);
    return 0;
}
#if CONFIG_VAAPI
static int mk_opt_vaapi_device(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    int err;
    err = mk_vaapi_device_init(arg);
    if (err < 0) {
        task->running = 0;
        return -1;
    }
    return 0;
}
#endif

/**
 * Parse a metadata specifier passed as 'arg' parameter.
 * @param arg  metadata string to parse
 * @param type metadata type is written here -- g(lobal)/s(tream)/c(hapter)/p(rogram)
 * @param index for type c/p, chapter/program index is written here
 * @param stream_spec for type s, the stream specifier is written here
 */
static void mk_parse_meta_type(mk_task_ctx_t* task,char *arg, char *type, int *index, const char **stream_spec)
{
    if (*arg) {
        *type = *arg;
        switch (*arg) {
        case 'g':
            break;
        case 's':
            if (*(++arg) && *arg != ':') {
                av_log(NULL, AV_LOG_FATAL, "Invalid metadata specifier %s.\n", arg);
                task->running = 0;
                return;
                //exit_program(task,1);
            }
            *stream_spec = *arg == ':' ? arg + 1 : "";
            break;
        case 'c':
        case 'p':
            if (*(++arg) == ':')
                *index = strtol(++arg, NULL, 0);
            break;
        default:
            av_log(NULL, AV_LOG_FATAL, "Invalid metadata type %c.\n", *arg);
            task->running = 0;
            return;
            //exit_program(task,1);
        }
    } else
        *type = 'g';
}

static int mk_copy_metadata(mk_task_ctx_t* task,char *outspec, char *inspec, AVFormatContext *oc, AVFormatContext *ic, mk_option_ctx_t *o)
{
    AVDictionary **meta_in = NULL;
    AVDictionary **meta_out = NULL;
    int i, ret = 0;
    char type_in, type_out;
    const char *istream_spec = NULL, *ostream_spec = NULL;
    int idx_in = 0, idx_out = 0;

    mk_parse_meta_type(task,inspec,  &type_in,  &idx_in,  &istream_spec);
    mk_parse_meta_type(task,outspec, &type_out, &idx_out, &ostream_spec);

    if (!ic) {
        if (type_out == 'g' || !*outspec)
            o->metadata_global_manual = 1;
        if (type_out == 's' || !*outspec)
            o->metadata_streams_manual = 1;
        if (type_out == 'c' || !*outspec)
            o->metadata_chapters_manual = 1;
        return 0;
    }

    if (type_in == 'g' || type_out == 'g')
        o->metadata_global_manual = 1;
    if (type_in == 's' || type_out == 's')
        o->metadata_streams_manual = 1;
    if (type_in == 'c' || type_out == 'c')
        o->metadata_chapters_manual = 1;

    /* ic is NULL when just disabling automatic mappings */
    if (!ic)
        return 0;

#define METADATA_CHECK_INDEX(index, nb_elems, desc)\
    if ((index) < 0 || (index) >= (nb_elems)) {\
        av_log(NULL, AV_LOG_FATAL, "Invalid %s index %d while processing metadata maps.\n",\
                (desc), (index));\
        task->running = 0;\
    }

#define SET_DICT(type, meta, context, index)\
        switch (type) {\
        case 'g':\
            meta = &context->metadata;\
            break;\
        case 'c':\
            METADATA_CHECK_INDEX(index, context->nb_chapters, "chapter")\
            meta = &context->chapters[index]->metadata;\
            break;\
        case 'p':\
            METADATA_CHECK_INDEX(index, context->nb_programs, "program")\
            meta = &context->programs[index]->metadata;\
            break;\
        case 's':\
            break; /* handled separately below */ \
        default: av_assert0(0);\
        }\

    SET_DICT(type_in, meta_in, ic, idx_in);
    SET_DICT(type_out, meta_out, oc, idx_out);

    /* for input streams choose first matching stream */
    if (type_in == 's') {
        for (i = 0; i < ic->nb_streams; i++) {
            if ((ret = mk_check_stream_specifier(ic, ic->streams[i], istream_spec)) > 0) {
                meta_in = &ic->streams[i]->metadata;
                break;
            } else if (ret < 0){
                task->running = 0;
                return -1;
                //exit_program(task,1);
            }
        }
        if (!meta_in) {
            av_log(NULL, AV_LOG_FATAL, "Stream specifier %s does not match  any streams.\n", istream_spec);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
    }

    if (type_out == 's') {
        for (i = 0; i < oc->nb_streams; i++) {
            if ((ret = mk_check_stream_specifier(oc, oc->streams[i], ostream_spec)) > 0) {
                meta_out = &oc->streams[i]->metadata;
                av_dict_copy(meta_out, *meta_in, AV_DICT_DONT_OVERWRITE);
            } else if (ret < 0){
                task->running = 0;
                return -1;
                //exit_program(task,1);
            }
        }
    } else
        av_dict_copy(meta_out, *meta_in, AV_DICT_DONT_OVERWRITE);

    return 0;
}

static int mk_opt_recording_timestamp(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    char buf[128];
    int64_t recording_timestamp = mk_parse_time_or_die(task,opt, arg, 0) / 1E6;
    struct tm* time = gmtime((time_t*)&recording_timestamp);
    if (!strftime(buf, sizeof(buf), "creation_time=%Y-%m-%dT%H:%M:%S%z", time))
        return -1;
    mk_parse_option(task,o, "metadata", buf, options);

    av_log(NULL, AV_LOG_WARNING, "%s is deprecated, set the 'creation_time' metadata "
                                 "tag instead.\n", opt);
    return 0;
}

static AVCodec *mk_find_codec_or_die(mk_task_ctx_t* task,const char *name, enum AVMediaType type, int encoder)
{
    const AVCodecDescriptor *desc;
    const char *codec_string = encoder ? "encoder" : "decoder";
    AVCodec *codec;

    codec = encoder ?
        avcodec_find_encoder_by_name(name) :
        avcodec_find_decoder_by_name(name);

    if (!codec && (desc = avcodec_descriptor_get_by_name(name))) {
        codec = encoder ? avcodec_find_encoder(desc->id) :
                          avcodec_find_decoder(desc->id);
        if (codec)
            av_log(NULL, AV_LOG_VERBOSE, "Matched %s '%s' for codec '%s'.\n",
                   codec_string, codec->name, desc->name);
    }

    if (!codec) {
        av_log(NULL, AV_LOG_FATAL, "Unknown %s '%s'\n", codec_string, name);
        task->running = 0;
        return NULL;
        //exit_program(task,1);
    }
    if (codec->type != type) {
        av_log(NULL, AV_LOG_FATAL, "Invalid %s type '%s'\n", codec_string, name);
        task->running = 0;
        return NULL;
        //exit_program(task,1);
    }
    return codec;
}

static AVCodec *mk_choose_decoder(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *s, AVStream *st)
{
    char *codec_name = NULL;

    MATCH_PER_STREAM_OPT(codec_names, str, codec_name, s, st);
    if (codec_name) {
        AVCodec *codec = mk_find_codec_or_die(task,codec_name, st->codecpar->codec_type, 0);
        st->codecpar->codec_id = codec->id;
        return codec;
    } else {
        if(st->codecpar) {
            return avcodec_find_decoder(st->codecpar->codec_id);
        }
        return avcodec_find_decoder(st->codec->codec_id);
    }
}

/* Add all the streams from the given input file to the global
 * list of input streams. */
static void mk_add_input_streams(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *ic)
{
    int i, ret;

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        AVCodecParameters *par = st->codecpar;
        mk_input_stream_t *ist = av_mallocz(sizeof(*ist));
        char *framerate = NULL, *hwaccel = NULL, *hwaccel_device = NULL;
        char *hwaccel_output_format = NULL;
        char *codec_tag = NULL;
        char *next;
        char *discard_str = NULL;
        const AVClass *cc = avcodec_get_class();
        const AVOption *discard_opt = av_opt_find(&cc, "skip_frame", NULL, 0, 0);


        if (!ist){
            task->running = 0;
            return;
            //exit_program(task,1);
        }

        GROW_ARRAY(task->input_streams, task->nb_input_streams);
        task->input_streams[task->nb_input_streams - 1] = ist;

        ist->st = st;
        ist->file_index = task->nb_input_files;
        ist->discard = 1;
        st->discard  = AVDISCARD_ALL;
        ist->nb_samples = 0;
        ist->min_pts = INT64_MAX;
        ist->max_pts = INT64_MIN;

        ist->ts_scale = 1.0;
        MATCH_PER_STREAM_OPT(ts_scale, dbl, ist->ts_scale, ic, st);

        ist->autorotate = 1;
        MATCH_PER_STREAM_OPT(autorotate, i, ist->autorotate, ic, st);

        MATCH_PER_STREAM_OPT(codec_tags, str, codec_tag, ic, st);
        if (codec_tag) {
            uint32_t tag = strtol(codec_tag, &next, 0);
            if (*next)
                tag = AV_RL32(codec_tag);
            st->codecpar->codec_tag = tag;
        }

        ist->dec = mk_choose_decoder(task,o, ic, st);
        ist->decoder_opts = mk_filter_codec_opts(o->g->codec_opts, ist->st->codecpar->codec_id, ic, st, ist->dec);

        ist->reinit_filters = -1;
        MATCH_PER_STREAM_OPT(reinit_filters, i, ist->reinit_filters, ic, st);

        MATCH_PER_STREAM_OPT(discard, str, discard_str, ic, st);
        ist->user_set_discard = AVDISCARD_NONE;
        if (discard_str && av_opt_eval_int(&cc, discard_opt, discard_str, &ist->user_set_discard) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error parsing discard %s.\n",
                    discard_str);
            task->running = 0;
            return;
            //exit_program(task,1);
        }

        ist->filter_in_rescale_delta_last = AV_NOPTS_VALUE;

        ist->dec_ctx = avcodec_alloc_context3(ist->dec);
        if (!ist->dec_ctx) {
            av_log(NULL, AV_LOG_ERROR, "Error allocating the decoder context.\n");
            task->running = 0;
            return;
            //exit_program(task,1);
        }

        ret = avcodec_parameters_to_context(ist->dec_ctx, par);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error initializing the decoder context.\n");
            task->running = 0;
            return;
            //exit_program(task,1);
        }

        switch (par->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            if(!ist->dec)
                ist->dec = avcodec_find_decoder(par->codec_id);
#if FF_API_EMU_EDGE
            if (av_codec_get_lowres(st->codec)) {
                av_codec_set_lowres(ist->dec_ctx, av_codec_get_lowres(st->codec));
                ist->dec_ctx->width  = st->codec->width;
                ist->dec_ctx->height = st->codec->height;
                ist->dec_ctx->coded_width  = st->codec->coded_width;
                ist->dec_ctx->coded_height = st->codec->coded_height;
                ist->dec_ctx->flags |= CODEC_FLAG_EMU_EDGE;
            }
#endif
            // avformat_find_stream_info() doesn't set this for us anymore.
            ist->dec_ctx->framerate = st->avg_frame_rate;

            MATCH_PER_STREAM_OPT(frame_rates, str, framerate, ic, st);
            if (framerate && av_parse_video_rate(&ist->framerate,
                                                 framerate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error parsing framerate %s.\n",
                       framerate);
                task->running = 0;
                return;
                //exit_program(task,1);
            }

            ist->top_field_first = -1;
            MATCH_PER_STREAM_OPT(top_field_first, i, ist->top_field_first, ic, st);

            MATCH_PER_STREAM_OPT(hwaccels, str, hwaccel, ic, st);
            if (hwaccel) {
                if (!strcmp(hwaccel, "none"))
                    ist->hwaccel_id = HWACCEL_NONE;
                else if (!strcmp(hwaccel, "auto"))
                    ist->hwaccel_id = HWACCEL_AUTO;
                else {
                    int i;
                    for (i = 0; hwaccels[i].name; i++) {
                        if (!strcmp(hwaccels[i].name, hwaccel)) {
                            ist->hwaccel_id = hwaccels[i].id;
                            break;
                        }
                    }

                    if (!ist->hwaccel_id) {
                        av_log(NULL, AV_LOG_FATAL, "Unrecognized hwaccel: %s.\n",
                               hwaccel);
                        av_log(NULL, AV_LOG_FATAL, "Supported hwaccels: ");
                        for (i = 0; hwaccels[i].name; i++)
                            av_log(NULL, AV_LOG_FATAL, "%s ", hwaccels[i].name);
                        av_log(NULL, AV_LOG_FATAL, "\n");
                        task->running = 0;
                        return;
                        //exit_program(task,1);
                    }
                }
            }

            MATCH_PER_STREAM_OPT(hwaccel_devices, str, hwaccel_device, ic, st);
            if (hwaccel_device) {
                ist->hwaccel_device = av_strdup(hwaccel_device);
                if (!ist->hwaccel_device){
                    task->running = 0;
                    return;
                    //exit_program(task,1);
                }
            }

            MATCH_PER_STREAM_OPT(hwaccel_output_formats, str,
                                 hwaccel_output_format, ic, st);
            if (hwaccel_output_format) {
                ist->hwaccel_output_format = av_get_pix_fmt(hwaccel_output_format);
                if (ist->hwaccel_output_format == AV_PIX_FMT_NONE) {
                    av_log(NULL, AV_LOG_FATAL, "Unrecognised hwaccel output "
                           "format: %s", hwaccel_output_format);
                }
            } else {
                ist->hwaccel_output_format = AV_PIX_FMT_NONE;
            }

            ist->hwaccel_pix_fmt = AV_PIX_FMT_NONE;

            break;
        case AVMEDIA_TYPE_AUDIO:
            ist->guess_layout_max = INT_MAX;
            MATCH_PER_STREAM_OPT(guess_layout_max, i, ist->guess_layout_max, ic, st);
            mk_guess_input_channel_layout(ist);

            break;
        case AVMEDIA_TYPE_DATA:
        case AVMEDIA_TYPE_SUBTITLE: {
            char *canvas_size = NULL;
            if(!ist->dec)
                ist->dec = avcodec_find_decoder(par->codec_id);
            MATCH_PER_STREAM_OPT(fix_sub_duration, i, ist->fix_sub_duration, ic, st);
            MATCH_PER_STREAM_OPT(canvas_sizes, str, canvas_size, ic, st);
            if (canvas_size &&
                av_parse_video_size(&ist->dec_ctx->width, &ist->dec_ctx->height, canvas_size) < 0) {
                av_log(NULL, AV_LOG_FATAL, "Invalid canvas size: %s.\n", canvas_size);
                task->running = 0;
                return;
                //exit_program(task,1);
            }
            break;
        }
        case AVMEDIA_TYPE_ATTACHMENT:
        case AVMEDIA_TYPE_UNKNOWN:
            break;
        default:{
            task->running = 0;
            //exit_program(task,1);
           }
        }

        ret = avcodec_parameters_from_context(par, ist->dec_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error initializing the decoder context.\n");
            task->running = 0;
        }
    }
}


static void mk_dump_attachment(mk_task_ctx_t* task,AVStream *st, const char *filename)
{
    int ret;
    AVIOContext *out = NULL;
    AVDictionaryEntry *e;

    if (!st->codecpar->extradata_size) {
        av_log(NULL, AV_LOG_WARNING, "No extradata to dump in stream #%d:%d.\n",
               task->nb_input_files - 1, st->index);
        return;
    }
    if (!*filename && (e = av_dict_get(st->metadata, "filename", NULL, 0)))
        filename = e->value;
    if (!*filename) {
        av_log(NULL, AV_LOG_FATAL, "No filename specified and no 'filename' tag"
               "in stream #%d:%d.\n", task->nb_input_files - 1, st->index);
        task->running = 0;
        return;
        //exit_program(task,1);
    }


    if ((ret = avio_open2(&out, filename, AVIO_FLAG_WRITE, &task->int_cb, NULL)) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Could not open file %s for writing.\n",
               filename);
        task->running = 0;
        return;
        //exit_program(task,1);
    }

    avio_write(out, st->codecpar->extradata, st->codecpar->extradata_size);
    avio_flush(out);
    avio_close(out);
}

static int mk_open_input_file(mk_task_ctx_t* task,mk_option_ctx_t *o, const char *filename)
{
    mk_input_file_t *f;
    AVFormatContext *ic;
    AVInputFormat *file_iformat = NULL;
    int err, i, ret;
    int64_t timestamp;
    AVDictionary **opts;
    AVDictionary *unused_opts = NULL;
    AVDictionaryEntry *e = NULL;
    int orig_nb_streams;                     // number of streams before avformat_find_stream_info
    char *   video_codec_name = NULL;
    char *   audio_codec_name = NULL;
    char *subtitle_codec_name = NULL;
    char *    data_codec_name = NULL;
    int scan_all_pmts_set = 0;

    if (o->format) {
        if (!(file_iformat = av_find_input_format(o->format))) {
            av_log(NULL, AV_LOG_FATAL, "Unknown input format: '%s'\n", o->format);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
    }

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    /* get default parameters from command line */
    ic = avformat_alloc_context();
    if (!ic) {
        mk_log_error(filename, AVERROR(ENOMEM));
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }
    ic->flags |= AVFMT_FLAG_KEEP_SIDE_DATA;
    if (o->nb_audio_sample_rate) {
        av_dict_set_int(&o->g->format_opts, "sample_rate", o->audio_sample_rate[o->nb_audio_sample_rate - 1].u.i, 0);
    }
    if (o->nb_audio_channels) {
        /* because we set audio_channels based on both the "ac" and
         * "channel_layout" options, we need to check that the specified
         * demuxer actually has the "channels" option before setting it */
        if (file_iformat && file_iformat->priv_class &&
            av_opt_find(&file_iformat->priv_class, "channels", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set_int(&o->g->format_opts, "channels", o->audio_channels[o->nb_audio_channels - 1].u.i, 0);
        }
    }
    if (o->nb_frame_rates) {
        /* set the format-level framerate option;
         * this is important for video grabbers, e.g. x11 */
        if (file_iformat && file_iformat->priv_class &&
            av_opt_find(&file_iformat->priv_class, "framerate", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&o->g->format_opts, "framerate",
                        o->frame_rates[o->nb_frame_rates - 1].u.str, 0);
        }
    }
    if (o->nb_frame_sizes) {
        av_dict_set(&o->g->format_opts, "video_size", o->frame_sizes[o->nb_frame_sizes - 1].u.str, 0);
    }
    if (o->nb_frame_pix_fmts)
        av_dict_set(&o->g->format_opts, "pixel_format", o->frame_pix_fmts[o->nb_frame_pix_fmts - 1].u.str, 0);

    MATCH_PER_TYPE_OPT(codec_names, str,    video_codec_name, ic, "v");
    MATCH_PER_TYPE_OPT(codec_names, str,    audio_codec_name, ic, "a");
    MATCH_PER_TYPE_OPT(codec_names, str, subtitle_codec_name, ic, "s");
    MATCH_PER_TYPE_OPT(codec_names, str,     data_codec_name, ic, "d");

    ic->video_codec_id   = video_codec_name ?
        mk_find_codec_or_die(task,video_codec_name   , AVMEDIA_TYPE_VIDEO   , 0)->id : AV_CODEC_ID_NONE;
    ic->audio_codec_id   = audio_codec_name ?
        mk_find_codec_or_die(task,audio_codec_name   , AVMEDIA_TYPE_AUDIO   , 0)->id : AV_CODEC_ID_NONE;
    ic->subtitle_codec_id= subtitle_codec_name ?
        mk_find_codec_or_die(task,subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, 0)->id : AV_CODEC_ID_NONE;
    ic->data_codec_id    = data_codec_name ?
        mk_find_codec_or_die(task,data_codec_name, AVMEDIA_TYPE_DATA, 0)->id : AV_CODEC_ID_NONE;

    if (video_codec_name)
        av_format_set_video_codec   (ic, mk_find_codec_or_die(task,video_codec_name   , AVMEDIA_TYPE_VIDEO   , 0));
    if (audio_codec_name)
        av_format_set_audio_codec   (ic, mk_find_codec_or_die(task,audio_codec_name   , AVMEDIA_TYPE_AUDIO   , 0));
    if (subtitle_codec_name)
        av_format_set_subtitle_codec(ic, mk_find_codec_or_die(task,subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, 0));
    if (data_codec_name)
        av_format_set_data_codec(ic, mk_find_codec_or_die(task,data_codec_name, AVMEDIA_TYPE_DATA, 0));

    ic->flags |= AVFMT_FLAG_NONBLOCK;
    ic->interrupt_callback = task->int_cb;

    if (!av_dict_get(o->g->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&o->g->format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    /* open the input file with generic avformat function */
    err = avformat_open_input(&ic, filename, file_iformat, &o->g->format_opts);
    if (err < 0) {
        mk_log_error(filename, err);
        if (err == AVERROR_PROTOCOL_NOT_FOUND)
            av_log(NULL, AV_LOG_ERROR, "Did you mean file:%s?\n", filename);
        task->running = 0;
        //exit_program(task,1);
        return -1;
    }
    if (scan_all_pmts_set)
        av_dict_set(&o->g->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    mk_remove_avoptions(&o->g->format_opts, o->g->codec_opts);
    mk_assert_avoptions(task,o->g->format_opts);

    /* apply forced codec ids */
    for (i = 0; i < ic->nb_streams; i++)
        mk_choose_decoder(task,o, ic, ic->streams[i]);

    /* Set AVCodecContext options for avformat_find_stream_info */
    opts = mk_setup_find_stream_info_opts(ic, o->g->codec_opts);
    orig_nb_streams = ic->nb_streams;

    /* If not enough info to get the stream parameters, we decode the
       first frames to get it. (used in mpeg case for example) */
    ret = avformat_find_stream_info(ic, opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "%s: could not find codec parameters\n", filename);
        if (ic->nb_streams == 0) {
            avformat_close_input(&ic);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
    }

    if (o->start_time_eof != AV_NOPTS_VALUE) {
        if (ic->duration>0) {
            o->start_time = o->start_time_eof + ic->duration;
        } else
            av_log(NULL, AV_LOG_WARNING, "Cannot use -sseof, duration of %s not known\n", filename);
    }
    timestamp = (o->start_time == AV_NOPTS_VALUE) ? 0 : o->start_time;
    /* add the stream start time */
    if (!o->seek_timestamp && ic->start_time != AV_NOPTS_VALUE)
        timestamp += ic->start_time;

    /* if seeking requested, we execute it */
    if (o->start_time != AV_NOPTS_VALUE) {
        int64_t seek_timestamp = timestamp;

        if (!(ic->iformat->flags & AVFMT_SEEK_TO_PTS)) {
            int dts_heuristic = 0;
            for (i=0; i<ic->nb_streams; i++) {
                const AVCodecParameters *par = ic->streams[i]->codecpar;
                if (par->video_delay)
                    dts_heuristic = 1;
            }
            if (dts_heuristic) {
                seek_timestamp -= 3*AV_TIME_BASE / 23;
            }
        }
        ret = avformat_seek_file(ic, -1, INT64_MIN, seek_timestamp, seek_timestamp, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                   filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    /* update the current parameters so that they match the one of the input stream */
    mk_add_input_streams(task,o, ic);

    /* dump the file content */
    av_dump_format(ic, task->nb_input_files, filename, 0);

    GROW_ARRAY(task->input_files, task->nb_input_files);
    f = av_mallocz(sizeof(*f));
    if (!f){
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }

    task->input_files[task->nb_input_files - 1] = f;

    f->ctx        = ic;
    f->ist_index  = task->nb_input_streams - ic->nb_streams;
    f->start_time = o->start_time;
    f->recording_time = o->recording_time;
    f->input_ts_offset = o->input_ts_offset;
    f->ts_offset  = o->input_ts_offset - (task->copy_ts ? (task->start_at_zero && ic->start_time != AV_NOPTS_VALUE ? ic->start_time : 0) : timestamp);
    f->nb_streams = ic->nb_streams;
    f->rate_emu   = o->rate_emu;
    f->accurate_seek = o->accurate_seek;
    f->loop = o->loop;
    f->duration = 0;
    f->time_base = (AVRational){ 1, 1 };
    f->thread_queue_size = o->thread_queue_size > 0 ? o->thread_queue_size : 8;

    /* check if all codec options have been used */
    unused_opts = mk_strip_specifiers(o->g->codec_opts);
    for (i = f->ist_index; i < task->nb_input_streams; i++) {
        e = NULL;
        while ((e = av_dict_get(task->input_streams[i]->decoder_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
            av_dict_set(&unused_opts, e->key, NULL, 0);
    }

    e = NULL;
    while ((e = av_dict_get(unused_opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
        const AVClass *class = avcodec_get_class();
        const AVOption *option = av_opt_find(&class, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        const AVClass *fclass = avformat_get_class();
        const AVOption *foption = av_opt_find(&fclass, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        if (!option || foption)
            continue;


        if (!(option->flags & AV_OPT_FLAG_DECODING_PARAM)) {
            av_log(NULL, AV_LOG_ERROR, "Codec AVOption %s (%s) specified for "
                   "input file #%d (%s) is not a decoding option.\n", e->key,
                   option->help ? option->help : "", task->nb_input_files - 1,
                   filename);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }

        av_log(NULL, AV_LOG_WARNING, "Codec AVOption %s (%s) specified for "
               "input file #%d (%s) has not been used for any stream. The most "
               "likely reason is either wrong type (e.g. a video option with "
               "no video streams) or that it is a private option of some decoder "
               "which was not actually used for any stream.\n", e->key,
               option->help ? option->help : "", task->nb_input_files - 1, filename);
    }
    av_dict_free(&unused_opts);

    for (i = 0; i < o->nb_dump_attachment; i++) {
        int j;

        for (j = 0; j < ic->nb_streams; j++) {
            AVStream *st = ic->streams[j];

            if (mk_check_stream_specifier(ic, st, o->dump_attachment[i].specifier) == 1)
                mk_dump_attachment(task,st, o->dump_attachment[i].u.str);
        }
    }

    for (i = 0; i < orig_nb_streams; i++)
        av_dict_free(&opts[i]);
    av_freep(&opts);

    input_stream_potentially_available = 1;

    return 0;
}

static int mk_choose_encoder(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *s, mk_output_stream_t *ost)
{
    enum AVMediaType type = ost->st->codecpar->codec_type;
    char *codec_name = NULL;

    if (type == AVMEDIA_TYPE_VIDEO || type == AVMEDIA_TYPE_AUDIO || type == AVMEDIA_TYPE_SUBTITLE) {
        MATCH_PER_STREAM_OPT(codec_names, str, codec_name, s, ost->st);
        if (!codec_name) {
            ost->st->codecpar->codec_id = av_guess_codec(s->oformat, NULL, s->filename,
                                                         NULL, ost->st->codecpar->codec_type);
            ost->enc = avcodec_find_encoder(ost->st->codecpar->codec_id);
            if (!ost->enc) {
                av_log(NULL, AV_LOG_FATAL, "Automatic encoder selection failed for "
                       "output stream #%d:%d. Default encoder for format %s (codec %s) is "
                       "probably disabled. Please choose an encoder manually.\n",
                       ost->file_index, ost->index, s->oformat->name,
                       avcodec_get_name(ost->st->codecpar->codec_id));
                return AVERROR_ENCODER_NOT_FOUND;
            }
        } else if (!strcmp(codec_name, "copy"))
            ost->stream_copy = 1;
        else {
            ost->enc = mk_find_codec_or_die(task,codec_name, ost->st->codecpar->codec_type, 1);
            ost->st->codecpar->codec_id = ost->enc->id;
        }
        ost->encoding_needed = !ost->stream_copy;
    } else {
        /* no encoding supported for other media types */
        ost->stream_copy     = 1;
        ost->encoding_needed = 0;
    }
    return 0;
}

static mk_output_stream_t *mk_new_output_stream(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *oc, enum AVMediaType type, int source_index)
{
    mk_output_stream_t *ost;
    AVStream *st = avformat_new_stream(oc, NULL);
    int idx      = oc->nb_streams - 1, ret = 0;
    const char *bsfs = NULL, *time_base = NULL;
    char *next, *codec_tag = NULL;
    double qscale = -1;
    int i;

    if (!st) {
        av_log(NULL, AV_LOG_FATAL, "Could not alloc stream.\n");
        task->running = 0;
        return NULL;
        //exit_program(task,1);
    }

    if (oc->nb_streams - 1 < o->nb_streamid_map)
        st->id = o->streamid_map[oc->nb_streams - 1];

    GROW_ARRAY(task->output_streams, task->nb_output_streams);
    if (!(ost = av_mallocz(sizeof(*ost)))){
        task->running = 0;
        return NULL;
        //exit_program(task,1);
    }

    task->output_streams[task->nb_output_streams - 1] = ost;

    ost->file_index = task->nb_output_files - 1;
    ost->index      = idx;
    ost->st         = st;
    st->codecpar->codec_type = type;
    ret = mk_choose_encoder(task,o, oc, ost);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error selecting an encoder for stream "
               "%d:%d\n", ost->file_index, ost->index);
        task->running = 0;
        return NULL;
    }

    ost->enc_ctx = avcodec_alloc_context3(ost->enc);
    if (!ost->enc_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Error allocating the encoding context.\n");
        task->running = 0;
        return NULL;
        //exit_program(task,1);
    }
    ost->enc_ctx->codec_type = type;

    ost->ref_par = avcodec_parameters_alloc();
    if (!ost->ref_par) {
        av_log(NULL, AV_LOG_ERROR, "Error allocating the encoding parameters.\n");
        task->running = 0;
        return NULL;
    }

    if (ost->enc) {
        ost->encoder_opts  = mk_filter_codec_opts(o->g->codec_opts, ost->enc->id, oc, st, ost->enc);
    } else {
        ost->encoder_opts = mk_filter_codec_opts(o->g->codec_opts, AV_CODEC_ID_NONE, oc, st, NULL);
    }

    MATCH_PER_STREAM_OPT(time_bases, str, time_base, oc, st);
    if (time_base) {
        AVRational q;
        if (av_parse_ratio(&q, time_base, INT_MAX, 0, NULL) < 0 ||
            q.num <= 0 || q.den <= 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid time base: %s\n", time_base);
            task->running = 0;
            return NULL;
        }
        st->time_base = q;
    }

    ost->max_frames = INT64_MAX;
    MATCH_PER_STREAM_OPT(max_frames, i64, ost->max_frames, oc, st);
    for (i = 0; i<o->nb_max_frames; i++) {
        char *p = o->max_frames[i].specifier;
        if (!*p && type != AVMEDIA_TYPE_VIDEO) {
            av_log(NULL, AV_LOG_WARNING, "Applying unspecific -frames to non video streams, maybe you meant -vframes ?\n");
            break;
        }
    }

    ost->copy_prior_start = -1;
    MATCH_PER_STREAM_OPT(copy_prior_start, i, ost->copy_prior_start, oc ,st);


    MATCH_PER_STREAM_OPT(bitstream_filters, str, bsfs, oc, st);
        while (bsfs && *bsfs) {
            const AVBitStreamFilter *filter;
            char *bsf, *bsf_options_str, *bsf_name;

            bsf = av_get_token(&bsfs, ",");
            if (!bsf){
                task->running = 0;
                return NULL;
            }
            bsf_name = av_strtok(bsf, "=", &bsf_options_str);
            if (!bsf_name) {
                task->running = 0;
                return NULL;
            }

            filter = av_bsf_get_by_name(bsf_name);
            if (!filter) {
                av_log(NULL, AV_LOG_FATAL, "Unknown bitstream filter %s\n", bsf_name);
                task->running = 0;
                return NULL;
            }

            ost->bsf_ctx = av_realloc_array(ost->bsf_ctx,
                                            ost->nb_bitstream_filters + 1,
                                            sizeof(*ost->bsf_ctx));
            if (!ost->bsf_ctx) {
                task->running = 0;
                return NULL;
            }

            ret = av_bsf_alloc(filter, &ost->bsf_ctx[ost->nb_bitstream_filters]);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error allocating a bitstream filter context\n");
                task->running = 0;
                return NULL;
            }

            ost->nb_bitstream_filters++;

            if (bsf_options_str && filter->priv_class) {
                const AVOption *opt = av_opt_next(ost->bsf_ctx[ost->nb_bitstream_filters-1]->priv_data, NULL);
                const char * shorthand[2] = {NULL};

                if (opt)
                    shorthand[0] = opt->name;

                ret = av_opt_set_from_string(ost->bsf_ctx[ost->nb_bitstream_filters-1]->priv_data, bsf_options_str, shorthand, "=", ":");
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error parsing options for bitstream filter %s\n", bsf_name);
                    task->running = 0;
                    return NULL;
                }
            }
            av_freep(&bsf);

            if (*bsfs)
                bsfs++;
        }
        if (ost->nb_bitstream_filters) {
            ost->bsf_extradata_updated = av_mallocz_array(ost->nb_bitstream_filters, sizeof(*ost->bsf_extradata_updated));
            if (!ost->bsf_extradata_updated) {
                av_log(NULL, AV_LOG_FATAL, "Bitstream filter memory allocation failed\n");
                task->running = 0;
                return NULL;
        }
    }

    MATCH_PER_STREAM_OPT(codec_tags, str, codec_tag, oc, st);
    if (codec_tag) {
        uint32_t tag = strtol(codec_tag, &next, 0);
        if (*next)
            tag = AV_RL32(codec_tag);
        ost->st->codecpar->codec_tag =
        ost->enc_ctx->codec_tag = tag;
    }

    MATCH_PER_STREAM_OPT(qscale, dbl, qscale, oc, st);
    if (qscale >= 0) {
        ost->enc_ctx->flags |= AV_CODEC_FLAG_QSCALE;
        ost->enc_ctx->global_quality = FF_QP2LAMBDA * qscale;
    }

    MATCH_PER_STREAM_OPT(disposition, str, ost->disposition, oc, st);
    ost->disposition = av_strdup(ost->disposition);

    ost->max_muxing_queue_size = 128;
    MATCH_PER_STREAM_OPT(max_muxing_queue_size, i, ost->max_muxing_queue_size, oc, st);
    ost->max_muxing_queue_size *= sizeof(AVPacket);

    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        ost->enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_dict_copy(&ost->sws_dict, o->g->sws_dict, 0);

    av_dict_copy(&ost->swr_opts, o->g->swr_opts, 0);
    if (ost->enc && av_get_exact_bits_per_sample(ost->enc->id) == 24)
        av_dict_set(&ost->swr_opts, "output_sample_bits", "24", 0);

    av_dict_copy(&ost->resample_opts, o->g->resample_opts, 0);

    ost->source_index = source_index;
    if (source_index >= 0) {
        ost->sync_ist = task->input_streams[source_index];
        task->input_streams[source_index]->discard = 0;
        task->input_streams[source_index]->st->discard = task->input_streams[source_index]->user_set_discard;
    }
    ost->last_mux_dts = AV_NOPTS_VALUE;

    ost->muxing_queue = av_fifo_alloc(8 * sizeof(AVPacket));
    if (!ost->muxing_queue) {
        task->running = 0;
        return NULL;
    }

    return ost;
}

static void mk_parse_matrix_coeffs(mk_task_ctx_t* task,uint16_t *dest, const char *str)
{
    int i;
    const char *p = str;
    for (i = 0;; i++) {
        dest[i] = atoi(p);
        if (i == 63)
            break;
        p = strchr(p, ',');
        if (!p) {
            av_log(NULL, AV_LOG_FATAL, "Syntax error in matrix \"%s\" at coeff %d\n", str, i);
            task->running = 0;
            return;
            //exit_program(task,1);
        }
        p++;
    }
}

/* read file contents into a string */
static uint8_t *mk_read_file(const char *filename)
{
    AVIOContext *pb      = NULL;
    AVIOContext *dyn_buf = NULL;
    int ret = avio_open(&pb, filename, AVIO_FLAG_READ);
    uint8_t buf[1024], *str;

    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error opening file %s.\n", filename);
        return NULL;
    }

    ret = avio_open_dyn_buf(&dyn_buf);
    if (ret < 0) {
        avio_closep(&pb);
        return NULL;
    }
    while ((ret = avio_read(pb, buf, sizeof(buf))) > 0)
        avio_write(dyn_buf, buf, ret);
    avio_w8(dyn_buf, 0);
    avio_closep(&pb);

    ret = avio_close_dyn_buf(dyn_buf, &str);
    if (ret < 0)
        return NULL;
    return str;
}

static char *mk_get_ost_filters(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *oc,
                             mk_output_stream_t *ost)
{
    AVStream *st = ost->st;

    if (ost->filters_script && ost->filters) {
        av_log(NULL, AV_LOG_ERROR, "Both -filter and -filter_script set for "
               "output stream #%d:%d.\n", task->nb_output_files, st->index);
        task->running = 0;
        return NULL;
        //exit_program(task,1);
    }

    if (ost->filters_script)
        return mk_read_file(ost->filters_script);
    else if (ost->filters)
        return av_strdup(ost->filters);

    return av_strdup(st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ?
                     "null" : "anull");
}

static void mk_check_streamcopy_filters(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *oc,
                                     const mk_output_stream_t *ost, enum AVMediaType type)
{
    if (ost->filters_script || ost->filters) {
        av_log(NULL, AV_LOG_ERROR,
               "%s '%s' was defined for %s output stream %d:%d but codec copy was selected.\n"
               "Filtering and streamcopy cannot be used together.\n",
               ost->filters ? "Filtergraph" : "Filtergraph script",
               ost->filters ? ost->filters : ost->filters_script,
               av_get_media_type_string(type), ost->file_index, ost->index);
        task->running = 0;
        //exit_program(task,1);
    }
}

static mk_output_stream_t *mk_new_video_stream(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *oc, int source_index)
{
    AVStream *st;
    mk_output_stream_t *ost;
    AVCodecContext *video_enc;
    char *frame_rate = NULL, *frame_aspect_ratio = NULL;

    ost = mk_new_output_stream(task,o, oc, AVMEDIA_TYPE_VIDEO, source_index);
    st  = ost->st;
    video_enc = ost->enc_ctx;

    MATCH_PER_STREAM_OPT(frame_rates, str, frame_rate, oc, st);
    if (frame_rate && av_parse_video_rate(&ost->frame_rate, frame_rate) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Invalid framerate value: %s\n", frame_rate);
        task->running = 0;
        return NULL;
        //exit_program(task,1);
    }
    if (frame_rate && task->video_sync_method == VSYNC_PASSTHROUGH)
        av_log(NULL, AV_LOG_ERROR, "Using -vsync 0 and -r can produce invalid output files\n");

    MATCH_PER_STREAM_OPT(frame_aspect_ratios, str, frame_aspect_ratio, oc, st);
    if (frame_aspect_ratio) {
        AVRational q;
        if (av_parse_ratio(&q, frame_aspect_ratio, 255, 0, NULL) < 0 ||
            q.num <= 0 || q.den <= 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid aspect ratio: %s\n", frame_aspect_ratio);
            task->running = 0;
            return NULL;
            //exit_program(task,1);
        }
        ost->frame_aspect_ratio = q;
    }

    MATCH_PER_STREAM_OPT(filter_scripts, str, ost->filters_script, oc, st);
    MATCH_PER_STREAM_OPT(filters,        str, ost->filters,        oc, st);

    if (!ost->stream_copy) {
        const char *p = NULL;
        char *frame_size = NULL;
        char *frame_pix_fmt = NULL;
        char *intra_matrix = NULL, *inter_matrix = NULL;
        char *chroma_intra_matrix = NULL;
        int do_pass = 0;
        int i;

        MATCH_PER_STREAM_OPT(frame_sizes, str, frame_size, oc, st);
        if (frame_size && av_parse_video_size(&video_enc->width, &video_enc->height, frame_size) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid frame size: %s.\n", frame_size);
            task->running = 0;
            return NULL;
            //exit_program(task,1);
        }

        video_enc->bits_per_raw_sample = task->frame_bits_per_raw_sample;
        MATCH_PER_STREAM_OPT(frame_pix_fmts, str, frame_pix_fmt, oc, st);
        if (frame_pix_fmt && *frame_pix_fmt == '+') {
            ost->keep_pix_fmt = 1;
            if (!*++frame_pix_fmt)
                frame_pix_fmt = NULL;
        }
        if (frame_pix_fmt && (video_enc->pix_fmt = av_get_pix_fmt(frame_pix_fmt)) == AV_PIX_FMT_NONE) {
            av_log(NULL, AV_LOG_FATAL, "Unknown pixel format requested: %s.\n", frame_pix_fmt);
            task->running = 0;
            return NULL;
            //exit_program(task,1);
        }
        st->sample_aspect_ratio = video_enc->sample_aspect_ratio;

        if (intra_only)
            video_enc->gop_size = 0;
        MATCH_PER_STREAM_OPT(intra_matrices, str, intra_matrix, oc, st);
        if (intra_matrix) {
            if (!(video_enc->intra_matrix = av_mallocz(sizeof(*video_enc->intra_matrix) * 64))) {
                av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for intra matrix.\n");
                task->running = 0;
                return NULL;
                //exit_program(task,1);
            }
            mk_parse_matrix_coeffs(task,video_enc->intra_matrix, intra_matrix);
        }
        MATCH_PER_STREAM_OPT(chroma_intra_matrices, str, chroma_intra_matrix, oc, st);
        if (chroma_intra_matrix) {
            uint16_t *p = av_mallocz(sizeof(*video_enc->chroma_intra_matrix) * 64);
            if (!p) {
                av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for intra matrix.\n");
                task->running = 0;
                return NULL;
                //exit_program(task,1);
            }
            av_codec_set_chroma_intra_matrix(video_enc, p);
            mk_parse_matrix_coeffs(task,p, chroma_intra_matrix);
        }
        MATCH_PER_STREAM_OPT(inter_matrices, str, inter_matrix, oc, st);
        if (inter_matrix) {
            if (!(video_enc->inter_matrix = av_mallocz(sizeof(*video_enc->inter_matrix) * 64))) {
                av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for inter matrix.\n");
                task->running = 0;
                return NULL;
                //exit_program(task,1);
            }
            mk_parse_matrix_coeffs(task,video_enc->inter_matrix, inter_matrix);
        }

        MATCH_PER_STREAM_OPT(rc_overrides, str, p, oc, st);
        for (i = 0; p; i++) {
            int start, end, q;
            int e = sscanf(p, "%d,%d,%d", &start, &end, &q);
            if (e != 3) {
                av_log(NULL, AV_LOG_FATAL, "error parsing rc_override\n");
                task->running = 0;
                return NULL;
                //exit_program(task,1);
            }
            video_enc->rc_override =
                av_realloc_array(video_enc->rc_override,
                                 i + 1, sizeof(RcOverride));
            if (!video_enc->rc_override) {
                av_log(NULL, AV_LOG_FATAL, "Could not (re)allocate memory for rc_override.\n");
                task->running = 0;
                return NULL;
                //exit_program(task,1);
            }
            video_enc->rc_override[i].start_frame = start;
            video_enc->rc_override[i].end_frame   = end;
            if (q > 0) {
                video_enc->rc_override[i].qscale         = q;
                video_enc->rc_override[i].quality_factor = 1.0;
            }
            else {
                video_enc->rc_override[i].qscale         = 0;
                video_enc->rc_override[i].quality_factor = -q/100.0;
            }
            p = strchr(p, '/');
            if (p) p++;
        }
        video_enc->rc_override_count = i;

        if (do_psnr)
            video_enc->flags|= AV_CODEC_FLAG_PSNR;

        /* two pass mode */
        MATCH_PER_STREAM_OPT(pass, i, do_pass, oc, st);
        if (do_pass) {
            if (do_pass & 1) {
                video_enc->flags |= AV_CODEC_FLAG_PASS1;
                av_dict_set(&ost->encoder_opts, "flags", "+pass1", AV_DICT_APPEND);
            }
            if (do_pass & 2) {
                video_enc->flags |= AV_CODEC_FLAG_PASS2;
                av_dict_set(&ost->encoder_opts, "flags", "+pass2", AV_DICT_APPEND);
            }
        }

        MATCH_PER_STREAM_OPT(passlogfiles, str, ost->logfile_prefix, oc, st);
        if (ost->logfile_prefix &&
            !(ost->logfile_prefix = av_strdup(ost->logfile_prefix))) {
                task->running = 0;
                return NULL;
                //exit_program(task,1);
            }


        if (do_pass) {
            char logfilename[1024];
            FILE *f;

            snprintf(logfilename, sizeof(logfilename), "%s-%d.log",
                     ost->logfile_prefix ? ost->logfile_prefix :
                                           DEFAULT_PASS_LOGFILENAME_PREFIX,
                     i);
            if (!strcmp(ost->enc->name, "libx264")) {
                av_dict_set(&ost->encoder_opts, "stats", logfilename, AV_DICT_DONT_OVERWRITE);
            } else {
                if (video_enc->flags & AV_CODEC_FLAG_PASS2) {
                    char  *logbuffer = mk_read_file(logfilename);

                    if (!logbuffer) {
                        av_log(NULL, AV_LOG_FATAL, "Error reading log file '%s' for pass-2 encoding\n",
                               logfilename);
                        task->running = 0;
                        return NULL;
                        //exit_program(task,1);
                    }
                    video_enc->stats_in = logbuffer;
                }
                if (video_enc->flags & AV_CODEC_FLAG_PASS1) {
                    f = av_fopen_utf8(logfilename, "wb");
                    if (!f) {
                        av_log(NULL, AV_LOG_FATAL,
                               "Cannot write log file '%s' for pass-1 encoding: %s\n",
                               logfilename, strerror(errno));
                        task->running = 0;
                        return NULL;
                        //exit_program(task,1);
                    }
                    ost->logfile = f;
                }
            }
        }

        MATCH_PER_STREAM_OPT(forced_key_frames, str, ost->forced_keyframes, oc, st);
        if (ost->forced_keyframes)
            ost->forced_keyframes = av_strdup(ost->forced_keyframes);

        MATCH_PER_STREAM_OPT(force_fps, i, ost->force_fps, oc, st);

        ost->top_field_first = -1;
        MATCH_PER_STREAM_OPT(top_field_first, i, ost->top_field_first, oc, st);


        ost->avfilter = mk_get_ost_filters(task,o, oc, ost);
        if (!ost->avfilter){
            task->running = 0;
            return NULL;
            //exit_program(task,1);
        }
    } else {
        MATCH_PER_STREAM_OPT(copy_initial_nonkeyframes, i, ost->copy_initial_nonkeyframes, oc ,st);
    }

    if (ost->stream_copy)
        mk_check_streamcopy_filters(task,o, oc, ost, AVMEDIA_TYPE_VIDEO);

    return ost;
}

static mk_output_stream_t *mk_new_audio_stream(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *oc, int source_index)
{
    int n;
    AVStream *st;
    mk_output_stream_t *ost;
    AVCodecContext *audio_enc;

    ost = mk_new_output_stream(task,o, oc, AVMEDIA_TYPE_AUDIO, source_index);
    st  = ost->st;

    audio_enc = ost->enc_ctx;
    audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;

    MATCH_PER_STREAM_OPT(filter_scripts, str, ost->filters_script, oc, st);
    MATCH_PER_STREAM_OPT(filters,        str, ost->filters,        oc, st);

    if (!ost->stream_copy) {
        char *sample_fmt = NULL;

        MATCH_PER_STREAM_OPT(audio_channels, i, audio_enc->channels, oc, st);

        MATCH_PER_STREAM_OPT(sample_fmts, str, sample_fmt, oc, st);
        if (sample_fmt &&
            (audio_enc->sample_fmt = av_get_sample_fmt(sample_fmt)) == AV_SAMPLE_FMT_NONE) {
            av_log(NULL, AV_LOG_FATAL, "Invalid sample format '%s'\n", sample_fmt);
            task->running = 0;
            return NULL;
            //exit_program(task,1);
        }

        MATCH_PER_STREAM_OPT(audio_sample_rate, i, audio_enc->sample_rate, oc, st);

        MATCH_PER_STREAM_OPT(apad, str, ost->apad, oc, st);
        ost->apad = av_strdup(ost->apad);

        ost->avfilter = mk_get_ost_filters(task,o, oc, ost);
        if (!ost->avfilter){
            task->running = 0;
            return;
            //exit_program(task,1);
        }

        /* check for channel mapping for this audio stream */
        for (n = 0; n < o->nb_audio_channel_maps; n++) {
            mk_audio_channel_map_t *map = &o->audio_channel_maps[n];
            if ((map->ofile_idx   == -1 || ost->file_index == map->ofile_idx) &&
                (map->ostream_idx == -1 || ost->st->index  == map->ostream_idx)) {
                mk_input_stream_t *ist;

                if (map->channel_idx == -1) {
                    ist = NULL;
                } else if (ost->source_index < 0) {
                    av_log(NULL, AV_LOG_FATAL, "Cannot determine input stream for channel mapping %d.%d\n",
                           ost->file_index, ost->st->index);
                    continue;
                } else {
                    ist = task->input_streams[ost->source_index];
                }

                if (!ist || (ist->file_index == map->file_idx && ist->st->index == map->stream_idx)) {
                    if (av_reallocp_array(&ost->audio_channels_map,
                                          ost->audio_channels_mapped + 1,
                                          sizeof(*ost->audio_channels_map)
                                          ) < 0 ){
                        task->running = 0;
                        return NULL;
                        // exit_program(task,1);
                    }

                    ost->audio_channels_map[ost->audio_channels_mapped++] = map->channel_idx;
                }
            }
        }
    }

    if (ost->stream_copy)
        mk_check_streamcopy_filters(task,o, oc, ost, AVMEDIA_TYPE_AUDIO);

    return ost;
}

static mk_output_stream_t *mk_new_data_stream(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *oc, int source_index)
{
    mk_output_stream_t *ost;

    ost = mk_new_output_stream(task,o, oc, AVMEDIA_TYPE_DATA, source_index);
    if (!ost->stream_copy) {
        av_log(NULL, AV_LOG_FATAL, "Data stream encoding not supported yet (only streamcopy)\n");
        task->running = 0;
        return NULL;
        //exit_program(task,1);
    }

    return ost;
}

static mk_output_stream_t *mk_new_unknown_stream(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *oc, int source_index)
{
    mk_output_stream_t *ost;

    ost = mk_new_output_stream(task,o, oc, AVMEDIA_TYPE_UNKNOWN, source_index);
    if (!ost->stream_copy) {
        av_log(NULL, AV_LOG_FATAL, "Unknown stream encoding not supported yet (only streamcopy)\n");
        task->running = 0;
        return NULL;
        //exit_program(task,1);
    }

    return ost;
}

static mk_output_stream_t *mk_new_attachment_stream(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *oc, int source_index)
{
    mk_output_stream_t *ost = mk_new_output_stream(task,o, oc, AVMEDIA_TYPE_ATTACHMENT, source_index);
    ost->stream_copy = 1;
    ost->finished    = 1;
    return ost;
}

static mk_output_stream_t *mk_new_subtitle_stream(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *oc, int source_index)
{
    AVStream *st;
    mk_output_stream_t *ost;
    AVCodecContext *subtitle_enc;

    ost = mk_new_output_stream(task,o, oc, AVMEDIA_TYPE_SUBTITLE, source_index);
    st  = ost->st;
    subtitle_enc = ost->enc_ctx;

    subtitle_enc->codec_type = AVMEDIA_TYPE_SUBTITLE;

    MATCH_PER_STREAM_OPT(copy_initial_nonkeyframes, i, ost->copy_initial_nonkeyframes, oc, st);

    if (!ost->stream_copy) {
        char *frame_size = NULL;

        MATCH_PER_STREAM_OPT(frame_sizes, str, frame_size, oc, st);
        if (frame_size && av_parse_video_size(&subtitle_enc->width, &subtitle_enc->height, frame_size) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid frame size: %s.\n", frame_size);
            task->running = 0;
            return NULL;
            //exit_program(task,1);
        }
    }

    return ost;
}

/* arg format is "output-stream-index:streamid-value". */
static int mk_opt_streamid(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    int idx;
    char *p;
    char idx_str[16];

    av_strlcpy(idx_str, arg, sizeof(idx_str));
    p = strchr(idx_str, ':');
    if (!p) {
        av_log(NULL, AV_LOG_FATAL,
               "Invalid value '%s' for option '%s', required syntax is 'index:value'\n",
               arg, opt);
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }
    *p++ = '\0';
    idx = mk_parse_number_or_die(task,opt, idx_str, OPT_INT, 0, MAX_STREAMS-1);
    o->streamid_map = mk_grow_array(o->streamid_map, sizeof(*o->streamid_map), &o->nb_streamid_map, idx+1);
    o->streamid_map[idx] = mk_parse_number_or_die(task,opt, p, OPT_INT, 0, INT_MAX);
    return 0;
}

static int mk_copy_chapters(mk_input_file_t *ifile, mk_output_file_t *ofile, int copy_metadata)
{
    AVFormatContext *is = ifile->ctx;
    AVFormatContext *os = ofile->ctx;
    AVChapter **tmp;
    int i;

    tmp = av_realloc_f(os->chapters, is->nb_chapters + os->nb_chapters, sizeof(*os->chapters));
    if (!tmp)
        return AVERROR(ENOMEM);
    os->chapters = tmp;

    for (i = 0; i < is->nb_chapters; i++) {
        AVChapter *in_ch = is->chapters[i], *out_ch;
        int64_t start_time = (ofile->start_time == AV_NOPTS_VALUE) ? 0 : ofile->start_time;
        int64_t ts_off   = av_rescale_q(start_time - ifile->ts_offset,
                                       AV_TIME_BASE_Q, in_ch->time_base);
        int64_t rt       = (ofile->recording_time == INT64_MAX) ? INT64_MAX :
                           av_rescale_q(ofile->recording_time, AV_TIME_BASE_Q, in_ch->time_base);


        if (in_ch->end < ts_off)
            continue;
        if (rt != INT64_MAX && in_ch->start > rt + ts_off)
            break;

        out_ch = av_mallocz(sizeof(AVChapter));
        if (!out_ch)
            return AVERROR(ENOMEM);

        out_ch->id        = in_ch->id;
        out_ch->time_base = in_ch->time_base;
        out_ch->start     = FFMAX(0,  in_ch->start - ts_off);
        out_ch->end       = FFMIN(rt, in_ch->end   - ts_off);

        if (copy_metadata)
            av_dict_copy(&out_ch->metadata, in_ch->metadata, 0);

        os->chapters[os->nb_chapters++] = out_ch;
    }
    return 0;
}

static int mk_read_ffserver_streams(mk_task_ctx_t* task,mk_option_ctx_t *o, AVFormatContext *s, const char *filename)
{
    int i, err;
    AVFormatContext *ic = avformat_alloc_context();

    ic->flags |= AVFMT_FLAG_KEEP_SIDE_DATA;
    ic->interrupt_callback = task->int_cb;
    err = avformat_open_input(&ic, filename, NULL, NULL);
    if (err < 0)
        return err;
    /* copy stream format */
    for(i=0;i<ic->nb_streams;i++) {
        AVStream *st;
        mk_output_stream_t *ost;
        AVCodec *codec;
        const char *enc_config;

        codec = avcodec_find_encoder(ic->streams[i]->codecpar->codec_id);
        if (!codec) {
            av_log(s, AV_LOG_ERROR, "no encoder found for codec id %i\n", ic->streams[i]->codecpar->codec_id);
            return AVERROR(EINVAL);
        }
        if (codec->type == AVMEDIA_TYPE_AUDIO)
            mk_opt_audio_codec(task,o, "c:a", codec->name);
        else if (codec->type == AVMEDIA_TYPE_VIDEO)
            mk_opt_video_codec(task,o, "c:v", codec->name);
        ost   = mk_new_output_stream(task,o, s, codec->type, -1);
        st    = ost->st;

        avcodec_get_context_defaults3(st->codec, codec);
        enc_config = av_stream_get_recommended_encoder_configuration(ic->streams[i]);
        if (enc_config) {
            AVDictionary *opts = NULL;
            av_dict_parse_string(&opts, enc_config, "=", ",", 0);
            av_opt_set_dict2(st->codec, &opts, AV_OPT_SEARCH_CHILDREN);
            av_dict_free(&opts);
        }

        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !ost->stream_copy)
            mk_choose_sample_fmt(st, codec);
        else if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !ost->stream_copy)
            mk_choose_pixel_fmt(st, st->codec, codec, st->codecpar->format);
        avcodec_copy_context(ost->enc_ctx, st->codec);
        if (enc_config)
            av_dict_parse_string(&ost->encoder_opts, enc_config, "=", ",", 0);
    }

    avformat_close_input(&ic);
    return err;
}

static void mk_init_output_filter(mk_task_ctx_t* task,mk_output_filter_t *ofilter, mk_option_ctx_t *o,
                               AVFormatContext *oc)
{
    mk_output_stream_t *ost;

    switch (ofilter->type) {
    case AVMEDIA_TYPE_VIDEO: ost = mk_new_video_stream(task,o, oc, -1); break;
    case AVMEDIA_TYPE_AUDIO: ost = mk_new_audio_stream(task,o, oc, -1); break;
    default:
        av_log(NULL, AV_LOG_FATAL, "Only video and audio filters are supported "
               "currently.\n");
        task->running = 0;
        return;
        //exit_program(task,1);
    }

    ost->source_index = -1;
    ost->filter       = ofilter;

    ofilter->ost      = ost;
    ofilter->format   = -1;

    if (ost->stream_copy) {
        av_log(NULL, AV_LOG_ERROR, "Streamcopy requested for output stream %d:%d, "
               "which is fed from a complex filtergraph. Filtering and streamcopy "
               "cannot be used together.\n", ost->file_index, ost->index);
        task->running = 0;
        return;
        //exit_program(task,1);
    }

    if (ost->avfilter && (ost->filters || ost->filters_script)) {
        const char *opt = ost->filters ? "-vf/-af/-filter" : "-filter_script";
        av_log(NULL, AV_LOG_ERROR,
               "%s '%s' was specified through the %s option "
               "for output stream %d:%d, which is fed from a complex filtergraph.\n"
               "%s and -filter_complex cannot be used together for the same stream.\n",
               ost->filters ? "Filtergraph" : "Filtergraph script",
               ost->filters ? ost->filters : ost->filters_script,
               opt, ost->file_index, ost->index, opt);
        task->running = 0;
        return;
        //exit_program(task,1);
    }

    avfilter_inout_free(&ofilter->out_tmp);
}

static int mk_init_complex_filters(mk_task_ctx_t* task)
{
    int i, ret = 0;

    for (i = 0; i < task->nb_filtergraphs; i++) {
        ret = mk_init_complex_filtergraph(task,task->filtergraphs[i]);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int mk_open_output_file(mk_task_ctx_t* task,mk_option_ctx_t *o, const char *filename)
{
    AVFormatContext *oc;
    int i, j, err;
    AVOutputFormat *file_oformat;
    mk_output_file_t *of;
    mk_output_stream_t *ost;
    mk_input_stream_t  *ist;
    AVDictionary *unused_opts = NULL;
    AVDictionaryEntry *e = NULL;
    int format_flags = 0;


    if (o->stop_time != INT64_MAX && o->recording_time != INT64_MAX) {
        o->stop_time = INT64_MAX;
        av_log(NULL, AV_LOG_WARNING, "-t and -to cannot be used together; using -t.\n");
    }

    if (o->stop_time != INT64_MAX && o->recording_time == INT64_MAX) {
        int64_t start_time = o->start_time == AV_NOPTS_VALUE ? 0 : o->start_time;
        if (o->stop_time <= start_time) {
            av_log(NULL, AV_LOG_ERROR, "-to value smaller than -ss; aborting.\n");
            task->running = 0;
            return -1;
            //exit_program(task,1);
        } else {
            o->recording_time = o->stop_time - start_time;
        }
    }

    GROW_ARRAY(task->output_files, task->nb_output_files);
    of = av_mallocz(sizeof(*of));
    if (!of){
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }
    task->output_files[task->nb_output_files - 1] = of;

    of->ost_index      = task->nb_output_streams;
    of->recording_time = o->recording_time;
    of->start_time     = o->start_time;
    of->limit_filesize = o->limit_filesize;
    of->shortest       = o->shortest;
    av_dict_copy(&of->opts, o->g->format_opts, 0);

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    err = avformat_alloc_output_context2(&oc, NULL, o->format, filename);
    if (!oc) {
        mk_log_error(filename, err);
        task->running = 0;
        //exit_program(task,1);
        return -1;
    }

    of->ctx = oc;
    if (o->recording_time != INT64_MAX)
        oc->duration = o->recording_time;

    file_oformat= oc->oformat;
    oc->interrupt_callback = task->int_cb;

    e = av_dict_get(o->g->format_opts, "fflags", NULL, 0);
    if (e) {
        const AVOption *o = av_opt_find(oc, "fflags", NULL, 0, 0);
        av_opt_eval_flags(oc, o, e->value, &format_flags);
    }

    /* create streams for all unlabeled output pads */
    for (i = 0; i < task->nb_filtergraphs; i++) {
        mk_filter_graph_t *fg = task->filtergraphs[i];
        for (j = 0; j < fg->nb_outputs; j++) {
            mk_output_filter_t *ofilter = fg->outputs[j];

            if (!ofilter->out_tmp || ofilter->out_tmp->name)
                continue;

            switch (ofilter->type) {
            case AVMEDIA_TYPE_VIDEO:    o->video_disable    = 1; break;
            case AVMEDIA_TYPE_AUDIO:    o->audio_disable    = 1; break;
            case AVMEDIA_TYPE_SUBTITLE: o->subtitle_disable = 1; break;
            }
            mk_init_output_filter(task,ofilter, o, oc);
        }
    }

    /* ffserver seeking with date=... needs a date reference */
    if (!strcmp(file_oformat->name, "ffm") &&
        !(format_flags & AVFMT_FLAG_BITEXACT) &&
        av_strstart(filename, "http:", NULL)) {
        int err = mk_parse_option(task,o, "metadata", "creation_time=now", options);
        if (err < 0) {
            mk_log_error(filename, err);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
    }

    if (!strcmp(file_oformat->name, "ffm") && !override_ffserver &&
        av_strstart(filename, "http:", NULL)) {
        int j;
        /* special case for files sent to ffserver: we get the stream
           parameters from ffserver */
        int err = mk_read_ffserver_streams(task,o, oc, filename);
        if (err < 0) {
            mk_log_error(filename, err);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
        for(j = task->nb_output_streams - oc->nb_streams; j < task->nb_output_streams; j++) {
            ost = task->output_streams[j];
            for (i = 0; i < task->nb_input_streams; i++) {
                ist = task->input_streams[i];
                if(ist->st->codecpar->codec_type == ost->st->codecpar->codec_type){
                    ost->sync_ist= ist;
                    ost->source_index= i;
                    if(ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ost->avfilter = av_strdup("anull");
                    if(ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ost->avfilter = av_strdup("null");
                    ist->discard = 0;
                    ist->st->discard = ist->user_set_discard;
                    break;
                }
            }
            if(!ost->sync_ist){
                av_log(NULL, AV_LOG_FATAL, "Missing %s stream which is required by this ffm\n", av_get_media_type_string(ost->st->codecpar->codec_type));
                task->running = 0;
                return -1;
                //exit_program(task,1);
            }
        }
    } else if (!o->nb_stream_maps) {
        char *subtitle_codec_name = NULL;
        /* pick the "best" stream of each type */

        /* video: highest resolution */
        if (!o->video_disable && av_guess_codec(oc->oformat, NULL, filename, NULL, AVMEDIA_TYPE_VIDEO) != AV_CODEC_ID_NONE) {
            int area = 0, idx = -1;
            int qcr = avformat_query_codec(oc->oformat, oc->oformat->video_codec, 0);
            for (i = 0; i < task->nb_input_streams; i++) {
                int new_area;
                ist = task->input_streams[i];
                new_area = ist->st->codecpar->width * ist->st->codecpar->height + 100000000*!!ist->st->codec_info_nb_frames;
                if((qcr!=MKTAG('A', 'P', 'I', 'C')) && (ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC))
                    new_area = 1;
                if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                    new_area > area) {
                    if((qcr==MKTAG('A', 'P', 'I', 'C')) && !(ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC))
                        continue;
                    area = new_area;
                    idx = i;
                }
            }
            if (idx >= 0)
                mk_new_video_stream(task,o, oc, idx);
        }

        /* audio: most channels */
        if (!o->audio_disable && av_guess_codec(oc->oformat, NULL, filename, NULL, AVMEDIA_TYPE_AUDIO) != AV_CODEC_ID_NONE) {
            int best_score = 0, idx = -1;
            for (i = 0; i < task->nb_input_streams; i++) {
                int score;
                ist = task->input_streams[i];
                score = ist->st->codecpar->channels + 100000000*!!ist->st->codec_info_nb_frames;
                if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                    score > best_score) {
                    best_score = score;
                    idx = i;
                }
            }
            if (idx >= 0)
                mk_new_audio_stream(task,o, oc, idx);
        }

        /* subtitles: pick first */
        MATCH_PER_TYPE_OPT(codec_names, str, subtitle_codec_name, oc, "s");
        if (!o->subtitle_disable && (avcodec_find_encoder(oc->oformat->subtitle_codec) || subtitle_codec_name)) {
            for (i = 0; i < task->nb_input_streams; i++)
                if (task->input_streams[i]->st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                    AVCodecDescriptor const *input_descriptor =
                        avcodec_descriptor_get(task->input_streams[i]->st->codecpar->codec_id);
                    AVCodecDescriptor const *output_descriptor = NULL;
                    AVCodec const *output_codec =
                        avcodec_find_encoder(oc->oformat->subtitle_codec);
                    int input_props = 0, output_props = 0;
                    if (output_codec)
                        output_descriptor = avcodec_descriptor_get(output_codec->id);
                    if (input_descriptor)
                        input_props = input_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
                    if (output_descriptor)
                        output_props = output_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
                    if (subtitle_codec_name ||
                        input_props & output_props ||
                        // Map dvb teletext which has neither property to any output subtitle encoder
                        input_descriptor && output_descriptor &&
                        (!input_descriptor->props ||
                         !output_descriptor->props)) {
                        mk_new_subtitle_stream(task,o, oc, i);
                        break;
                    }
                }
        }
        /* Data only if codec id match */
        if (!o->data_disable ) {
            enum AVCodecID codec_id = av_guess_codec(oc->oformat, NULL, filename, NULL, AVMEDIA_TYPE_DATA);
            for (i = 0; codec_id != AV_CODEC_ID_NONE && i < task->nb_input_streams; i++) {
                if (task->input_streams[i]->st->codecpar->codec_type == AVMEDIA_TYPE_DATA
                    && task->input_streams[i]->st->codecpar->codec_id == codec_id )
                    mk_new_data_stream(task,o, oc, i);
            }
        }
    } else {
        for (i = 0; i < o->nb_stream_maps; i++) {
            mk_stream_map_t *map = &o->stream_maps[i];

            if (map->disabled)
                continue;

            if (map->linklabel) {
                mk_filter_graph_t *fg;
                mk_output_filter_t *ofilter = NULL;
                int j, k;

                for (j = 0; j < task->nb_filtergraphs; j++) {
                    fg = task->filtergraphs[j];
                    for (k = 0; k < fg->nb_outputs; k++) {
                        AVFilterInOut *out = fg->outputs[k]->out_tmp;
                        if (out && !strcmp(out->name, map->linklabel)) {
                            ofilter = fg->outputs[k];
                            goto loop_end;
                        }
                    }
                }
loop_end:
                if (!ofilter) {
                    av_log(NULL, AV_LOG_FATAL, "Output with label '%s' does not exist "
                           "in any defined filter graph, or was already used elsewhere.\n", map->linklabel);
                    task->running = 0;
                    return -1;
                    //exit_program(task,1);
                }
                mk_init_output_filter(task,ofilter, o, oc);
            } else {
                int src_idx = task->input_files[map->file_index]->ist_index + map->stream_index;

                ist = task->input_streams[task->input_files[map->file_index]->ist_index + map->stream_index];
                if(o->subtitle_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
                    continue;
                if(o->audio_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                    continue;
                if(o->video_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                    continue;
                if(o->data_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_DATA)
                    continue;

                ost = NULL;
                switch (ist->st->codecpar->codec_type) {
                case AVMEDIA_TYPE_VIDEO:      ost = mk_new_video_stream     (task,o, oc, src_idx); break;
                case AVMEDIA_TYPE_AUDIO:      ost = mk_new_audio_stream     (task,o, oc, src_idx); break;
                case AVMEDIA_TYPE_SUBTITLE:   ost = mk_new_subtitle_stream  (task,o, oc, src_idx); break;
                case AVMEDIA_TYPE_DATA:       ost = mk_new_data_stream      (task,o, oc, src_idx); break;
                case AVMEDIA_TYPE_ATTACHMENT: ost = mk_new_attachment_stream(task,o, oc, src_idx); break;
                case AVMEDIA_TYPE_UNKNOWN:
                    if (copy_unknown_streams) {
                        ost = mk_new_unknown_stream(task,o, oc, src_idx);
                        break;
                    }
                default:
                    av_log(NULL, ignore_unknown_streams ? AV_LOG_WARNING : AV_LOG_FATAL,
                           "Cannot map stream #%d:%d - unsupported type.\n",
                           map->file_index, map->stream_index);
                    if (!ignore_unknown_streams) {
                        av_log(NULL, AV_LOG_FATAL,
                               "If you want unsupported types ignored instead "
                               "of failing, please use the -ignore_unknown option\n"
                               "If you want them copied, please use -copy_unknown\n");
                        task->running = 0;
                        return -1;
                        //exit_program(task,1);
                    }
                }
                if (ost)
                    ost->sync_ist = task->input_streams[task->input_files[map->sync_file_index]->ist_index
                                                  + map->sync_stream_index];
            }
        }
    }

    /* handle attached files */
    for (i = 0; i < o->nb_attachments; i++) {
        AVIOContext *pb;
        uint8_t *attachment;
        const char *p;
        int64_t len;

        if ((err = avio_open2(&pb, o->attachments[i], AVIO_FLAG_READ, &task->int_cb, NULL)) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Could not open attachment file %s.\n",
                   o->attachments[i]);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
        if ((len = avio_size(pb)) <= 0) {
            av_log(NULL, AV_LOG_FATAL, "Could not get size of the attachment %s.\n",
                   o->attachments[i]);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
        if (!(attachment = av_malloc(len))) {
            av_log(NULL, AV_LOG_FATAL, "Attachment %s too large to fit into memory.\n",
                   o->attachments[i]);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
        avio_read(pb, attachment, len);

        ost = mk_new_attachment_stream(task,o, oc, -1);
        ost->stream_copy               = 0;
        ost->attachment_filename       = o->attachments[i];
        ost->st->codecpar->extradata      = attachment;
        ost->st->codecpar->extradata_size = len;

        p = strrchr(o->attachments[i], '/');
        av_dict_set(&ost->st->metadata, "filename", (p && *p) ? p + 1 : o->attachments[i], AV_DICT_DONT_OVERWRITE);
        avio_closep(&pb);
    }

#if FF_API_LAVF_AVCTX
    for (i = task->nb_output_streams - oc->nb_streams; i < task->nb_output_streams; i++) { //for all streams of this output file
        AVDictionaryEntry *e;
        ost = task->output_streams[i];

        if ((ost->stream_copy || ost->attachment_filename)
            && (e = av_dict_get(o->g->codec_opts, "flags", NULL, AV_DICT_IGNORE_SUFFIX))
            && (!e->key[5] || mk_check_stream_specifier(oc, ost->st, e->key+6)))
            if (av_opt_set(ost->st->codec, "flags", e->value, 0) < 0){
                task->running = 0;
                return -1;
                //exit_program(task,1);
            }

    }
#endif

    if (!oc->nb_streams && !(oc->oformat->flags & AVFMT_NOSTREAMS)) {
        av_dump_format(oc, task->nb_output_files - 1, oc->filename, 1);
        av_log(NULL, AV_LOG_ERROR, "Output file #%d does not contain any stream\n", task->nb_output_files - 1);
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }

    /* check if all codec options have been used */
    unused_opts = mk_strip_specifiers(o->g->codec_opts);
    for (i = of->ost_index; i < task->nb_output_streams; i++) {
        e = NULL;
        while ((e = av_dict_get(task->output_streams[i]->encoder_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
            av_dict_set(&unused_opts, e->key, NULL, 0);
    }

    e = NULL;
    while ((e = av_dict_get(unused_opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
        const AVClass *class = avcodec_get_class();
        const AVOption *option = av_opt_find(&class, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        const AVClass *fclass = avformat_get_class();
        const AVOption *foption = av_opt_find(&fclass, e->key, NULL, 0,
                                              AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        if (!option || foption)
            continue;


        if (!(option->flags & AV_OPT_FLAG_ENCODING_PARAM)) {
            av_log(NULL, AV_LOG_ERROR, "Codec AVOption %s (%s) specified for "
                   "output file #%d (%s) is not an encoding option.\n", e->key,
                   option->help ? option->help : "", task->nb_output_files - 1,
                   filename);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }

        // gop_timecode is injected by generic code but not always used
        if (!strcmp(e->key, "gop_timecode"))
            continue;

        av_log(NULL, AV_LOG_WARNING, "Codec AVOption %s (%s) specified for "
               "output file #%d (%s) has not been used for any stream. The most "
               "likely reason is either wrong type (e.g. a video option with "
               "no video streams) or that it is a private option of some encoder "
               "which was not actually used for any stream.\n", e->key,
               option->help ? option->help : "", task->nb_output_files - 1, filename);
    }
    av_dict_free(&unused_opts);

    /* set the decoding_needed flags and create simple filtergraphs */
    for (i = of->ost_index; i < task->nb_output_streams; i++) {
        mk_output_stream_t *ost = task->output_streams[i];

        if (ost->encoding_needed && ost->source_index >= 0) {
            mk_input_stream_t *ist = task->input_streams[ost->source_index];
            ist->decoding_needed |= DECODING_FOR_OST;

            if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
                ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                err = mk_init_simple_filtergraph(task,ist, ost);
                if (err < 0) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Error initializing a simple filtergraph between streams "
                           "%d:%d->%d:%d\n", ist->file_index, ost->source_index,
                           task->nb_output_files - 1, ost->st->index);
                    task->running = 0;
                    return -1;
                }
            }
        }
        /* set the filter output constraints */
        if (ost->filter) {
            mk_output_filter_t *f = ost->filter;
            int count;
            switch (ost->enc_ctx->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                f->frame_rate = ost->frame_rate;
                f->width      = ost->enc_ctx->width;
                f->height     = ost->enc_ctx->height;
                if (ost->enc_ctx->pix_fmt != AV_PIX_FMT_NONE) {
                    f->format = ost->enc_ctx->pix_fmt;
                } else if (ost->enc->pix_fmts) {
                    count = 0;
                    while (ost->enc->pix_fmts[count] != AV_PIX_FMT_NONE)
                        count++;
                    f->formats = av_mallocz_array(count + 1, sizeof(*f->formats));
                    if (!f->formats){
                        task->running = 0;
                        return -1;
                    }
                    memcpy(f->formats, ost->enc->pix_fmts, (count + 1) * sizeof(*f->formats));
                }
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (ost->enc_ctx->sample_fmt != AV_SAMPLE_FMT_NONE) {
                    f->format = ost->enc_ctx->sample_fmt;
                } else if (ost->enc->sample_fmts) {
                    count = 0;
                    while (ost->enc->sample_fmts[count] != AV_SAMPLE_FMT_NONE)
                        count++;
                    f->formats = av_mallocz_array(count + 1, sizeof(*f->formats));
                    if (!f->formats){
                        task->running = 0;
                        return -1;
                    }
                    memcpy(f->formats, ost->enc->sample_fmts, (count + 1) * sizeof(*f->formats));
                }
                if (ost->enc_ctx->sample_rate) {
                    f->sample_rate = ost->enc_ctx->sample_rate;
                } else if (ost->enc->supported_samplerates) {
                    count = 0;
                    while (ost->enc->supported_samplerates[count])
                        count++;
                    f->sample_rates = av_mallocz_array(count + 1, sizeof(*f->sample_rates));
                    if (!f->sample_rates){
                        task->running = 0;
                        return -1;
                    }
                    memcpy(f->sample_rates, ost->enc->supported_samplerates,
                           (count + 1) * sizeof(*f->sample_rates));
                }
                if (ost->enc_ctx->channels) {
                    f->channel_layout = av_get_default_channel_layout(ost->enc_ctx->channels);
                } else if (ost->enc->channel_layouts) {
                    count = 0;
                    while (ost->enc->channel_layouts[count])
                        count++;
                    f->channel_layouts = av_mallocz_array(count + 1, sizeof(*f->channel_layouts));
                    if (!f->channel_layouts){
                        task->running = 0;
                        return -1;
                    }
                    memcpy(f->channel_layouts, ost->enc->channel_layouts,
                           (count + 1) * sizeof(*f->channel_layouts));
                }
                break;
            }
        }
    }

    /* check filename in case of an image number is expected */
    if (oc->oformat->flags & AVFMT_NEEDNUMBER) {
        if (!av_filename_number_test(oc->filename)) {
            mk_log_error(oc->filename, AVERROR(EINVAL));
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
    }

    if (!(oc->oformat->flags & AVFMT_NOSTREAMS) && !input_stream_potentially_available) {
        av_log(NULL, AV_LOG_ERROR,
               "No input streams but output needs an input stream\n");
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        /* open the file */
        if ((err = avio_open2(&oc->pb, filename, AVIO_FLAG_WRITE,
                              &oc->interrupt_callback,
                              &of->opts)) < 0) {
            mk_log_error(filename, err);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
    }
    if (o->mux_preload) {
        av_dict_set_int(&of->opts, "preload", o->mux_preload*AV_TIME_BASE, 0);
    }
    oc->max_delay = (int)(o->mux_max_delay * AV_TIME_BASE);

    /* copy metadata */
    for (i = 0; i < o->nb_metadata_map; i++) {
        char *p;
        int in_file_index = strtol(o->metadata_map[i].u.str, &p, 0);

        if (in_file_index >= task->nb_input_files) {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index %d while processing metadata maps\n", in_file_index);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
        mk_copy_metadata(task,o->metadata_map[i].specifier, *p ? p + 1 : p, oc,
                      in_file_index >= 0 ?
                      task->input_files[in_file_index]->ctx : NULL, o);
    }

    /* copy chapters */
    if (o->chapters_input_file >= task->nb_input_files) {
        if (o->chapters_input_file == INT_MAX) {
            /* copy chapters from the first input file that has them*/
            o->chapters_input_file = -1;
            for (i = 0; i < task->nb_input_files; i++)
                if ((task->input_files[i]->ctx)&&(task->input_files[i]->ctx->nb_chapters)) {
                    o->chapters_input_file = i;
                    break;
                }
        } else {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index %d in chapter mapping.\n",
                   o->chapters_input_file);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
    }
    if (o->chapters_input_file >= 0)
        mk_copy_chapters(task->input_files[o->chapters_input_file], of,
                      !o->metadata_chapters_manual);

    /* copy global metadata by default */
    if (!o->metadata_global_manual && task->nb_input_files){
        av_dict_copy(&oc->metadata, task->input_files[0]->ctx->metadata,
                     AV_DICT_DONT_OVERWRITE);
        if(o->recording_time != INT64_MAX)
            av_dict_set(&oc->metadata, "duration", NULL, 0);
        av_dict_set(&oc->metadata, "creation_time", NULL, 0);
    }
    if (!o->metadata_streams_manual)
        for (i = of->ost_index; i < task->nb_output_streams; i++) {
            mk_input_stream_t *ist;
            if (task->output_streams[i]->source_index < 0)         /* this is true e.g. for attached files */
                continue;
            ist = task->input_streams[task->output_streams[i]->source_index];
            av_dict_copy(&task->output_streams[i]->st->metadata, ist->st->metadata, AV_DICT_DONT_OVERWRITE);
            if (!task->output_streams[i]->stream_copy) {
                av_dict_set(&task->output_streams[i]->st->metadata, "encoder", NULL, 0);
            }
        }

        /* process manually set programs */
        for (i = 0; i < o->nb_program; i++) {
            const char *p = o->program[i].u.str;
            int progid = i+1;
            AVProgram *program;

            while(*p) {
                const char *p2 = av_get_token(&p, ":");
                const char *to_dealloc = p2;
                char *key;
                if (!p2)
                    break;

                if(*p) p++;

                key = av_get_token(&p2, "=");
                if (!key || !*p2) {
                    av_freep(&to_dealloc);
                    av_freep(&key);
                    break;
                }
                p2++;

                if (!strcmp(key, "program_num"))
                    progid = strtol(p2, NULL, 0);
                av_freep(&to_dealloc);
                av_freep(&key);
            }

            program = av_new_program(oc, progid);

            p = o->program[i].u.str;
            while(*p) {
                const char *p2 = av_get_token(&p, ":");
                const char *to_dealloc = p2;
                char *key;
                if (!p2)
                    break;
                if(*p) p++;

                key = av_get_token(&p2, "=");
                if (!key) {
                    av_log(NULL, AV_LOG_FATAL,
                           "No '=' character in program string %s.\n",
                           p2);
                    task->running = 0;
                    return -1;
                }
                if (!*p2){
                    task->running = 0;
                    return -1;
                }
                p2++;

                if (!strcmp(key, "title")) {
                    av_dict_set(&program->metadata, "title", p2, 0);
                } else if (!strcmp(key, "program_num")) {
                } else if (!strcmp(key, "st")) {
                    int st_num = strtol(p2, NULL, 0);
                    av_program_add_stream_index(oc, progid, st_num);
                } else {
                    av_log(NULL, AV_LOG_FATAL, "Unknown program key %s.\n", key);
                    task->running = 0;
                    return -1;
                }
                av_freep(&to_dealloc);
                av_freep(&key);
            }
        }

    /* process manually set metadata */
    for (i = 0; i < o->nb_metadata; i++) {
        AVDictionary **m;
        char type, *val;
        const char *stream_spec;
        int index = 0, j, ret = 0;

        val = strchr(o->metadata[i].u.str, '=');
        if (!val) {
            av_log(NULL, AV_LOG_FATAL, "No '=' character in metadata string %s.\n",
                   o->metadata[i].u.str);
            task->running = 0;
            return -1;
            //exit_program(task,1);
        }
        *val++ = 0;

        mk_parse_meta_type(task,o->metadata[i].specifier, &type, &index, &stream_spec);
        if (type == 's') {
            for (j = 0; j < oc->nb_streams; j++) {
                ost = task->output_streams[task->nb_output_streams - oc->nb_streams + j];
                if ((ret = mk_check_stream_specifier(oc, oc->streams[j], stream_spec)) > 0) {
                    if (!strcmp(o->metadata[i].u.str, "rotate")) {
                        char *tail;
                        double theta = av_strtod(val, &tail);
                        if (!*tail) {
                            ost->rotate_overridden = 1;
                            ost->rotate_override_value = theta;
                        }
                    } else {
                        av_dict_set(&oc->streams[j]->metadata, o->metadata[i].u.str, *val ? val : NULL, 0);
                    }
                } else if (ret < 0){
                    task->running = 0;
                    return -1;
                    //exit_program(task,1);
               }
            }
        }
        else {
            switch (type) {
            case 'g':
                m = &oc->metadata;
                break;
            case 'c':
                if (index < 0 || index >= oc->nb_chapters) {
                    av_log(NULL, AV_LOG_FATAL, "Invalid chapter index %d in metadata specifier.\n", index);
                    task->running = 0;
                     return -1;
                     //exit_program(task,1);
                }
                m = &oc->chapters[index]->metadata;
                break;
            case 'p':
                if (index < 0 || index >= oc->nb_programs) {
                    av_log(NULL, AV_LOG_FATAL, "Invalid program index %d in metadata specifier.\n", index);
                    task->running = 0;
                    return -1;
                }
                m = &oc->programs[index]->metadata;
                break;
            default:{
                    av_log(NULL, AV_LOG_FATAL, "Invalid metadata specifier %s.\n", o->metadata[i].specifier);
                    task->running = 0;
                    return -1;
                    //exit_program(task,1);
                }
            }
            av_dict_set(m, o->metadata[i].u.str, *val ? val : NULL, 0);
        }
    }

    return 0;
}

static int mk_opt_target(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    enum { PAL, NTSC, FILM, UNKNOWN } norm = UNKNOWN;
    static const char *const frame_rates[] = { "25", "30000/1001", "24000/1001" };

    if (!strncmp(arg, "pal-", 4)) {
        norm = PAL;
        arg += 4;
    } else if (!strncmp(arg, "ntsc-", 5)) {
        norm = NTSC;
        arg += 5;
    } else if (!strncmp(arg, "film-", 5)) {
        norm = FILM;
        arg += 5;
    } else {
        /* Try to determine PAL/NTSC by peeking in the input files */
        if (task->nb_input_files) {
            int i, j, fr;
            for (j = 0; j < task->nb_input_files; j++) {
                for (i = 0; i < task->input_files[j]->nb_streams; i++) {
                    AVStream *st = task->input_files[j]->ctx->streams[i];
                    if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
                        continue;
                    fr = st->time_base.den * 1000 / st->time_base.num;
                    if (fr == 25000) {
                        norm = PAL;
                        break;
                    } else if ((fr == 29970) || (fr == 23976)) {
                        norm = NTSC;
                        break;
                    }
                }
                if (norm != UNKNOWN)
                    break;
            }
        }
        if (norm != UNKNOWN)
            av_log(NULL, AV_LOG_INFO, "Assuming %s for target.\n", norm == PAL ? "PAL" : "NTSC");
    }

    if (norm == UNKNOWN) {
        av_log(NULL, AV_LOG_FATAL, "Could not determine norm (PAL/NTSC/NTSC-Film) for target.\n");
        av_log(NULL, AV_LOG_FATAL, "Please prefix target with \"pal-\", \"ntsc-\" or \"film-\",\n");
        av_log(NULL, AV_LOG_FATAL, "or set a framerate with \"-r xxx\".\n");
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }

    if (!strcmp(arg, "vcd")) {
        mk_opt_video_codec(task,o, "c:v", "mpeg1video");
        mk_opt_audio_codec(task,o, "c:a", "mp2");
        mk_parse_option(task,o, "f", "vcd", options);

        mk_parse_option(task,o, "s", norm == PAL ? "352x288" : "352x240", options);
        mk_parse_option(task,o, "r", frame_rates[norm], options);
        mk_opt_default(task,NULL, "g", norm == PAL ? "15" : "18");

        mk_opt_default(task,NULL, "b:v", "1150000");
        mk_opt_default(task,NULL, "maxrate:v", "1150000");
        mk_opt_default(task,NULL, "minrate:v", "1150000");
        mk_opt_default(task,NULL, "bufsize:v", "327680"); // 40*1024*8;

        mk_opt_default(task,NULL, "b:a", "224000");
        mk_parse_option(task,o, "ar", "44100", options);
        mk_parse_option(task,o, "ac", "2", options);

        mk_opt_default(task,NULL, "packetsize", "2324");
        mk_opt_default(task,NULL, "muxrate", "1411200"); // 2352 * 75 * 8;

        /* We have to offset the PTS, so that it is consistent with the SCR.
           SCR starts at 36000, but the first two packs contain only padding
           and the first pack from the other stream, respectively, may also have
           been written before.
           So the real data starts at SCR 36000+3*1200. */
        o->mux_preload = (36000 + 3 * 1200) / 90000.0; // 0.44
    } else if (!strcmp(arg, "svcd")) {

        mk_opt_video_codec(task,o, "c:v", "mpeg2video");
        mk_opt_audio_codec(task,o, "c:a", "mp2");
        mk_parse_option(task,o, "f", "svcd", options);

        mk_parse_option(task,o, "s", norm == PAL ? "480x576" : "480x480", options);
        mk_parse_option(task,o, "r", frame_rates[norm], options);
        mk_parse_option(task,o, "pix_fmt", "yuv420p", options);
        mk_opt_default(task,NULL, "g", norm == PAL ? "15" : "18");

        mk_opt_default(task,NULL, "b:v", "2040000");
        mk_opt_default(task,NULL, "maxrate:v", "2516000");
        mk_opt_default(task,NULL, "minrate:v", "0"); // 1145000;
        mk_opt_default(task,NULL, "bufsize:v", "1835008"); // 224*1024*8;
        mk_opt_default(task,NULL, "scan_offset", "1");

        mk_opt_default(task,NULL, "b:a", "224000");
        mk_parse_option(task,o, "ar", "44100", options);

        mk_opt_default(task,NULL, "packetsize", "2324");

    } else if (!strcmp(arg, "dvd")) {

        mk_opt_video_codec(task,o, "c:v", "mpeg2video");
        mk_opt_audio_codec(task,o, "c:a", "ac3");
        mk_parse_option(task,o, "f", "dvd", options);

        mk_parse_option(task,o, "s", norm == PAL ? "720x576" : "720x480", options);
        mk_parse_option(task,o, "r", frame_rates[norm], options);
        mk_parse_option(task,o, "pix_fmt", "yuv420p", options);
        mk_opt_default(task,NULL, "g", norm == PAL ? "15" : "18");

        mk_opt_default(task,NULL, "b:v", "6000000");
        mk_opt_default(task,NULL, "maxrate:v", "9000000");
        mk_opt_default(task,NULL, "minrate:v", "0"); // 1500000;
        mk_opt_default(task,NULL, "bufsize:v", "1835008"); // 224*1024*8;

        mk_opt_default(task,NULL, "packetsize", "2048");  // from www.mpucoder.com: DVD sectors contain 2048 bytes of data, this is also the size of one pack.
        mk_opt_default(task,NULL, "muxrate", "10080000"); // from mplex project: data_rate = 1260000. mux_rate = data_rate * 8

        mk_opt_default(task,NULL, "b:a", "448000");
        mk_parse_option(task,o, "ar", "48000", options);

    } else if (!strncmp(arg, "dv", 2)) {

        mk_parse_option(task,o, "f", "dv", options);

        mk_parse_option(task,o, "s", norm == PAL ? "720x576" : "720x480", options);
        mk_parse_option(task,o, "pix_fmt", !strncmp(arg, "dv50", 4) ? "yuv422p" :
                          norm == PAL ? "yuv420p" : "yuv411p", options);
        mk_parse_option(task,o, "r", frame_rates[norm], options);

        mk_parse_option(task,o, "ar", "48000", options);
        mk_parse_option(task,o, "ac", "2", options);

    } else {
        av_log(NULL, AV_LOG_ERROR, "Unknown target: %s\n", arg);
        return AVERROR(EINVAL);
    }

    av_dict_copy(&o->g->codec_opts,  task->codec_opts, AV_DICT_DONT_OVERWRITE);
    av_dict_copy(&o->g->format_opts, task->format_opts, AV_DICT_DONT_OVERWRITE);

    return 0;
}

static int mk_opt_vstats_file(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    av_free (task->vstats_filename);
    task->vstats_filename = av_strdup (arg);
    return 0;
}

static int mk_opt_vstats(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    char filename[40];
    time_t today2 = time(NULL);
    struct tm *today = localtime(&today2);

    if (!today) { // maybe tomorrow
        av_log(NULL, AV_LOG_FATAL, "Unable to get current time: %s\n", strerror(errno));
        task->running = 0;
        return -1;
        //exit_program(task,1);
    }

    snprintf(filename, sizeof(filename), "vstats_%02d%02d%02d.log", today->tm_hour, today->tm_min,
             today->tm_sec);
    return mk_opt_vstats_file(task,NULL, opt, filename);
}

static int mk_opt_video_frames(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    return mk_parse_option(task,o, "frames:v", arg, options);
}

static int mk_opt_audio_frames(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    return mk_parse_option(task,o, "frames:a", arg, options);
}

static int mk_opt_data_frames(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    return mk_parse_option(task,o, "frames:d", arg, options);
}

static int mk_opt_default_new(mk_task_ctx_t* task,mk_option_ctx_t *o, const char *opt, const char *arg)
{
    int ret;
    AVDictionary *cbak = task->codec_opts;
    AVDictionary *fbak = task->format_opts;
    task->codec_opts = NULL;
    task->format_opts = NULL;

    ret = mk_opt_default(task,NULL, opt, arg);

    av_dict_copy(&o->g->codec_opts , task->codec_opts, 0);
    av_dict_copy(&o->g->format_opts, task->format_opts, 0);
    av_dict_free(&task->codec_opts);
    av_dict_free(&task->format_opts);
    task->codec_opts = cbak;
    task->format_opts = fbak;

    return ret;
}


static int mk_opt_old2new(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    char *s = av_asprintf("%s:%c", opt + 1, *opt);
    int ret = mk_parse_option(task,o, s, arg, options);
    av_free(s);
    return ret;
}

static int mk_opt_bitrate(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;

    if(!strcmp(opt, "ab")){
        av_dict_set(&o->g->codec_opts, "b:a", arg, 0);
        return 0;
    } else if(!strcmp(opt, "b")){
        av_log(NULL, AV_LOG_WARNING, "Please use -b:a or -b:v, -b is ambiguous\n");
        av_dict_set(&o->g->codec_opts, "b:v", arg, 0);
        return 0;
    }
    av_dict_set(&o->g->codec_opts, opt, arg, 0);
    return 0;
}

static int mk_opt_qscale(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    char *s;
    int ret;
    if(!strcmp(opt, "qscale")){
        av_log(NULL, AV_LOG_WARNING, "Please use -q:a or -q:v, -qscale is ambiguous\n");
        return mk_parse_option(task,o, "q:v", arg, options);
    }
    s = av_asprintf("q%s", opt + 6);
    ret = mk_parse_option(task,o, s, arg, options);
    av_free(s);
    return ret;
}

static int mk_opt_profile(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    if(!strcmp(opt, "profile")){
        av_log(NULL, AV_LOG_WARNING, "Please use -profile:a or -profile:v, -profile is ambiguous\n");
        av_dict_set(&o->g->codec_opts, "profile:v", arg, 0);
        return 0;
    }
    av_dict_set(&o->g->codec_opts, opt, arg, 0);
    return 0;
}

static int mk_opt_video_filters(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    return mk_parse_option(task,o, "filter:v", arg, options);
}

static int mk_opt_audio_filters(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    return mk_parse_option(task,o, "filter:a", arg, options);
}

static int mk_opt_vsync(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    if      (!av_strcasecmp(arg, "cfr"))         task->video_sync_method = VSYNC_CFR;
    else if (!av_strcasecmp(arg, "vfr"))         task->video_sync_method = VSYNC_VFR;
    else if (!av_strcasecmp(arg, "passthrough")) task->video_sync_method = VSYNC_PASSTHROUGH;
    else if (!av_strcasecmp(arg, "drop"))        task->video_sync_method = VSYNC_DROP;

    if (task->video_sync_method == VSYNC_AUTO)
        task->video_sync_method = mk_parse_number_or_die(task,"vsync", arg, OPT_INT, VSYNC_AUTO, VSYNC_VFR);
    return 0;
}

static int mk_opt_timecode(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    char *tcr = av_asprintf("timecode=%s", arg);
    int ret = mk_parse_option(task,o, "metadata:g", tcr, options);
    if (ret >= 0)
        ret = av_dict_set(&o->g->codec_opts, "gop_timecode", arg, 0);
    av_free(tcr);
    return 0;
}

static int mk_opt_channel_layout(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    char layout_str[32];
    char *stream_str;
    char *ac_str;
    int ret, channels, ac_str_size;
    uint64_t layout;

    layout = av_get_channel_layout(arg);
    if (!layout) {
        av_log(NULL, AV_LOG_ERROR, "Unknown channel layout: %s\n", arg);
        return AVERROR(EINVAL);
    }
    snprintf(layout_str, sizeof(layout_str), "%"PRIu64, layout);
    ret = mk_opt_default_new(task,o, opt, layout_str);
    if (ret < 0)
        return ret;

    /* set 'ac' option based on channel layout */
    channels = av_get_channel_layout_nb_channels(layout);
    snprintf(layout_str, sizeof(layout_str), "%d", channels);
    stream_str = strchr(opt, ':');
    ac_str_size = 3 + (stream_str ? strlen(stream_str) : 0);
    ac_str = av_mallocz(ac_str_size);
    if (!ac_str)
        return AVERROR(ENOMEM);
    av_strlcpy(ac_str, "ac", 3);
    if (stream_str)
        av_strlcat(ac_str, stream_str, ac_str_size);
    ret = mk_parse_option(task,o, ac_str, layout_str, options);
    av_free(ac_str);

    return ret;
}

static int mk_opt_audio_qscale(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    mk_option_ctx_t *o = optctx;
    return mk_parse_option(task,o, "q:a", arg, options);
}

static int mk_opt_filter_complex(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    GROW_ARRAY(task->filtergraphs, task->nb_filtergraphs);
    if (!(task->filtergraphs[task->nb_filtergraphs - 1] = av_mallocz(sizeof(*task->filtergraphs[0]))))
        return AVERROR(ENOMEM);
    task->filtergraphs[task->nb_filtergraphs - 1]->index      = task->nb_filtergraphs - 1;
    task->filtergraphs[task->nb_filtergraphs - 1]->graph_desc = av_strdup(arg);
    if (!task->filtergraphs[task->nb_filtergraphs - 1]->graph_desc)
        return AVERROR(ENOMEM);

    input_stream_potentially_available = 1;

    return 0;
}

static int mk_opt_filter_complex_script(mk_task_ctx_t* task,void *optctx, const char *opt, const char *arg)
{
    uint8_t *graph_desc = mk_read_file(arg);
    if (!graph_desc)
        return AVERROR(EINVAL);

    GROW_ARRAY(task->filtergraphs, task->nb_filtergraphs);
    if (!(task->filtergraphs[task->nb_filtergraphs - 1] = av_mallocz(sizeof(*task->filtergraphs[0]))))
        return AVERROR(ENOMEM);
    task->filtergraphs[task->nb_filtergraphs - 1]->index      = task->nb_filtergraphs - 1;
    task->filtergraphs[task->nb_filtergraphs - 1]->graph_desc = graph_desc;

    input_stream_potentially_available = 1;

    return 0;
}


enum OptGroup {
    GROUP_OUTFILE,
    GROUP_INFILE,
};

static const mk_option_group_def_t groups[] = {
    [GROUP_OUTFILE] = { "output url",  "dst", OPT_OUTPUT },
    [GROUP_INFILE]  = { "input url",   "src",  OPT_INPUT },
};

static int mk_open_files(mk_task_ctx_t* task,mk_option_group_list_t *l, const char *inout,
                      int (*open_file)(mk_task_ctx_t* task,mk_option_ctx_t*, const char*))
{
    int i, ret;

    for (i = 0; i < l->nb_groups; i++) {
        mk_option_group_t *g = &l->groups[i];
        mk_option_ctx_t o;

        mk_init_options(&o);
        o.g = g;

        ret = mk_parse_optgroup(task,&o, g);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error parsing options for %s file "
                   "%s.\n", inout, g->arg);
            return ret;
        }

        av_log(NULL, AV_LOG_DEBUG, "Opening an %s file: %s.\n", inout, g->arg);
        ret = open_file(task,&o, g->arg);
        mk_uninit_options(&o);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error opening %s file %s.\n",
                   inout, g->arg);
            return ret;
        }
        av_log(NULL, AV_LOG_DEBUG, "Successfully opened the file.\n");
    }

    return 0;
}

int mk_ffmpeg_parse_options(mk_task_ctx_t* task,int argc, char **argv)
{
    mk_option_parse_ctx_t octx;
    uint8_t error[128];
    int ret;

    memset(&octx, 0, sizeof(octx));

    /* split the commandline into an internal representation */
    ret = mk_split_commandline(task,&octx, argc, argv, options, groups,
                            FF_ARRAY_ELEMS(groups));
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error splitting the argument list: ");
        goto fail;
    }

    /* apply global options */
    ret = mk_parse_optgroup(task,NULL, &octx.global_opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error parsing global options: ");
        goto fail;
    }

    /* open input files */
    ret = mk_open_files(task,&octx.groups[GROUP_INFILE], "input", mk_open_input_file);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error opening input files: ");
        goto fail;
    }

    /* create the complex filtergraphs */
    ret = mk_init_complex_filters(task);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error initializing complex filters.\n");
        goto fail;
    }

    /* open output files */
    ret = mk_open_files(task,&octx.groups[GROUP_OUTFILE], "output", mk_open_output_file);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error opening output files: ");
        goto fail;
    }

    mk_check_filter_outputs(task);

fail:
    mk_uninit_parse_context(task,&octx);
    if (ret < 0) {
        av_strerror(ret, error, sizeof(error));
        av_log(NULL, AV_LOG_FATAL, "%s\n", error);
    }
    return ret;
}

void mk_init_ffmpeg_option(mk_task_ctx_t* task)
{
    task->input_streams               = NULL;
    task->nb_input_streams            = 0;
    task->input_files                 = NULL;
    task->nb_input_files              = 0;
    task->output_streams              = NULL;
    task->nb_output_streams           = 0;
    task->output_files                = NULL;
    task->nb_output_files             = 0;
    task->filtergraphs                = NULL;
    task->nb_filtergraphs             = 0;
    task->hwaccel_lax_profile_check   = 0;
    task->hw_device_ctx               = NULL;
    task->vstats_filename             = NULL;
    task->sdp_filename                = NULL;
    task->audio_drift_threshold       = 0.1;
    task->dts_delta_threshold         = 10;
    task->dts_error_threshold         = 3600*30;
    task->audio_volume                = 256;
    task->audio_sync_method           = 0;
    task->video_sync_method           = VSYNC_AUTO;
    task->frame_drop_threshold        = 0;
    task->do_deinterlace              = 0;
    task->do_hex_dump                 = 0;
    task->do_pkt_dump                 = 0;
    task->copy_ts                     = 0;
    task->start_at_zero               = 0;
    task->copy_tb                     = -1;
    task->debug_ts                    = 0;
    task->exit_on_error               = 0;
    task->abort_on_flags              = 0;
    task->print_stats                 = -1;
    task->frame_bits_per_raw_sample   = 0;
    task->sws_dict                    = NULL;
    task->swr_opts                    = NULL;
    task->format_opts                 = NULL;
    task->codec_opts                  = NULL;
    task->resample_opts               = NULL;
    task->thread                      = NULL;
    task->mutex                       = NULL;
    task->status                      = MK_TASK_STATUS_INIT;
    task->running                     = 0;
    task->transcode_init_done         = 0;
    task->int_cb.callback             = mk_decode_interrupt_cb;
    task->int_cb.opaque               = task ;
    task->vstats_file                 = NULL;
    task->nb_frames_dup               = 0;
    task->nb_frames_drop              = 0;
    task->decode_error_stat[0]        = 0;
    task->decode_error_stat[1]        = 0;
    task->subtitle_out                = NULL;
    task->main_return_code            = 0;
    task->nparamcount                 = 0;
    task->paramlist                   = NULL;
    task->filter_nbthreads            = 0;
    task->filter_complex_nbthreads    = 0;
    task->vstats_version              = 2;
    task->dup_warning                 = 1000;
}


#define OFFSET(x) offsetof(mk_option_ctx_t, x)
const mk_option_def_t options[] = {
    /* main options */
#include "mk_media_cmdutils_opts.h"
    { "f",              HAS_ARG | OPT_STRING | OPT_OFFSET |
                        OPT_INPUT | OPT_OUTPUT,                      { .off       = OFFSET(format) },
        "force format", "fmt" },
    { "y",              OPT_BOOL,                                    {              &file_overwrite },
        "overwrite output files" },
    { "n",              OPT_BOOL,                                    {              &no_file_overwrite },
        "never overwrite output files" },
    { "ignore_unknown", OPT_BOOL,                                    {              &ignore_unknown_streams },
        "Ignore unknown stream types" },
    { "copy_unknown",   OPT_BOOL | OPT_EXPERT,                       {              &copy_unknown_streams },
        "Copy unknown stream types" },
    { "c",              HAS_ARG | OPT_STRING | OPT_SPEC |
                        OPT_INPUT | OPT_OUTPUT,                      { .off       = OFFSET(codec_names) },
        "codec name", "codec" },
    { "codec",          HAS_ARG | OPT_STRING | OPT_SPEC |
                        OPT_INPUT | OPT_OUTPUT,                      { .off       = OFFSET(codec_names) },
        "codec name", "codec" },
    { "pre",            HAS_ARG | OPT_STRING | OPT_SPEC |
                        OPT_OUTPUT,                                  { .off       = OFFSET(presets) },
        "preset name", "preset" },
    { "map",            HAS_ARG | OPT_EXPERT | OPT_PERFILE |
                        OPT_OUTPUT,                                  { .func_arg = mk_opt_map },
        "set input stream mapping",
        "[-]input_file_id[:stream_specifier][,sync_file_id[:stream_specifier]]" },
    { "map_channel",    HAS_ARG | OPT_EXPERT | OPT_PERFILE | OPT_OUTPUT, { .func_arg = mk_opt_map_channel },
        "map an audio channel from one stream to another", "file.stream.channel[:syncfile.syncstream]" },
    { "map_metadata",   HAS_ARG | OPT_STRING | OPT_SPEC |
                        OPT_OUTPUT,                                  { .off       = OFFSET(metadata_map) },
        "set metadata information of outfile from infile",
        "outfile[,metadata]:infile[,metadata]" },
    { "map_chapters",   HAS_ARG | OPT_INT | OPT_EXPERT | OPT_OFFSET |
                        OPT_OUTPUT,                                  { .off = OFFSET(chapters_input_file) },
        "set chapters mapping", "input_file_index" },
    { "t",              HAS_ARG | OPT_TIME | OPT_OFFSET |
                        OPT_INPUT | OPT_OUTPUT,                      { .off = OFFSET(recording_time) },
        "record or transcode \"duration\" seconds of audio/video",
        "duration" },
    { "to",             HAS_ARG | OPT_TIME | OPT_OFFSET | OPT_OUTPUT,  { .off = OFFSET(stop_time) },
        "record or transcode stop time", "time_stop" },
    { "fs",             HAS_ARG | OPT_INT64 | OPT_OFFSET | OPT_OUTPUT, { .off = OFFSET(limit_filesize) },
        "set the limit file size in bytes", "limit_size" },
    { "ss",             HAS_ARG | OPT_TIME | OPT_OFFSET |
                        OPT_INPUT | OPT_OUTPUT,                      { .off = OFFSET(start_time) },
        "set the start time offset", "time_off" },
    { "sseof",          HAS_ARG | OPT_TIME | OPT_OFFSET |
                        OPT_INPUT | OPT_OUTPUT,                      { .off = OFFSET(start_time_eof) },
        "set the start time offset relative to EOF", "time_off" },
    { "seek_timestamp", HAS_ARG | OPT_INT | OPT_OFFSET |
                        OPT_INPUT,                                   { .off = OFFSET(seek_timestamp) },
        "enable/disable seeking by timestamp with -ss" },
    { "accurate_seek",  OPT_BOOL | OPT_OFFSET | OPT_EXPERT |
                        OPT_INPUT,                                   { .off = OFFSET(accurate_seek) },
        "enable/disable accurate seeking with -ss" },
    { "itsoffset",      HAS_ARG | OPT_TIME | OPT_OFFSET |
                        OPT_EXPERT | OPT_INPUT,                      { .off = OFFSET(input_ts_offset) },
        "set the input ts offset", "time_off" },
    { "itsscale",       HAS_ARG | OPT_DOUBLE | OPT_SPEC |
                        OPT_EXPERT | OPT_INPUT,                      { .off = OFFSET(ts_scale) },
        "set the input ts scale", "scale" },
    { "timestamp",      HAS_ARG | OPT_PERFILE | OPT_OUTPUT,          { .func_arg = mk_opt_recording_timestamp },
        "set the recording timestamp ('now' to set the current time)", "time" },
    { "metadata",       HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(metadata) },
        "add metadata", "string=string" },
    { "program",        HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(program) },
        "add program with specified streams", "title=string:st=number..." },
    { "dframes",        HAS_ARG | OPT_PERFILE | OPT_EXPERT |
                        OPT_OUTPUT,                                  { .func_arg = mk_opt_data_frames },
        "set the number of data frames to output", "number" },
    { "re",             OPT_BOOL | OPT_EXPERT | OPT_OFFSET |
                        OPT_INPUT,                                   { .off = OFFSET(rate_emu) },
        "read input at native frame rate", "" },
    { "target",         HAS_ARG | OPT_PERFILE | OPT_OUTPUT,          { .func_arg = mk_opt_target },
        "specify target file type (\"vcd\", \"svcd\", \"dvd\", \"dv\" or \"dv50\" "
        "with optional prefixes \"pal-\", \"ntsc-\" or \"film-\")", "type" },
    { "vsync",          HAS_ARG | OPT_EXPERT,                        { .func_arg = mk_opt_vsync },
        "video sync method", "" },
    { "shortest",       OPT_BOOL | OPT_EXPERT | OPT_OFFSET |
                        OPT_OUTPUT,                                  { .off = OFFSET(shortest) },
        "finish encoding within shortest input" },
    { "apad",           OPT_STRING | HAS_ARG | OPT_SPEC |
                        OPT_OUTPUT,                                  { .off = OFFSET(apad) },
        "audio pad", "" },
    { "abort_on",       HAS_ARG | OPT_EXPERT,                        { .func_arg = mk_opt_abort_on },
        "abort on the specified condition flags", "flags" },
    { "copyinkf",       OPT_BOOL | OPT_EXPERT | OPT_SPEC |
                        OPT_OUTPUT,                                  { .off = OFFSET(copy_initial_nonkeyframes) },
        "copy initial non-keyframes" },
    { "copypriorss",    OPT_INT | HAS_ARG | OPT_EXPERT | OPT_SPEC | OPT_OUTPUT,   { .off = OFFSET(copy_prior_start) },
        "copy or discard frames before start time" },
    { "frames",         OPT_INT64 | HAS_ARG | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(max_frames) },
        "set the number of frames to output", "number" },
    { "tag",            OPT_STRING | HAS_ARG | OPT_SPEC |
                        OPT_EXPERT | OPT_OUTPUT | OPT_INPUT,         { .off = OFFSET(codec_tags) },
        "force codec tag/fourcc", "fourcc/tag" },
    { "q",              HAS_ARG | OPT_EXPERT | OPT_DOUBLE |
                        OPT_SPEC | OPT_OUTPUT,                       { .off = OFFSET(qscale) },
        "use fixed quality scale (VBR)", "q" },
    { "qscale",         HAS_ARG | OPT_EXPERT | OPT_PERFILE |
                        OPT_OUTPUT,                                  { .func_arg = mk_opt_qscale },
        "use fixed quality scale (VBR)", "q" },
    { "profile",        HAS_ARG | OPT_EXPERT | OPT_PERFILE | OPT_OUTPUT, { .func_arg = mk_opt_profile },
        "set profile", "profile" },
    { "filter",         HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(filters) },
        "set stream filtergraph", "filter_graph" },
    { "filter_script",  HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(filter_scripts) },
        "read stream filtergraph description from a file", "filename" },
    { "reinit_filter",  HAS_ARG | OPT_INT | OPT_SPEC | OPT_INPUT,    { .off = OFFSET(reinit_filters) },
        "reinit filtergraph on input parameter changes", "" },
    { "filter_complex", HAS_ARG | OPT_EXPERT,                        { .func_arg = mk_opt_filter_complex },
        "create a complex filtergraph", "graph_description" },
    { "lavfi",          HAS_ARG | OPT_EXPERT,                        { .func_arg = mk_opt_filter_complex },
        "create a complex filtergraph", "graph_description" },
    { "filter_complex_script", HAS_ARG | OPT_EXPERT,                 { .func_arg = mk_opt_filter_complex_script },
        "read complex filtergraph description from a file", "filename" },
    { "attach",         HAS_ARG | OPT_PERFILE | OPT_EXPERT |
                        OPT_OUTPUT,                                  { .func_arg = mk_opt_attach },
        "add an attachment to the output file", "filename" },
    { "dump_attachment", HAS_ARG | OPT_STRING | OPT_SPEC |
                         OPT_EXPERT | OPT_INPUT,                     { .off = OFFSET(dump_attachment) },
        "extract an attachment into a file", "filename" },
    { "stream_loop", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_INPUT |
                        OPT_OFFSET,                                  { .off = OFFSET(loop) }, "set number of times input stream shall be looped", "loop count" },
    { "discard",        OPT_STRING | HAS_ARG | OPT_SPEC |
                        OPT_INPUT,                                   { .off = OFFSET(discard) },
        "discard", "" },
    { "disposition",    OPT_STRING | HAS_ARG | OPT_SPEC |
                        OPT_OUTPUT,                                  { .off = OFFSET(disposition) },
        "disposition", "" },
    { "thread_queue_size", HAS_ARG | OPT_INT | OPT_OFFSET | OPT_EXPERT | OPT_INPUT,
                                                                     { .off = OFFSET(thread_queue_size) },
        "set the maximum number of queued packets from the demuxer" },

    /* video options */
    { "vframes",      OPT_VIDEO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = mk_opt_video_frames },
        "set the number of video frames to output", "number" },
    { "r",            OPT_VIDEO | HAS_ARG  | OPT_STRING | OPT_SPEC |
                      OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(frame_rates) },
        "set frame rate (Hz value, fraction or abbreviation)", "rate" },
    { "s",            OPT_VIDEO | HAS_ARG | OPT_SUBTITLE | OPT_STRING | OPT_SPEC |
                      OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(frame_sizes) },
        "set frame size (WxH or abbreviation)", "size" },
    { "aspect",       OPT_VIDEO | HAS_ARG  | OPT_STRING | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(frame_aspect_ratios) },
        "set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)", "aspect" },
    { "pix_fmt",      OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                      OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(frame_pix_fmts) },
        "set pixel format", "format" },
    { "intra",        OPT_VIDEO | OPT_BOOL | OPT_EXPERT,                         { &intra_only },
        "deprecated use -g 1" },
    { "vn",           OPT_VIDEO | OPT_BOOL  | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,{ .off = OFFSET(video_disable) },
        "disable video" },
    { "rc_override",  OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(rc_overrides) },
        "rate control override for specific intervals", "override" },
    { "vcodec",       OPT_VIDEO | HAS_ARG  | OPT_PERFILE | OPT_INPUT |
                      OPT_OUTPUT,                                                { .func_arg = mk_opt_video_codec },
        "force video codec ('copy' to copy stream)", "codec" },
    { "sameq",        OPT_VIDEO | OPT_EXPERT ,                                   { .func_arg = mk_opt_sameq },
        "Removed" },
    { "same_quant",   OPT_VIDEO | OPT_EXPERT ,                                   { .func_arg = mk_opt_sameq },
        "Removed" },
    { "timecode",     OPT_VIDEO | HAS_ARG | OPT_PERFILE | OPT_OUTPUT,            { .func_arg = mk_opt_timecode },
        "set initial TimeCode value.", "hh:mm:ss[:;.]ff" },
    { "pass",         OPT_VIDEO | HAS_ARG | OPT_SPEC | OPT_INT | OPT_OUTPUT,     { .off = OFFSET(pass) },
        "select the pass number (1 to 3)", "n" },
    { "passlogfile",  OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_EXPERT | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(passlogfiles) },
        "select two pass log file name prefix", "prefix" },
    { "psnr",         OPT_VIDEO | OPT_BOOL | OPT_EXPERT,                         { &do_psnr },
        "calculate PSNR of compressed frames" },
    { "vstats",       OPT_VIDEO | OPT_EXPERT ,                                   { .func_arg = mk_opt_vstats },
        "dump video coding statistics to file" },
    { "vstats_file",  OPT_VIDEO | HAS_ARG | OPT_EXPERT ,                         { .func_arg = mk_opt_vstats_file },
        "dump video coding statistics to file", "file" },
    { "vstats_version",  OPT_VIDEO | OPT_INT | HAS_ARG | OPT_EXPERT ,            { &vstats_version },
        "Version of the vstats format to use."},
    { "vf",           OPT_VIDEO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = mk_opt_video_filters },
        "set video filters", "filter_graph" },
    { "intra_matrix", OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(intra_matrices) },
        "specify intra matrix coeffs", "matrix" },
    { "inter_matrix", OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(inter_matrices) },
        "specify inter matrix coeffs", "matrix" },
    { "chroma_intra_matrix", OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(chroma_intra_matrices) },
        "specify intra matrix coeffs", "matrix" },
    { "top",          OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_INT| OPT_SPEC |
                      OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(top_field_first) },
        "top=1/bottom=0/auto=-1 field first", "" },
    { "vtag",         OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_PERFILE |
                      OPT_INPUT | OPT_OUTPUT,                                    { .func_arg = mk_opt_old2new },
        "force video tag/fourcc", "fourcc/tag" },
    { "force_fps",    OPT_VIDEO | OPT_BOOL | OPT_EXPERT  | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(force_fps) },
        "force the selected framerate, disable the best supported framerate selection" },
    { "streamid",     OPT_VIDEO | HAS_ARG | OPT_EXPERT | OPT_PERFILE |
                      OPT_OUTPUT,                                                { .func_arg = mk_opt_streamid },
        "set the value of an outfile streamid", "streamIndex:value" },
    { "force_key_frames", OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                          OPT_SPEC | OPT_OUTPUT,                                 { .off = OFFSET(forced_key_frames) },
        "force key frames at specified timestamps", "timestamps" },
    { "ab",           OPT_VIDEO | HAS_ARG | OPT_PERFILE | OPT_OUTPUT,            { .func_arg = mk_opt_bitrate },
        "audio bitrate (please use -b:a)", "bitrate" },
    { "b",            OPT_VIDEO | HAS_ARG | OPT_PERFILE | OPT_OUTPUT,            { .func_arg = mk_opt_bitrate },
        "video bitrate (please use -b:v)", "bitrate" },
    { "hwaccel",          OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                          OPT_SPEC | OPT_INPUT,                                  { .off = OFFSET(hwaccels) },
        "use HW accelerated decoding", "hwaccel name" },
    { "hwaccel_device",   OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                          OPT_SPEC | OPT_INPUT,                                  { .off = OFFSET(hwaccel_devices) },
        "select a device for HW acceleration", "devicename" },
    { "hwaccel_output_format", OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                          OPT_SPEC | OPT_INPUT,                                  { .off = OFFSET(hwaccel_output_formats) },
        "select output format used with HW accelerated decoding", "format" },
#if CONFIG_VDA || CONFIG_VIDEOTOOLBOX
    { "videotoolbox_pixfmt", HAS_ARG | OPT_STRING | OPT_EXPERT, { &videotoolbox_pixfmt}, "" },
#endif
    { "autorotate",       HAS_ARG | OPT_BOOL | OPT_SPEC |
                          OPT_EXPERT | OPT_INPUT,                                { .off = OFFSET(autorotate) },
        "automatically insert correct rotate filters" },

    /* audio options */
    { "aframes",        OPT_AUDIO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = mk_opt_audio_frames },
        "set the number of audio frames to output", "number" },
    { "aq",             OPT_AUDIO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = mk_opt_audio_qscale },
        "set audio quality (codec-specific)", "quality", },
    { "ar",             OPT_AUDIO | HAS_ARG  | OPT_INT | OPT_SPEC |
                        OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(audio_sample_rate) },
        "set audio sampling rate (in Hz)", "rate" },
    { "ac",             OPT_AUDIO | HAS_ARG  | OPT_INT | OPT_SPEC |
                        OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(audio_channels) },
        "set number of audio channels", "channels" },
    { "an",             OPT_AUDIO | OPT_BOOL | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,{ .off = OFFSET(audio_disable) },
        "disable audio" },
    { "acodec",         OPT_AUDIO | HAS_ARG  | OPT_PERFILE |
                        OPT_INPUT | OPT_OUTPUT,                                    { .func_arg = mk_opt_audio_codec },
        "force audio codec ('copy' to copy stream)", "codec" },
    { "atag",           OPT_AUDIO | HAS_ARG  | OPT_EXPERT | OPT_PERFILE |
                        OPT_OUTPUT,                                                { .func_arg = mk_opt_old2new },
        "force audio tag/fourcc", "fourcc/tag" },
    { "sample_fmt",     OPT_AUDIO | HAS_ARG  | OPT_EXPERT | OPT_SPEC |
                        OPT_STRING | OPT_INPUT | OPT_OUTPUT,                       { .off = OFFSET(sample_fmts) },
        "set sample format", "format" },
    { "channel_layout", OPT_AUDIO | HAS_ARG  | OPT_EXPERT | OPT_PERFILE |
                        OPT_INPUT | OPT_OUTPUT,                                    { .func_arg = mk_opt_channel_layout },
        "set channel layout", "layout" },
    { "af",             OPT_AUDIO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = mk_opt_audio_filters },
        "set audio filters", "filter_graph" },
    { "guess_layout_max", OPT_AUDIO | HAS_ARG | OPT_INT | OPT_SPEC | OPT_EXPERT | OPT_INPUT, { .off = OFFSET(guess_layout_max) },
      "set the maximum number of channels to try to guess the channel layout" },

    /* subtitle options */
    { "sn",     OPT_SUBTITLE | OPT_BOOL | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT, { .off = OFFSET(subtitle_disable) },
        "disable subtitle" },
    { "scodec", OPT_SUBTITLE | HAS_ARG  | OPT_PERFILE | OPT_INPUT | OPT_OUTPUT, { .func_arg = mk_opt_subtitle_codec },
        "force subtitle codec ('copy' to copy stream)", "codec" },
    { "stag",   OPT_SUBTITLE | HAS_ARG  | OPT_EXPERT  | OPT_PERFILE | OPT_OUTPUT, { .func_arg = mk_opt_old2new }
        , "force subtitle tag/fourcc", "fourcc/tag" },
    { "fix_sub_duration", OPT_BOOL | OPT_EXPERT | OPT_SUBTITLE | OPT_SPEC | OPT_INPUT, { .off = OFFSET(fix_sub_duration) },
        "fix subtitles duration" },
    { "canvas_size", OPT_SUBTITLE | HAS_ARG | OPT_STRING | OPT_SPEC | OPT_INPUT, { .off = OFFSET(canvas_sizes) },
        "set canvas size (WxH or abbreviation)", "size" },

    /* grab options */
    { "vc", HAS_ARG | OPT_EXPERT | OPT_VIDEO, { .func_arg = mk_opt_video_channel },
        "deprecated, use -channel", "channel" },
    { "tvstd", HAS_ARG | OPT_EXPERT | OPT_VIDEO, { .func_arg = mk_opt_video_standard },
        "deprecated, use -standard", "standard" },
    { "isync", OPT_BOOL | OPT_EXPERT, { &input_sync }, "this option is deprecated and does nothing", "" },

    /* muxer options */
    { "muxdelay",   OPT_FLOAT | HAS_ARG | OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT, { .off = OFFSET(mux_max_delay) },
        "set the maximum demux-decode delay", "seconds" },
    { "muxpreload", OPT_FLOAT | HAS_ARG | OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT, { .off = OFFSET(mux_preload) },
        "set the initial demux-decode delay", "seconds" },
    { "sdp_file", HAS_ARG | OPT_EXPERT | OPT_OUTPUT, {  .func_arg = mk_opt_sdp_file },
        "specify a file in which to print sdp information", "file" },

    { "time_base", HAS_ARG | OPT_STRING | OPT_EXPERT | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(time_bases) },
        "set the desired time base hint for output stream (1:24, 1:48000 or 0.04166, 2.0833e-5)", "ratio" },

    { "bsf", HAS_ARG | OPT_STRING | OPT_SPEC | OPT_EXPERT | OPT_OUTPUT, { .off = OFFSET(bitstream_filters) },
        "A comma-separated list of bitstream filters", "bitstream_filters" },
    { "absf", HAS_ARG | OPT_AUDIO | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT, { .func_arg = mk_opt_old2new },
        "deprecated", "audio bitstream_filters" },
    { "vbsf", OPT_VIDEO | HAS_ARG | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT, { .func_arg = mk_opt_old2new },
        "deprecated", "video bitstream_filters" },


    { "max_muxing_queue_size", HAS_ARG | OPT_INT | OPT_SPEC | OPT_EXPERT | OPT_OUTPUT, { .off = OFFSET(max_muxing_queue_size) },
        "maximum number of packets that can be buffered while waiting for all streams to initialize", "packets" },

    /* data codec support */
    { "dcodec", HAS_ARG | OPT_DATA | OPT_PERFILE | OPT_EXPERT | OPT_INPUT | OPT_OUTPUT, { .func_arg = mk_opt_data_codec },
        "force data codec ('copy' to copy stream)", "codec" },
    { "dn", OPT_BOOL | OPT_VIDEO | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT, { .off = OFFSET(data_disable) },
        "disable data" },
#if CONFIG_VAAPI
    { "vaapi_device", HAS_ARG | OPT_EXPERT, { .func_arg = mk_opt_vaapi_device },
                    "set VAAPI hardware device (DRM path or X11 display name)", "device" },
#endif

#if CONFIG_QSV
    { "qsv_device", HAS_ARG | OPT_STRING | OPT_EXPERT, { &qsv_device },
            "set QSV hardware device (DirectX adapter index, DRM path or X11 display name)", "device"},
#endif

    { NULL, },
};
