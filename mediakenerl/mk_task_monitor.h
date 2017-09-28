#ifndef __MEDIA_KENERL_TASK_MONITOR_H__
#define __MEDIA_KENERL_TASK_MONITOR_H__
#include "mk_config.h"
#include "mk_def.h"
#include "mk_common.h"
#include <stdint.h>
#include <stdio.h>
#include <signal.h>

int32_t mk_init_task_monitor();
void    mk_release_task_monitor();
int32_t mk_reg_task_monitor(mk_task_ctx_t* task);
void    mk_report_task_monitor(mk_task_ctx_t* task);
void    mk_unreg_task_monitor(mk_task_ctx_t* task);
#endif