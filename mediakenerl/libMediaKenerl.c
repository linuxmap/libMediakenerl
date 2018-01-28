#include "libMediaKenerl.h"
#include "mk_common.h"
#include "mk_task_monitor.h"

/* current ffmpeg version 3.3.2 */

static  int32_t g_mk_lib_init = 0;
static log_callback callback_fun = NULL;
static  int32_t g_mk_task_monitor = 0;
/* log call back funciton */
static void mk_log_callback_function(void *ptr, int level, const char *fmt, va_list vl)
{
    if(NULL == callback_fun)
    {
        return;
    }
    va_list vl2;
    char line[1024];
    static int print_prefix = 1;

    va_copy(vl2, vl);
    av_log_default_callback(ptr, level, fmt, vl);
    av_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
    va_end(vl2);

    callback_fun(line);

}


/* media kenerl task run thread function */
static  void* mk_task_thread(void * param)
{
    mk_task_ctx_t* task = (mk_task_ctx_t*)param;
    task->running = 1;
    mk_main_task(task);
    return NULL;
}

/* init the media kenerl libary */
int32_t   mk_lib_init(log_callback log_cb,int32_t task_monitor)
{
    if(g_mk_lib_init)
    {
        return MK_ERROR_CODE_OK;
    }

    callback_fun      = log_cb;
    g_mk_task_monitor = task_monitor;

    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    /*
    { "quiet"  , AV_LOG_QUIET   },
    { "panic"  , AV_LOG_PANIC   },
    { "fatal"  , AV_LOG_FATAL   },
    { "error"  , AV_LOG_ERROR   },
    { "warning", AV_LOG_WARNING },
    { "info"   , AV_LOG_INFO    },
    { "verbose", AV_LOG_VERBOSE },
    { "debug"  , AV_LOG_DEBUG   },
    { "trace"  , AV_LOG_TRACE   },
    */

    /* set the log call back */
    if(NULL != callback_fun) {
        av_log_set_level(AV_LOG_DEBUG);
        av_log_set_callback(mk_log_callback_function);
    }
    else {
        av_log_set_level(AV_LOG_ERROR);
    }

    avcodec_register_all();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avfilter_register_all();
    av_register_all();
    avformat_network_init();

    if(g_mk_task_monitor) {
        if(MK_ERROR_CODE_OK != mk_init_task_monitor()) {
            return MK_ERROR_CODE_FAIL;
        }
    }

    g_mk_lib_init = 1;

    return MK_ERROR_CODE_OK;
}

/* release the media kenerl bibary */
void      mk_lib_release()
{
    callback_fun = NULL;
    avformat_network_deinit();
    if(g_mk_task_monitor) {
        mk_release_task_monitor();
    }
    g_mk_lib_init = 0;
}

/* create a media kenerl handle for media task */
MK_HANDLE mk_create_handle()
{
    mk_task_ctx_t* task = NULL;
    task = (mk_task_ctx_t*)av_malloc(sizeof(mk_task_ctx_t));
    if(NULL != task)
    {
        mk_init_ffmpeg_option(task);
    }
    task->nparamcount      = 0;
    task->paramlist        = av_malloc(sizeof(char*)*PARAM_COUNT_MAX);
    task->mutex            = mk_create_mutex();
    task->exited           = 0;
    memset(&task->stat,0,sizeof(mk_task_stat_info_t));
    return (MK_HANDLE)task;
}
/* destory the media kenerl handle */
void      mk_destory_handle(MK_HANDLE handle)
{
    mk_task_ctx_t* task = (mk_task_ctx_t*)handle;
    if(NULL != task)
    {
        if(g_mk_task_monitor) {
            mk_unreg_task_monitor(task);
        }
        if(NULL != task->paramlist)
        {
            int i = 0;
            for(i = 0;i < task->nparamcount;i++)
            {
                av_free(task->paramlist[i]);
                task->paramlist[i] = NULL;
            }
            av_free(task->paramlist);
            task->paramlist = NULL;
        }
        if(NULL != task->mutex) {
            mk_destroy_mutex(task->mutex);
            task->mutex = NULL;
        }
        av_free(task);
    }
    return;
}

/* run the media kenerl task */
int32_t   mk_run_task(MK_HANDLE handle,uint32_t argc,char** argv)
{
    mk_task_ctx_t* task = (mk_task_ctx_t*)handle;
    int nparamcount = 0;
    char* param = NULL;

    if(argc >= PARAM_COUNT_MAX)
    {
        return MK_ERROR_CODE_PARAM;
    }

    if( MK_ERROR_CODE_OK != mk_mutex_lock(task->mutex)) {
        return MK_ERROR_CODE_FAIL;
    }

    param = av_malloc(strlen(program_name)+1);
    strncpy(param,program_name,strlen(program_name));
    task->paramlist[nparamcount] = param;
    nparamcount++;

    unsigned long i = 0;
    for(i = 0;i < argc; i++)
    {
        unsigned long ulSize = strlen(argv[i]);
        param = av_malloc(ulSize+2);
        memset(param,0,ulSize+2);
        strncpy(&param[0],argv[i],ulSize);
        task->paramlist[nparamcount] = param;
        nparamcount++;
    }

    task->nparamcount = nparamcount;



    int32_t ret = MK_ERROR_CODE_OK;
    task->running = 1;
    if(g_mk_task_monitor) {
        ret = mk_reg_task_monitor(task);
        if( MK_ERROR_CODE_OK != ret) {
            mk_mutex_unlock(task->mutex);
            return MK_ERROR_CODE_FAIL;
        }
    }
    ret = mk_create_thread((MK_THREAD_FUNC)mk_task_thread,task,&task->thread,MK_DEFAULT_STACK_SIZE);

    if( MK_ERROR_CODE_OK != mk_mutex_unlock(task->mutex)) {
        return MK_ERROR_CODE_FAIL;
    }
    return ret;
}
/* stop the media kenerl task */
void      mk_stop_task(MK_HANDLE handle)
{
    mk_task_ctx_t* task = (mk_task_ctx_t*)handle;
    if( MK_ERROR_CODE_OK != mk_mutex_lock(task->mutex)) {
        return ;
    }
    if(g_mk_task_monitor) {
        mk_unreg_task_monitor(task);
    }
    task->running       = 0;
    if( MK_ERROR_CODE_OK != mk_mutex_unlock(task->mutex)) {
        return ;
    }
}
/* run the media kenerl task by the main thread*/
int32_t   mk_do_task(MK_HANDLE handle,uint32_t argc,char** argv)
{
    mk_task_ctx_t* task = (mk_task_ctx_t*)handle;
    int nparamcount = 0;
    char* param = NULL;

    if(argc >= PARAM_COUNT_MAX)
    {
        return MK_ERROR_CODE_PARAM;
    }

    if( MK_ERROR_CODE_OK != mk_mutex_lock(task->mutex)) {
        return MK_ERROR_CODE_FAIL;
    }

    param = av_malloc(strlen(program_name)+1);
    strncpy(param,program_name,strlen(program_name));
    task->paramlist[nparamcount] = param;
    nparamcount++;

    unsigned long i = 0;
    for(i = 0;i < argc; i++)
    {
        unsigned long ulSize = strlen(argv[i]);
        param = av_malloc(ulSize+2);
        memset(param,0,ulSize+2);
        strncpy(&param[0],argv[i],ulSize);
        task->paramlist[nparamcount] = param;
        nparamcount++;
    }

    task->nparamcount = nparamcount;

    task->running = 1;
    if( MK_ERROR_CODE_OK != mk_mutex_unlock(task->mutex)) {
        return MK_ERROR_CODE_FAIL;
    }
    return mk_main_task(task);
}

/* get the media kenerl task run stat info */
int32_t   mk_get_task_status(MK_HANDLE handle,int* status, mk_task_stat_info_t* statInfo)
{
    if((NULL == status)||(NULL == statInfo)) {
        return MK_ERROR_CODE_PARAM;
    }
    mk_task_ctx_t* task = (mk_task_ctx_t*)handle;
    if( MK_ERROR_CODE_OK != mk_mutex_lock(task->mutex)) {
        return MK_ERROR_CODE_FAIL;
    }

    *status = task->status;
    memcpy(statInfo, &task->stat,sizeof(mk_task_stat_info_t));
    if( MK_ERROR_CODE_OK != mk_mutex_unlock(task->mutex)) {
        return MK_ERROR_CODE_FAIL;
    }
    return MK_ERROR_CODE_OK;
}

