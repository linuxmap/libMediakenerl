#include "mk_thread.h"
#include "mk_config.h"
#include "mk_common.h"
#if MK_APP_OS == MK_OS_LINUX
#include <pthread.h>
#elif MK_APP_OS == MK_OS_WIN32
#endif


int32_t  mk_create_thread( MK_THREAD_FUNC pfnThread, void *args, mk_thread_t **pstSVSThread,uint32_t ulStackSize)
{
    mk_thread_t *pstThread = NULL ;

    pstThread = (mk_thread_t*)av_malloc(sizeof(mk_thread_t));
    if( NULL == pstThread )
    {
        return MK_ERROR_CODE_MEM;
    }

#if MK_APP_OS == MK_OS_LINUX
    if ( pthread_attr_init(&pstThread->attr) != 0 )
    {
        av_free(pstThread);
        return MK_ERROR_CODE_FAIL ;
    }

    pthread_attr_setdetachstate(&pstThread->attr, PTHREAD_CREATE_JOINABLE );

    if( 0 == ulStackSize )
    {
        ulStackSize = MK_DEFAULT_STACK_SIZE;
    }
    if (pthread_attr_setstacksize(&pstThread->attr, (size_t)ulStackSize))
    {
        av_free(pstThread);
        return MK_ERROR_CODE_FAIL ;
    }

    if ( pthread_create(&pstThread->pthead, &pstThread->attr, pfnThread, args) != 0 )
    {
        av_free(pstThread);
        return MK_ERROR_CODE_FAIL ;
    }
#elif MK_APP_OS == MK_OS_WIN32
    pstThread->pthead = CreateThread(NULL,ulStackSize,pfnThread,args,0,&pstThread->ptheadID);
    if (NULL == pstThread->pthead)
    {
        av_free(pstThread);
        return MK_ERROR_CODE_FAIL ;
    }
#endif
    *pstSVSThread = pstThread ;

    return MK_ERROR_CODE_OK;
}

int32_t mk_join_thread(mk_thread_t *pstMKThread)
{
#if MK_APP_OS == MK_OS_LINUX
    pthread_join(pstMKThread->pthead, 0);
#elif MK_APP_OS == MK_OS_WIN32
    (void)WaitForSingleObject(pstMKThread->pthead, 0);
    (void)CloseHandle(pstMKThread->pthead);
#endif
    return MK_ERROR_CODE_OK;
}


