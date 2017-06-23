#ifndef __MK_MEDIA_DEFINE_H__
#define __MK_MEDIA_DEFINE_H__
#include <stdint.h>
#include <stdio.h>
#include <signal.h>


typedef void*    MK_HANDLE;

enum MK_ERROR_CODE {
    MK_ERROR_CODE_FAIL        = -1,
    MK_ERROR_CODE_OK          = 0x00,
    MK_ERROR_CODE_MEM         = 0x01,
    MK_ERROR_CODE_PARAM       = 0x02,
    MK_ERROR_CODE_MAX
};

enum MK_TASK_STATUS {
    MK_TASK_STATUS_INIT       = 0x00,
    MK_TASK_STATUS_START      = 0x01,
    MK_TASK_STATUS_RUNNING    = 0x02,
    MK_TASK_STATUSS_STOP      = 0x03,
    MK_TASK_STATUS_INVALID    = 0xFF,
};

typedef struct  {
    uint64_t total_time;         /*unit:second*/
    uint64_t total_size;         /*unit:KB*/
    uint64_t input_packets;      /*unit:packets*/
    uint64_t ouput_packets;      /*unit:packets*/
    uint64_t frames;             /*unit:one*/
    uint64_t bitrate;            /*unit:kbps*/
    uint64_t dup_frames;         /*unit:one*/
    uint64_t drop_frames;        /*unit:one*/
    uint64_t video_size;         /*unit:KB*/
    uint64_t audio_size;         /*unit:KB*/
    uint64_t extra_size;         /*unit:KB*/
    uint64_t other_size;         /*unit:KB*/
    uint64_t subtitle_size;      /*unit:KB*/
    uint64_t data_size;          /*unit:KB*/
}mk_task_stat_info_t;

typedef void (*task_callback)(MK_HANDLE handle,int status,int ret,void* ctx,mk_task_stat_info_t* stat);
typedef void (*log_callback)(const char*);

#endif /*__MK_MEDIA_DEFINE_H__*/