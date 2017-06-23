#ifndef __MEDIA_KENERL_THREAD_H__
#define __MEDIA_KENERL_THREAD_H__
#include "mk_config.h"
#include "mk_def.h"
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#if MK_APP_OS == MK_OS_WIN32
#include <windows.h>
#endif

#define  MK_DEFAULT_STACK_SIZE (128*1024)

#if MK_APP_OS == MK_OS_LINUX
typedef  void* ( * MK_THREAD_FUNC)(void *);
typedef struct tagMKThread
{
    pthread_attr_t attr;
    pthread_t pthead;
}mk_thread_t;

#elif MK_APP_OS == MK_OS_WIN32
typedef  uint32_t (__stdcall * MK_THREAD_FUNC)(void *);
typedef struct tagMKThread
{
    uint32_t ptheadID;
    HANDLE pthead;
}mk_thread_t;
#endif

int32_t mk_create_thread( MK_THREAD_FUNC pfnThread, void *args,
          mk_thread_t **pstMKThread,uint32_t ulStackSize);

int32_t mk_join_thread(mk_thread_t *pstMKThread);
#endif