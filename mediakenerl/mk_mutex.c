#include "mk_mutex.h"
#include "mk_config.h"
#include "mk_common.h"
#if MK_APP_OS == MK_OS_LINUX
#include <pthread.h>
#elif MK_APP_OS == MK_OS_WIN32
#endif


mk_mutex_t *mk_create_mutex()
{
    int32_t ulResult = MK_ERROR_CODE_OK ;

    mk_mutex_t *pstMutex = NULL ;

    pstMutex = (mk_mutex_t *) av_malloc(sizeof(mk_mutex_t));
    if ( NULL == pstMutex  )
    {
        return NULL ;
    }
#if MK_APP_OS == MK_OS_LINUX
    if(pthread_mutexattr_init(&pstMutex->attr) != 0)
    {
        return NULL ;
    }
    pthread_mutexattr_settype(&pstMutex->attr, PTHREAD_MUTEX_RECURSIVE);

    ulResult = (int32_t)pthread_mutex_init( &pstMutex->mutex, &pstMutex->attr);
    if( MK_ERROR_CODE_OK != ulResult )
    {
        av_free( pstMutex );
        return NULL ;
    }
#elif MK_APP_OS == MK_OS_WIN32
    pstMutex->mutex = CreateMutex(NULL,0,NULL);
    if (NULL == pstMutex->mutex)
    {
        av_free( pstMutex );
        return NULL ;
    }
    (void)ulResult; //¹ýPCLINT
#endif
    return pstMutex ;
}


int32_t mk_destroy_mutex( mk_mutex_t *pstMutex )
{
    int32_t ulResult = MK_ERROR_CODE_OK ;

#if MK_APP_OS == MK_OS_LINUX
    pthread_mutex_destroy( &pstMutex->mutex );
#elif MK_APP_OS == MK_OS_WIN32
    (void)CloseHandle(pstMutex->mutex);
#endif
    av_free( pstMutex );

    return ulResult ;
}


int32_t mk_mutex_lock( mk_mutex_t *pstMutex )
{
    int32_t ulResult = MK_ERROR_CODE_OK;

    if(NULL == pstMutex)
    {
        return MK_ERROR_CODE_FAIL;
    }

#if MK_APP_OS == MK_OS_LINUX
    ulResult = (int32_t)pthread_mutex_lock(&pstMutex->mutex);
    if( MK_ERROR_CODE_OK != ulResult )
    {
        return ulResult ;
    }
#elif MK_APP_OS == MK_OS_WIN32
    ulResult = WaitForSingleObject(pstMutex->mutex,INFINITE);
    if(WAIT_OBJECT_0 != ulResult)
    {
        return MK_ERROR_CODE_FAIL;
    }
#endif
    return MK_ERROR_CODE_OK ;
}

int32_t mk_mutex_unlock( mk_mutex_t *pstMutex )
{
    int32_t ulResult = MK_ERROR_CODE_OK ;

#if MK_APP_OS == MK_OS_LINUX
    ulResult = (int32_t)pthread_mutex_unlock(&pstMutex->mutex);
    if( MK_ERROR_CODE_OK != ulResult )
    {
        return ulResult ;
    }
#elif MK_APP_OS == MK_OS_WIN32
    if((NULL == pstMutex)
        || (TRUE != ReleaseMutex(pstMutex->mutex)))
    {
        ulResult = MK_ERROR_CODE_FAIL;
        return ulResult;
    }
#endif
    return MK_ERROR_CODE_OK ;
}



