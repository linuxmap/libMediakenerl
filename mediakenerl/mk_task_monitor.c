#include "mk_task_monitor.h"
#include "mk_config.h"
#include "mk_common.h"
#include "mk_mutex.h"
#include "mk_thread.h"
#include <time.h>

#define MK_MONITOR_ONE_MINUTE 60000000  /*microseconds*/
#define MK_MONITOR_MINUTE     60        /*second */
#define MK_MONITOR_TIMEOUT    180       /*second */

typedef struct tagMKMonitorNode mk_monitor_node_t;


struct tagMKMonitorNode
{
    mk_task_ctx_t     *task;
    mk_monitor_node_t *next;
};

typedef struct tagMKMonitorCtx
{
    mk_thread_t       *thread;
    mk_mutex_t        *mutex;
    volatile int32_t   running;
    uint32_t           nodecount;
    mk_monitor_node_t *front;
}mk_monitor_ctx_t;

static mk_monitor_ctx_t* g_monitor_ctx = NULL;

static void mk_monitor_check_all_task(mk_monitor_ctx_t* ctx)
{
    time_t cur = time(NULL);
    mk_mutex_lock(ctx->mutex);
    mk_monitor_node_t *t_node = ctx->front;
    while(NULL != t_node) {
        if(NULL != t_node->task) {
            if(cur > t_node->task->heartbeat) {
                if(MK_MONITOR_MINUTE < (cur - t_node->task->heartbeat)) {
                    av_log(NULL, AV_LOG_ERROR, "******the task have one minute not report heartbeat******\n");
                }
                if(MK_MONITOR_TIMEOUT < (cur - t_node->task->heartbeat)) {
                    av_log(NULL, AV_LOG_ERROR, "******the task have three minute not report heartbeat,and restart it******\n");
                    t_node->task->running = 0;
                    t_node->task->exited  = 1;
                }
            }
        }
        t_node = t_node->next;
    }
    mk_mutex_unlock(ctx->mutex);
}

static  void* mk_monitor_thread(void * param)
{
    mk_monitor_ctx_t* monitor_ctx = (mk_monitor_ctx_t*)param;

    while(monitor_ctx->running) {
        mk_monitor_check_all_task(monitor_ctx);
        av_usleep(MK_MONITOR_ONE_MINUTE);
    }
    mk_destroy_mutex(monitor_ctx->mutex);
    av_free(monitor_ctx);
    g_monitor_ctx = NULL;
    return NULL;
}

int32_t mk_init_task_monitor()
{
    if(NULL != g_monitor_ctx) {
        return MK_ERROR_CODE_OK;
    }
    g_monitor_ctx = av_malloc(sizeof(mk_monitor_ctx_t));

    if(NULL == g_monitor_ctx) {
        return MK_ERROR_CODE_FAIL;
    }
    g_monitor_ctx->mutex     = mk_create_mutex();
    g_monitor_ctx->running   = 1;
    g_monitor_ctx->nodecount = 0;
    g_monitor_ctx->front     = (mk_monitor_node_t*)av_malloc(sizeof(mk_monitor_node_t));
    g_monitor_ctx->front->task = NULL;
    g_monitor_ctx->front->next = NULL;

    int32_t ret = MK_ERROR_CODE_OK;
    ret = mk_create_thread((MK_THREAD_FUNC)mk_monitor_thread,g_monitor_ctx,
                           &g_monitor_ctx->thread,MK_DEFAULT_STACK_SIZE);

    if( MK_ERROR_CODE_OK !=ret) {
        mk_destroy_mutex(g_monitor_ctx->mutex);
        g_monitor_ctx->mutex = NULL;
        av_free(g_monitor_ctx);
        return MK_ERROR_CODE_FAIL;
    }
    return MK_ERROR_CODE_OK;
}

void    mk_release_task_monitor()
{
    mk_mutex_lock(g_monitor_ctx->mutex);
    g_monitor_ctx->running   = 0;
    mk_mutex_unlock(g_monitor_ctx->mutex);
}
int32_t mk_reg_task_monitor(mk_task_ctx_t* task)
{
    mk_monitor_node_t *node = (mk_monitor_node_t*)av_malloc(sizeof(mk_monitor_node_t));
    if(NULL == node) {
        return MK_ERROR_CODE_FAIL;
    }
    node->next = NULL;
    node->task = task;
    mk_mutex_lock(g_monitor_ctx->mutex);
    mk_monitor_node_t *t_node = g_monitor_ctx->front;
    while(NULL != t_node) {
        if(NULL == t_node->next) {
            t_node->next = node;
            break;
        }
        t_node = t_node->next;
    }
    mk_mutex_unlock(g_monitor_ctx->mutex);
}
void    mk_report_task_monitor(mk_task_ctx_t* task)
{
    if(NULL == task) {
        return;
    }
    if( MK_ERROR_CODE_OK != mk_mutex_lock(task->mutex)) {
        return;
    }
    task->heartbeat = time(NULL);
    if( MK_ERROR_CODE_OK != mk_mutex_unlock(task->mutex)) {
        return;
    }

    return;
}
void    mk_unreg_task_monitor(mk_task_ctx_t* task)
{
    mk_mutex_lock(g_monitor_ctx->mutex);
    mk_monitor_node_t *p_node = g_monitor_ctx->front;
    mk_monitor_node_t *t_node = p_node->next;
    while(NULL != t_node) {
        if(task == t_node->task) {
            p_node->next = t_node->next;
            av_free(t_node);
            break;
        }
        p_node = t_node;
        t_node = p_node->next;
    }
    mk_mutex_unlock(g_monitor_ctx->mutex);
}

