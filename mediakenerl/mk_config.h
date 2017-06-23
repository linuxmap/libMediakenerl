#ifndef __MEDIA_KENERL_CONFIG_H__
#define __MEDIA_KENERL_CONFIG_H__


#define   MK_OS_LINUX                       1
#define   MK_OS_WIN32                       2

#define   MK_BIG_ENDIAN                     0
#define   MK_LITTLE_ENDIAN                  1


#ifdef ENV_LINUX
#define   MK_APP_OS                     MK_OS_LINUX
#define   MK_BYTE_ORDER                 MK_LITTLE_ENDIAN
#endif

#ifdef WIN32
#define   MK_APP_OS                     MK_OS_WIN32
#define   MK_BYTE_ORDER                 MK_LITTLE_ENDIAN

#define snprintf _snprintf
#define strcasecmp stricmp
#define strncasecmp strnicmp
#define vsnprintf _vsnprintf

#endif

#endif /*__MEDIA_KENERL_CONFIG_H__*/