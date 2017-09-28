#ifndef __LIB_MEDIA_KENERL_H__
#define __LIB_MEDIA_KENERL_H__
#ifdef WIN32
#ifdef LIBMEDIAKENERL_EXPORTS
#define MK_API __declspec(dllexport)
#else
#define MK_API __declspec(dllimport)
#endif
#else
#define MK_API
#endif
#include "mk_def.h"


/* init the media kenerl libary */
MK_API int32_t   mk_lib_init(log_callback log_cb,int32_t task_monitor);
/* release the media kenerl bibary */
MK_API void      mk_lib_release();
/* create a media kenerl handle for media task */
MK_API MK_HANDLE mk_create_handle();
/* destory the media kenerl handle */
MK_API void      mk_destory_handle(MK_HANDLE handle);
/* run the media kenerl task */
MK_API int32_t   mk_run_task(MK_HANDLE handle, uint32_t argc, char** argv);
/* stop the media kenerl task */
MK_API void      mk_stop_task(MK_HANDLE handle);
/* run the media kenerl task by the main thread*/
MK_API int32_t   mk_do_task(MK_HANDLE handle, uint32_t argc, char** argv);
/* get the media kenerl task run stat info */
MK_API int32_t   mk_get_task_status(MK_HANDLE handle, int* status, mk_task_stat_info_t* statInfo);

#endif /*__LIB_MEDIA_KENERL_H__*/