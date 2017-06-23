#ifndef __MEDIA_KENERL_MUTEX_H__
#define __MEDIA_KENERL_MUTEX_H__
#include "mk_config.h"
#include "mk_def.h"
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#if MK_APP_OS == MK_OS_WIN32
#include <windows.h>
#endif
#if MK_APP_OS == MK_OS_LINUX
typedef struct tagMKMutex
{
    pthread_mutex_t     mutex;
    pthread_mutexattr_t attr;
}mk_mutex_t;

#elif MK_APP_OS == MK_OS_WIN32
typedef struct tagMKMutex
{
    HANDLE              mutex;
}mk_mutex_t;
#endif

mk_mutex_t *mk_create_mutex();
int32_t     mk_destroy_mutex( mk_mutex_t *pstMutex );
int32_t     mk_mutex_lock( mk_mutex_t *pstMutex );
int32_t     mk_mutex_unlock( mk_mutex_t *pstMutex );

#endif /* __MEDIA_KENERL_MUTEX_H__ */


